#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>

// Error codes for filesystem operations
#define EXT2_SUCCESS           0
#define EXT2_ERR_NOT_FOUND    -1
#define EXT2_ERR_NO_SPACE     -2
#define EXT2_ERR_BAD_FORMAT   -3
#define EXT2_ERR_IO_ERROR     -4
#define EXT2_ERR_INVALID_ARG  -5
#define EXT2_ERR_PERMISSION   -6
#define EXT2_ERR_CORRUPTED    -7

// File type and permission bits
#define EXT2_S_IFMT   0xF000  // Format mask
#define EXT2_S_IFSOCK 0xC000  // Socket
#define EXT2_S_IFLNK  0xA000  // Symbolic link
#define EXT2_S_IFREG  0x8000  // Regular file
#define EXT2_S_IFBLK  0x6000  // Block device
#define EXT2_S_IFDIR  0x4000  // Directory
#define EXT2_S_IFCHR  0x2000  // Character device
#define EXT2_S_IFIFO  0x1000  // FIFO
#define EXT2_S_ISUID  0x0800  // Set UID bit
#define EXT2_S_ISGID  0x0400  // Set GID bit
#define EXT2_S_ISVTX  0x0200  // Sticky bit
#define EXT2_S_IRUSR  0x0100  // User read
#define EXT2_S_IWUSR  0x0080  // User write
#define EXT2_S_IXUSR  0x0040  // User execute
#define EXT2_S_IRGRP  0x0020  // Group read
#define EXT2_S_IWGRP  0x0010  // Group write
#define EXT2_S_IXGRP  0x0008  // Group execute
#define EXT2_S_IROTH  0x0004  // Others read
#define EXT2_S_IWOTH  0x0002  // Others write
#define EXT2_S_IXOTH  0x0001  // Others execute

// Superblock structure
typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t reserved_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
} __attribute__((packed)) ext2_superblock_t;

// Inode structure
typedef struct {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint8_t  osd2[12];
} __attribute__((packed)) ext2_inode_t;

// Directory entry structure
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed)) ext2_dir_entry_t;

// File entry structure for directory listing
typedef struct {
    char     name[256]; // Max filename length in ext2
    uint16_t mode;
    uint32_t size;
    uint32_t inode;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint16_t links_count;
    uint16_t uid;
    uint16_t gid;
} ext2_file_entry_t;

// Initialize the ext2 filesystem
int ext2_init(const char* device);

// Read file data into a buffer
// Returns: Number of bytes read or an error code (negative value)
int ext2_read_file(const char* path, char* buffer, int size);

// Write data to a file
// Returns: Number of bytes written or an error code (negative value)
int ext2_write_file(const char* path, const char* buffer, int size, int flags);

// List files in a directory
// Returns: Number of entries found or an error code (negative value)
int ext2_list_directory(const char* path, ext2_file_entry_t* entries, int max_entries);

// Check if a file exists
// Returns: 1 if file exists, 0 if not, or an error code (negative value)
int ext2_file_exists(const char* path);

// Get file size
// Returns: File size or an error code (negative value)
int ext2_get_file_size(const char* path);

// Create a directory
// Returns: 0 on success or an error code (negative value)
int ext2_mkdir(const char* path, uint16_t mode);

// Remove a file or empty directory
// Returns: 0 on success or an error code (negative value)
int ext2_remove(const char* path);

// Create a symbolic link
// Returns: 0 on success or an error code (negative value)
int ext2_symlink(const char* target, const char* linkpath);

// Read a symbolic link
// Returns: Number of bytes read or an error code (negative value)
int ext2_readlink(const char* path, char* buffer, int size);

// Change file permissions
// Returns: 0 on success or an error code (negative value)
int ext2_chmod(const char* path, uint16_t mode);

// Change file owner/group
// Returns: 0 on success or an error code (negative value)
int ext2_chown(const char* path, uint16_t uid, uint16_t gid);

#endif // EXT2_H