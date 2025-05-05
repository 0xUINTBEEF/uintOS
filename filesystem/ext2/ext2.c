#include "ext2.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../kernel/io.h"

#define BLOCK_SIZE 1024
#define EXT2_SUPER_MAGIC 0xEF53
#define ROOT_INODE 2

// Core filesystem structures
static ext2_superblock_t superblock;
static uint32_t block_size = 1024;
static uint32_t inodes_per_block = 0;
static uint32_t block_group_count = 0;
static char* device_path = NULL;

// Group descriptor structure
typedef struct {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint8_t reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

// In-memory cache of group descriptors
static ext2_group_desc_t* group_descs = NULL;

// Forward declarations for internal functions
static int read_block(uint32_t block_num, void* buffer);
static int write_block(uint32_t block_num, const void* buffer);
static int read_inode(uint32_t inode_num, ext2_inode_t* inode);
static int write_inode(uint32_t inode_num, const ext2_inode_t* inode);
static uint32_t path_to_inode(const char* path);
static int read_file_block(ext2_inode_t* inode, uint32_t block_index, void* buffer);
static int parse_path(const char* path, char* dir_path, char* filename);

int ext2_init(const char* device) {
    // Save device path
    device_path = (char*)device;
    
    // Read superblock (located at offset 1024 bytes)
    uint8_t sb_buffer[1024];
    if (read_block(1, sb_buffer) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Copy superblock from buffer
    memcpy(&superblock, sb_buffer, sizeof(ext2_superblock_t));
    
    // Verify magic number
    if (superblock.magic != EXT2_SUPER_MAGIC) {
        return EXT2_ERR_BAD_FORMAT;
    }
    
    // Calculate block size
    block_size = 1024 << superblock.log_block_size;
    
    // Calculate inodes per block
    inodes_per_block = block_size / sizeof(ext2_inode_t);
    
    // Calculate block group count
    block_group_count = (superblock.blocks_count - 1) / superblock.blocks_per_group + 1;
    
    // Allocate memory for group descriptors
    // In a real implementation, this would use dynamic memory allocation
    static ext2_group_desc_t group_descs_buffer[128]; // Max 128 groups
    group_descs = group_descs_buffer;
    
    // Read block group descriptors (located after superblock)
    uint32_t gdt_block = (block_size == 1024) ? 2 : 1;
    uint32_t gdt_size = sizeof(ext2_group_desc_t) * block_group_count;
    uint8_t gdt_buffer[1024]; // Assuming block size >= 1024
    
    if (read_block(gdt_block, gdt_buffer) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Copy group descriptors from buffer
    memcpy(group_descs, gdt_buffer, (gdt_size < block_size) ? gdt_size : block_size);
    
    return EXT2_SUCCESS;
}

int ext2_read_file(const char* path, char* buffer, int size) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if it's a regular file
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Calculate how many bytes to read (min of file size and buffer size)
    uint32_t bytes_to_read = (inode.size < (uint32_t)size) ? inode.size : (uint32_t)size;
    uint32_t bytes_read = 0;
    
    // Calculate how many blocks we need to read
    uint32_t blocks_to_read = (bytes_to_read + block_size - 1) / block_size;
    
    // Read each block
    for (uint32_t i = 0; i < blocks_to_read; i++) {
        uint8_t block_buffer[4096]; // Max block size
        int read_result = read_file_block(&inode, i, block_buffer);
        if (read_result < 0) {
            return read_result;
        }
        
        // Calculate bytes to copy from this block
        uint32_t bytes_to_copy = block_size;
        if (bytes_read + bytes_to_copy > bytes_to_read) {
            bytes_to_copy = bytes_to_read - bytes_read;
        }
        
        // Copy the data to the output buffer
        memcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
        bytes_read += bytes_to_copy;
    }
    
    return bytes_read;
}

int ext2_write_file(const char* path, const char* buffer, int size, int flags) {
    // This is a simplified implementation - in a real OS, this would be more complex
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    
    // If file doesn't exist and we're not creating it, return error
    if (inode_num == 0 && !(flags & EXT2_WRITE_CREATE)) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    ext2_inode_t inode;
    char dir_path[VFS_MAX_PATH];
    char filename[VFS_MAX_PATH];
    
    // If we need to create the file
    if (inode_num == 0) {
        // Parse path to get directory and filename
        if (parse_path(path, dir_path, filename) != 0) {
            return EXT2_ERR_INVALID_ARG;
        }
        
        // Get the parent directory inode
        uint32_t dir_inode_num = path_to_inode(dir_path);
        if (dir_inode_num == 0) {
            return EXT2_ERR_NOT_FOUND;
        }
        
        // Read parent directory inode
        ext2_inode_t dir_inode;
        if (read_inode(dir_inode_num, &dir_inode) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
        
        // Check if it's a directory
        if ((dir_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
            return EXT2_ERR_INVALID_ARG;
        }
        
        // Allocate new inode
        inode_num = ext2_allocate_inode();
        if (inode_num == 0) {
            return EXT2_ERR_NO_SPACE;
        }
        
        // Initialize the new inode
        memset(&inode, 0, sizeof(ext2_inode_t));
        inode.mode = EXT2_S_IFREG | 0644; // Regular file with rw-r--r-- permissions
        inode.uid = 0;  // Default to root user
        inode.gid = 0;  // Default to root group
        inode.size = 0;
        inode.atime = inode.ctime = inode.mtime = get_current_time();
        inode.links_count = 1;
        
        // Add entry to parent directory
        int result = ext2_add_dir_entry(dir_inode_num, &dir_inode, filename, inode_num, EXT2_FT_REG_FILE);
        if (result != 0) {
            // Free the allocated inode and return error
            ext2_free_inode(inode_num);
            return result;
        }
        
        // Update directory's modification time
        dir_inode.mtime = get_current_time();
        if (write_inode(dir_inode_num, &dir_inode) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
    } else {
        // Read the existing inode
        if (read_inode(inode_num, &inode) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
        
        // Check if it's a regular file
        if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
            return EXT2_ERR_INVALID_ARG;
        }
        
        // If truncate flag is set, free all existing blocks
        if (flags & EXT2_WRITE_TRUNCATE) {
            // Free all blocks associated with this file
            ext2_free_file_blocks(&inode);
            
            // Reset size to 0
            inode.size = 0;
            
            // Update block pointers
            memset(inode.block, 0, sizeof(inode.block));
        }
    }
    
    // Calculate how many blocks we need
    uint32_t bytes_to_write = size;
    uint32_t bytes_written = 0;
    uint32_t blocks_needed = (bytes_to_write + block_size - 1) / block_size;
    uint32_t block_index = 0;
    
    // Write data block by block
    while (bytes_written < (uint32_t)size) {
        // Allocate or get a block for this position
        uint32_t block_num = ext2_get_or_allocate_block(&inode, block_index);
        if (block_num == 0) {
            // Could not allocate block
            break;
        }
        
        // Calculate how many bytes to write to this block
        uint32_t block_offset = bytes_written % block_size;
        uint32_t bytes_this_block = block_size - block_offset;
        if (bytes_this_block > (size - bytes_written)) {
            bytes_this_block = size - bytes_written;
        }
        
        // If we're not writing a full block or not starting at the beginning,
        // we need to do a read-modify-write cycle
        uint8_t block_buffer[4096]; // Max block size
        if (block_offset > 0 || bytes_this_block < block_size) {
            if (read_block(block_num, block_buffer) != 0) {
                break;
            }
        }
        
        // Copy data to the block buffer
        memcpy(block_buffer + block_offset, buffer + bytes_written, bytes_this_block);
        
        // Write the block
        if (write_block(block_num, block_buffer) != 0) {
            break;
        }
        
        // Update counters
        bytes_written += bytes_this_block;
        block_index++;
    }
    
    // Update inode size if we've written data that extends the file
    if (bytes_written > 0) {
        uint32_t new_size;
        
        if (flags & EXT2_WRITE_APPEND) {
            new_size = inode.size + bytes_written;
        } else {
            new_size = bytes_written;
        }
        
        if (new_size > inode.size) {
            inode.size = new_size;
        }
        
        // Update timestamps
        inode.mtime = inode.atime = get_current_time();
        
        // Write the updated inode
        if (write_inode(inode_num, &inode) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
    }
    
    // Update filesystem metadata (superblock, group descriptors)
    ext2_update_fs_metadata();
    
    return bytes_written;
}

int ext2_list_directory(const char* path, ext2_file_entry_t* entries, int max_entries) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if it's a directory
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Variables to track reading progress
    uint32_t entries_found = 0;
    uint32_t bytes_read = 0;
    uint32_t current_block = 0;
    uint8_t block_buffer[4096]; // Max block size
    
    // Read directory entries block by block
    while (bytes_read < inode.size && entries_found < (uint32_t)max_entries) {
        // Read current directory block
        int read_result = read_file_block(&inode, current_block, block_buffer);
        if (read_result < 0) {
            return read_result;
        }
        
        // Process directory entries in this block
        uint32_t offset = 0;
        while (offset < block_size && entries_found < (uint32_t)max_entries) {
            ext2_dir_entry_t* dir_entry = (ext2_dir_entry_t*)(block_buffer + offset);
            
            // If entry is invalid or zero inode, skip
            if (dir_entry->rec_len == 0 || dir_entry->inode == 0) {
                break;
            }
            
            // Skip "." and ".." entries
            if (!(dir_entry->name_len == 1 && dir_entry->name[0] == '.') &&
                !(dir_entry->name_len == 2 && dir_entry->name[0] == '.' && dir_entry->name[1] == '.')) {
                
                // Read the entry's inode
                ext2_inode_t entry_inode;
                if (read_inode(dir_entry->inode, &entry_inode) == 0) {
                    // Copy name (ensure null-terminated)
                    memcpy(entries[entries_found].name, dir_entry->name, dir_entry->name_len);
                    entries[entries_found].name[dir_entry->name_len] = '\0';
                    
                    // Copy other attributes
                    entries[entries_found].mode = entry_inode.mode;
                    entries[entries_found].size = entry_inode.size;
                    entries[entries_found].inode = dir_entry->inode;
                    entries[entries_found].atime = entry_inode.atime;
                    entries[entries_found].ctime = entry_inode.ctime;
                    entries[entries_found].mtime = entry_inode.mtime;
                    entries[entries_found].links_count = entry_inode.links_count;
                    entries[entries_found].uid = entry_inode.uid;
                    entries[entries_found].gid = entry_inode.gid;
                    
                    entries_found++;
                }
            }
            
            // Move to next directory entry
            offset += dir_entry->rec_len;
        }
        
        // Move to next block
        current_block++;
        bytes_read += block_size;
    }
    
    return entries_found;
}

int ext2_file_exists(const char* path) {
    return path_to_inode(path) != 0;
}

int ext2_get_file_size(const char* path) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if it's a regular file
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    return inode.size;
}

int ext2_mkdir(const char* path, uint16_t mode) {
    // Parse path to get directory and filename
    char dir_path[VFS_MAX_PATH];
    char dirname[VFS_MAX_PATH];
    
    if (parse_path(path, dir_path, dirname) != 0) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Check if the directory already exists
    if (ext2_file_exists(path)) {
        return EXT2_ERR_EXISTS;
    }
    
    // Get the parent directory inode
    uint32_t parent_inode_num = path_to_inode(dir_path);
    if (parent_inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read parent directory inode
    ext2_inode_t parent_inode;
    if (read_inode(parent_inode_num, &parent_inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if parent is a directory
    if ((parent_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return EXT2_ERR_NOT_DIR;
    }
    
    // Allocate a new inode for the directory
    uint32_t new_inode_num = ext2_allocate_inode();
    if (new_inode_num == 0) {
        return EXT2_ERR_NO_SPACE;
    }
    
    // Initialize the new directory inode
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = EXT2_S_IFDIR | (mode & 0xFFF); // Directory with specified permissions
    new_inode.uid = 0;  // Default to root user
    new_inode.gid = 0;  // Default to root group
    new_inode.size = 0;
    new_inode.atime = new_inode.ctime = new_inode.mtime = get_current_time();
    new_inode.links_count = 2; // "." + parent's entry
    
    // Allocate first block for the directory
    uint32_t new_block = ext2_allocate_block();
    if (new_block == 0) {
        ext2_free_inode(new_inode_num);
        return EXT2_ERR_NO_SPACE;
    }
    
    // Set the block in the inode
    new_inode.block[0] = new_block;
    
    // Create the "." and ".." entries in the new directory
    uint8_t block_buffer[4096]; // Max block size
    memset(block_buffer, 0, block_size);
    
    // Create "." entry (points to itself)
    ext2_dir_entry_t* dot_entry = (ext2_dir_entry_t*)block_buffer;
    dot_entry->inode = new_inode_num;
    dot_entry->rec_len = 12; // 8 bytes for header + 1 byte for name + padding for 4-byte alignment
    dot_entry->name_len = 1;
    dot_entry->file_type = EXT2_FT_DIR;
    strcpy(dot_entry->name, ".");
    
    // Create ".." entry (points to parent)
    ext2_dir_entry_t* dotdot_entry = (ext2_dir_entry_t*)(block_buffer + dot_entry->rec_len);
    dotdot_entry->inode = parent_inode_num;
    dotdot_entry->rec_len = block_size - dot_entry->rec_len; // Use rest of block
    dotdot_entry->name_len = 2;
    dotdot_entry->file_type = EXT2_FT_DIR;
    strcpy(dotdot_entry->name, "..");
    
    // Write the block
    if (write_block(new_block, block_buffer) != 0) {
        ext2_free_block(new_block);
        ext2_free_inode(new_inode_num);
        return EXT2_ERR_IO_ERROR;
    }
    
    // Update new directory inode size
    new_inode.size = block_size;
    
    // Write the new inode
    if (write_inode(new_inode_num, &new_inode) != 0) {
        ext2_free_block(new_block);
        ext2_free_inode(new_inode_num);
        return EXT2_ERR_IO_ERROR;
    }
    
    // Add directory entry in the parent directory
    int add_result = ext2_add_dir_entry(parent_inode_num, &parent_inode, dirname, 
                                         new_inode_num, EXT2_FT_DIR);
    
    if (add_result != 0) {
        ext2_free_block(new_block);
        ext2_free_inode(new_inode_num);
        return add_result;
    }
    
    // Increment link count of parent directory (for the ".." entry)
    parent_inode.links_count++;
    parent_inode.mtime = get_current_time();
    
    // Write the updated parent inode
    if (write_inode(parent_inode_num, &parent_inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Update filesystem metadata
    ext2_update_fs_metadata();
    
    return EXT2_SUCCESS;
}

int ext2_remove(const char* path) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Parse path to get directory and filename
    char dir_path[VFS_MAX_PATH];
    char filename[VFS_MAX_PATH];
    
    if (parse_path(path, dir_path, filename) != 0) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Get the parent directory inode
    uint32_t parent_inode_num = path_to_inode(dir_path);
    if (parent_inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read parent directory inode
    ext2_inode_t parent_inode;
    if (read_inode(parent_inode_num, &parent_inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if it's a directory and if so, ensure it's empty
    if ((inode.mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
        // Cannot remove "." or ".."
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
            return EXT2_ERR_PERMISSION;
        }
        
        // Check if the directory is empty
        ext2_file_entry_t entries[2];
        int entry_count = ext2_list_directory(path, entries, 2);
        
        if (entry_count > 0) {
            return EXT2_ERR_NOT_EMPTY;
        }
        
        // Decrement parent's link count (for the ".." entry)
        parent_inode.links_count--;
    }
    
    // Remove directory entry from parent
    int remove_result = ext2_remove_dir_entry(parent_inode_num, &parent_inode, filename);
    if (remove_result != 0) {
        return remove_result;
    }
    
    // Update parent's modification time
    parent_inode.mtime = get_current_time();
    
    // Write the updated parent inode
    if (write_inode(parent_inode_num, &parent_inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Decrement inode's link count
    inode.links_count--;
    
    // If link count reaches zero, free the inode and its blocks
    if (inode.links_count == 0) {
        // Free all blocks associated with the inode
        ext2_free_file_blocks(&inode);
        
        // Free the inode
        ext2_free_inode(inode_num);
    } else {
        // Write the updated inode
        if (write_inode(inode_num, &inode) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
    }
    
    // Update filesystem metadata
    ext2_update_fs_metadata();
    
    return EXT2_SUCCESS;
}

int ext2_symlink(const char* target, const char* linkpath) {
    // Parse path to get directory and filename for the link
    char dir_path[VFS_MAX_PATH];
    char link_name[VFS_MAX_PATH];
    
    if (parse_path(linkpath, dir_path, link_name) != 0) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Check if the symlink already exists
    if (ext2_file_exists(linkpath)) {
        return EXT2_ERR_EXISTS;
    }
    
    // Get the parent directory inode
    uint32_t parent_inode_num = path_to_inode(dir_path);
    if (parent_inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read parent directory inode
    ext2_inode_t parent_inode;
    if (read_inode(parent_inode_num, &parent_inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if parent is a directory
    if ((parent_inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        return EXT2_ERR_NOT_DIR;
    }
    
    // Allocate a new inode for the symlink
    uint32_t symlink_inode_num = ext2_allocate_inode();
    if (symlink_inode_num == 0) {
        return EXT2_ERR_NO_SPACE;
    }
    
    // Initialize the symlink inode
    ext2_inode_t symlink_inode;
    memset(&symlink_inode, 0, sizeof(ext2_inode_t));
    symlink_inode.mode = EXT2_S_IFLNK | 0777; // Symlink with full permissions
    symlink_inode.uid = 0;  // Default to root user
    symlink_inode.gid = 0;  // Default to root group
    symlink_inode.size = strlen(target);
    symlink_inode.atime = symlink_inode.ctime = symlink_inode.mtime = get_current_time();
    symlink_inode.links_count = 1;
    
    // In EXT2, if the symlink target is small enough (<= 60 bytes),
    // it's stored directly in the inode's block pointers (fast symlinks)
    if (symlink_inode.size <= 60) {
        // Copy target path directly into the block pointers
        strcpy((char*)symlink_inode.block, target);
    } else {
        // For larger targets, allocate blocks and store the path there
        uint32_t blocks_needed = (symlink_inode.size + block_size - 1) / block_size;
        
        // Allocate blocks and store the target path
        for (uint32_t i = 0; i < blocks_needed; i++) {
            uint32_t block_num = ext2_allocate_block();
            if (block_num == 0) {
                // Free previously allocated blocks
                for (uint32_t j = 0; j < i; j++) {
                    ext2_free_block(symlink_inode.block[j]);
                }
                ext2_free_inode(symlink_inode_num);
                return EXT2_ERR_NO_SPACE;
            }
            
            // Set block pointer in inode
            symlink_inode.block[i] = block_num;
            
            // Calculate bytes to write to this block
            uint32_t offset = i * block_size;
            uint32_t bytes_this_block = block_size;
            if (offset + bytes_this_block > symlink_inode.size) {
                bytes_this_block = symlink_inode.size - offset;
            }
            
            // Prepare block buffer
            uint8_t block_buffer[4096]; // Max block size
            memset(block_buffer, 0, block_size);
            memcpy(block_buffer, target + offset, bytes_this_block);
            
            // Write the block
            if (write_block(block_num, block_buffer) != 0) {
                // Free allocated blocks
                for (uint32_t j = 0; j <= i; j++) {
                    ext2_free_block(symlink_inode.block[j]);
                }
                ext2_free_inode(symlink_inode_num);
                return EXT2_ERR_IO_ERROR;
            }
        }
    }
    
    // Write the inode
    if (write_inode(symlink_inode_num, &symlink_inode) != 0) {
        // Free all blocks if we allocated them
        if (symlink_inode.size > 60) {
            ext2_free_file_blocks(&symlink_inode);
        }
        ext2_free_inode(symlink_inode_num);
        return EXT2_ERR_IO_ERROR;
    }
    
    // Add entry to parent directory
    int result = ext2_add_dir_entry(parent_inode_num, &parent_inode, link_name, 
                                   symlink_inode_num, EXT2_FT_SYMLINK);
    
    if (result != 0) {
        // Free all blocks if we allocated them
        if (symlink_inode.size > 60) {
            ext2_free_file_blocks(&symlink_inode);
        }
        ext2_free_inode(symlink_inode_num);
        return result;
    }
    
    // Update parent's modification time
    parent_inode.mtime = get_current_time();
    
    // Write the parent inode
    if (write_inode(parent_inode_num, &parent_inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Update filesystem metadata
    ext2_update_fs_metadata();
    
    return EXT2_SUCCESS;
}

int ext2_readlink(const char* path, char* buffer, int size) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Check if it's a symlink
    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFLNK) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // For small symlinks, the data is stored in the inode itself
    if (inode.size <= 60) { // 60 = size of block pointers in inode
        int copy_size = (inode.size < (uint32_t)size) ? inode.size : size;
        memcpy(buffer, inode.block, copy_size);
        return copy_size;
    } else {
        // For larger symlinks, read from blocks
        return ext2_read_file(path, buffer, size);
    }
}

int ext2_chmod(const char* path, uint16_t mode) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Update the mode (preserve file type bits)
    uint16_t file_type = inode.mode & EXT2_S_IFMT;
    inode.mode = file_type | (mode & ~EXT2_S_IFMT);
    
    // Write the inode back
    if (write_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    return EXT2_SUCCESS;
}

int ext2_chown(const char* path, uint16_t uid, uint16_t gid) {
    // Get the inode number for the path
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == 0) {
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Read the inode
    ext2_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Update UID and GID
    inode.uid = uid;
    inode.gid = gid;
    
    // Write the inode back
    if (write_inode(inode_num, &inode) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    return EXT2_SUCCESS;
}

// Internal function implementations

// Function to read a block from the filesystem
static int read_block(uint32_t block_num, void* buffer) {
    // Use proper block device operations to read from the actual device
    
    // Check if the device path is valid
    if (!device_path) {
        log_error("EXT2", "No device path specified");
        return EXT2_ERR_IO_ERROR;
    }
    
    // Get the block device interface from the VFS
    vfs_block_device_t* block_dev = vfs_get_block_device(device_path);
    if (!block_dev) {
        log_error("EXT2", "Failed to get block device: %s", device_path);
        return EXT2_ERR_IO_ERROR;
    }
    
    // Ensure the block device has the necessary operations
    if (!block_dev->operations || !block_dev->operations->read_blocks) {
        log_error("EXT2", "Block device missing required operations");
        return EXT2_ERR_IO_ERROR;
    }
    
    // Calculate block offset based on filesystem block size
    uint64_t lba = block_num;
    
    // Convert ext2 block number to device block number if block sizes differ
    if (block_dev->block_size != block_size) {
        lba = (block_num * block_size) / block_dev->block_size;
    }
    
    // Calculate number of device blocks to read
    uint32_t blocks_to_read = (block_size + block_dev->block_size - 1) / block_dev->block_size;
    
    // For devices with block size < ext2 block size, we might need a temporary buffer
    void* read_buffer = buffer;
    
    if (block_dev->block_size < block_size && blocks_to_read > 1) {
        // Allocate a temporary aligned buffer for the read
        read_buffer = malloc(blocks_to_read * block_dev->block_size);
        if (!read_buffer) {
            log_error("EXT2", "Failed to allocate read buffer");
            return EXT2_ERR_IO_ERROR;
        }
    }
    
    // Read the block data from the block device
    int result = block_dev->operations->read_blocks(block_dev, lba, blocks_to_read, read_buffer);
    
    // Check for read errors
    if (result != blocks_to_read) {
        log_error("EXT2", "Block device read error: %d", result);
        if (read_buffer != buffer) {
            free(read_buffer);
        }
        return EXT2_ERR_IO_ERROR;
    }
    
    // If we used a temporary buffer, copy the data to the caller's buffer
    if (read_buffer != buffer) {
        memcpy(buffer, read_buffer, block_size);
        free(read_buffer);
    }
    
    return 0;  // Success
}

// Write a block to the filesystem
static int write_block(uint32_t block_num, const void* buffer) {
    // Use proper block device operations to write to the actual device
    
    // Check if the device path is valid
    if (!device_path) {
        log_error("EXT2", "No device path specified");
        return EXT2_ERR_IO_ERROR;
    }
    
    // Get the block device interface from the VFS
    vfs_block_device_t* block_dev = vfs_get_block_device(device_path);
    if (!block_dev) {
        log_error("EXT2", "Failed to get block device: %s", device_path);
        return EXT2_ERR_IO_ERROR;
    }
    
    // Ensure the block device has the necessary operations
    if (!block_dev->operations || !block_dev->operations->write_blocks) {
        log_error("EXT2", "Block device missing required write operations");
        return EXT2_ERR_IO_ERROR;
    }
    
    // Calculate block offset based on filesystem block size
    uint64_t lba = block_num;
    
    // Convert ext2 block number to device block number if block sizes differ
    if (block_dev->block_size != block_size) {
        lba = (block_num * block_size) / block_dev->block_size;
    }
    
    // Calculate number of device blocks to write
    uint32_t blocks_to_write = (block_size + block_dev->block_size - 1) / block_dev->block_size;
    
    // For devices with block size < ext2 block size, we need to handle block alignment
    if (block_dev->block_size < block_size && blocks_to_write > 1) {
        void* write_buffer = malloc(blocks_to_write * block_dev->block_size);
        if (!write_buffer) {
            log_error("EXT2", "Failed to allocate write buffer");
            return EXT2_ERR_IO_ERROR;
        }
        
        // For proper read-modify-write, first read the blocks
        int read_result = block_dev->operations->read_blocks(block_dev, lba, blocks_to_write, write_buffer);
        if (read_result != blocks_to_write) {
            log_error("EXT2", "Block device read error during write: %d", read_result);
            free(write_buffer);
            return EXT2_ERR_IO_ERROR;
        }
        
        // Now copy our buffer data over the read data
        memcpy(write_buffer, buffer, block_size);
        
        // Write the modified buffer to the device
        int write_result = block_dev->operations->write_blocks(block_dev, lba, blocks_to_write, write_buffer);
        
        free(write_buffer);
        
        if (write_result != blocks_to_write) {
            log_error("EXT2", "Block device write error: %d", write_result);
            return EXT2_ERR_IO_ERROR;
        }
    } else {
        // Block sizes match or the filesystem block size is smaller, direct write is possible
        int write_result = block_dev->operations->write_blocks(block_dev, lba, blocks_to_write, buffer);
        
        if (write_result != blocks_to_write) {
            log_error("EXT2", "Block device write error: %d", write_result);
            return EXT2_ERR_IO_ERROR;
        }
    }
    
    // Issue a sync command to ensure data is written to the physical medium
    if (block_dev->operations->sync) {
        block_dev->operations->sync(block_dev);
    }
    
    return 0;  // Success
}

// Read an inode from the filesystem
static int read_inode(uint32_t inode_num, ext2_inode_t* inode) {
    if (inode_num < 1 || inode_num > superblock.inodes_count) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Calculate block group
    uint32_t group = (inode_num - 1) / superblock.inodes_per_group;
    
    // Calculate local inode index
    uint32_t index = (inode_num - 1) % superblock.inodes_per_group;
    
    // Calculate block containing this inode
    uint32_t block = group_descs[group].inode_table + (index * sizeof(ext2_inode_t)) / block_size;
    
    // Calculate offset within the block
    uint32_t offset = (index * sizeof(ext2_inode_t)) % block_size;
    
    // Read the block
    uint8_t block_buffer[4096]; // Max block size
    if (read_block(block, block_buffer) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Copy the inode from the block
    memcpy(inode, block_buffer + offset, sizeof(ext2_inode_t));
    
    return 0;
}

// Write an inode to the filesystem
static int write_inode(uint32_t inode_num, const ext2_inode_t* inode) {
    if (inode_num < 1 || inode_num > superblock.inodes_count) {
        return EXT2_ERR_INVALID_ARG;
    }
    
    // Calculate block group
    uint32_t group = (inode_num - 1) / superblock.inodes_per_group;
    
    // Calculate local inode index
    uint32_t index = (inode_num - 1) % superblock.inodes_per_group;
    
    // Calculate block containing this inode
    uint32_t block = group_descs[group].inode_table + (index * sizeof(ext2_inode_t)) / block_size;
    
    // Calculate offset within the block
    uint32_t offset = (index * sizeof(ext2_inode_t)) % block_size;
    
    // Read the block
    uint8_t block_buffer[4096]; // Max block size
    if (read_block(block, block_buffer) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    // Update the inode in the block
    memcpy(block_buffer + offset, inode, sizeof(ext2_inode_t));
    
    // Write the block back
    if (write_block(block, block_buffer) != 0) {
        return EXT2_ERR_IO_ERROR;
    }
    
    return 0;
}

// Find the inode number for a given path
static uint32_t path_to_inode(const char* path) {
    if (path == NULL) {
        return 0;
    }
    
    // Root directory is always inode 2
    if (path[0] == '/' && path[1] == '\0') {
        return ROOT_INODE;
    }
    
    // Start at root
    uint32_t current_inode = ROOT_INODE;
    
    // Skip leading slash
    if (path[0] == '/') {
        path++;
    }
    
    // Buffer for path component
    char component[256];
    int i = 0;
    
    // Iterate through path components
    while (*path) {
        // Reset component buffer
        i = 0;
        
        // Extract next path component
        while (*path && *path != '/' && i < 255) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        
        // Skip slash
        if (*path == '/') {
            path++;
        }
        
        // Find this component in the current directory
        ext2_inode_t inode;
        if (read_inode(current_inode, &inode) != 0) {
            return 0;
        }
        
        // Check if it's a directory
        if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
            return 0;
        }
        
        // Look for the component in this directory
        uint32_t next_inode = 0;
        
        // Read directory blocks and search for the component
        uint32_t bytes_read = 0;
        uint32_t current_block = 0;
        uint8_t block_buffer[4096]; // Max block size
        
        while (bytes_read < inode.size && next_inode == 0) {
            // Read current directory block
            if (read_file_block(&inode, current_block, block_buffer) != 0) {
                return 0;
            }
            
            // Process directory entries in this block
            uint32_t offset = 0;
            while (offset < block_size) {
                ext2_dir_entry_t* dir_entry = (ext2_dir_entry_t*)(block_buffer + offset);
                
                // If entry is invalid or zero inode, skip to next block
                if (dir_entry->rec_len == 0 || dir_entry->inode == 0) {
                    break;
                }
                
                // Check if this entry matches our component
                if (dir_entry->name_len == strlen(component) &&
                    strncmp(dir_entry->name, component, dir_entry->name_len) == 0) {
                    next_inode = dir_entry->inode;
                    break;
                }
                
                // Move to next directory entry
                offset += dir_entry->rec_len;
            }
            
            // Move to next block
            current_block++;
            bytes_read += block_size;
        }
        
        // If component not found, return 0
        if (next_inode == 0) {
            return 0;
        }
        
        // Move to next component
        current_inode = next_inode;
    }
    
    return current_inode;
}

// Read a specific block from a file given by inode
static int read_file_block(ext2_inode_t* inode, uint32_t block_index, void* buffer) {
    uint32_t block_num = 0;
    
    // Direct blocks (0-11)
    if (block_index < 12) {
        block_num = inode->block[block_index];
    }
    // Indirect block (12)
    else if (block_index < 12 + block_size / 4) {
        // Read indirect block
        uint32_t indirect_block[block_size / 4];
        if (read_block(inode->block[12], indirect_block) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
        
        block_num = indirect_block[block_index - 12];
    }
    // Double indirect block (13)
    else if (block_index < 12 + block_size / 4 + (block_size / 4) * (block_size / 4)) {
        uint32_t index1 = (block_index - 12 - block_size / 4) / (block_size / 4);
        uint32_t index2 = (block_index - 12 - block_size / 4) % (block_size / 4);
        
        // Read double indirect block
        uint32_t dind_block[block_size / 4];
        if (read_block(inode->block[13], dind_block) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
        
        // Read indirect block
        uint32_t ind_block[block_size / 4];
        if (read_block(dind_block[index1], ind_block) != 0) {
            return EXT2_ERR_IO_ERROR;
        }
        
        block_num = ind_block[index2];
    }
    // Triple indirect block (14) - for very large files
    else {
        // For simplicity, we're not implementing triple indirect blocks
        return EXT2_ERR_INVALID_ARG;
    }
    
    // If block number is 0, this is a sparse file (hole), fill with zeros
    if (block_num == 0) {
        memset(buffer, 0, block_size);
        return 0;
    }
    
    // Read the actual data block
    return read_block(block_num, buffer);
}

// Parse a path into directory path and filename components
static int parse_path(const char* path, char* dir_path, char* filename) {
    if (!path || !dir_path || !filename) {
        return -1;
    }
    
    // Find the last slash in the path
    const char* last_slash = strrchr(path, '/');
    
    if (!last_slash) {
        // No slash, current directory is implied
        strcpy(dir_path, ".");
        strcpy(filename, path);
    } else if (last_slash == path) {
        // Slash is first character, file is in root directory
        strcpy(dir_path, "/");
        strcpy(filename, last_slash + 1);
    } else {
        // Copy the directory part
        int dir_len = last_slash - path;
        strncpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
        
        // Copy the filename part
        strcpy(filename, last_slash + 1);
    }
    
    return 0;
}