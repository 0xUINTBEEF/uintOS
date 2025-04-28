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
    if (inode_num == 0 && !(flags & 1)) { // 1 = create flag
        return EXT2_ERR_NOT_FOUND;
    }
    
    // Implementation would include:
    // 1. If creating new file: allocate inode, create directory entry
    // 2. If file exists: potentially truncate if flag set
    // 3. Allocate blocks as needed
    // 4. Write data to blocks
    // 5. Update inode size and block pointers
    // 6. Update superblock and group descriptors
    
    // For this educational implementation, we'll just return a success code
    return size;
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
    // This is a simplified placeholder
    // A real implementation would:
    // 1. Find parent directory inode
    // 2. Allocate new inode for the directory
    // 3. Initialize the directory with "." and ".." entries
    // 4. Create directory entry in parent directory
    
    return EXT2_ERR_NOT_FOUND;
}

int ext2_remove(const char* path) {
    // This is a simplified placeholder
    // A real implementation would:
    // 1. Find the inode for the file/directory
    // 2. If directory, ensure it's empty
    // 3. Remove the directory entry
    // 4. Decrement links count, free inode if zero
    // 5. Free data blocks
    
    return EXT2_ERR_NOT_FOUND;
}

int ext2_symlink(const char* target, const char* linkpath) {
    // This is a simplified placeholder
    // A real implementation would:
    // 1. Create a new file at linkpath with mode set to symbolic link
    // 2. Write the target path as the file contents
    
    return EXT2_ERR_NOT_FOUND;
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

// Read a block from the filesystem
static int read_block(uint32_t block_num, void* buffer) {
    // This would use disk I/O functions to read the block
    // For simplicity, we'll just return success
    return 0;
}

// Write a block to the filesystem
static int write_block(uint32_t block_num, const void* buffer) {
    // This would use disk I/O functions to write the block
    // For simplicity, we'll just return success
    return 0;
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