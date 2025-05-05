#ifndef VFS_H
#define VFS_H

#include <stdint.h>

/* VFS Error Codes */
#define VFS_SUCCESS             0
#define VFS_ERR_NOT_FOUND      -1
#define VFS_ERR_EXISTS         -2
#define VFS_ERR_IO_ERROR       -3
#define VFS_ERR_NO_SPACE       -4
#define VFS_ERR_INVALID_ARG    -5
#define VFS_ERR_NOT_DIR        -6
#define VFS_ERR_NOT_FILE       -7
#define VFS_ERR_NOT_EMPTY      -8
#define VFS_ERR_READONLY       -9
#define VFS_ERR_UNSUPPORTED    -10
#define VFS_ERR_JOURNAL_FULL   -11
#define VFS_ERR_CORRUPTED      -12
#define VFS_ERR_PERMISSION     -13
#define VFS_ERR_LOCKED         -14
#define VFS_ERR_TIMEOUT        -15
#define VFS_ERR_UNKNOWN        -16

/* File Types */
#define VFS_TYPE_FILE          1
#define VFS_TYPE_DIRECTORY     2
#define VFS_TYPE_SYMLINK       3
#define VFS_TYPE_DEVICE        4
#define VFS_TYPE_SOCKET        5
#define VFS_TYPE_PIPE          6
#define VFS_TYPE_SPECIAL       7

/* File Attributes */
#define VFS_ATTR_READ          0x01
#define VFS_ATTR_WRITE         0x02
#define VFS_ATTR_EXECUTE       0x04
#define VFS_ATTR_HIDDEN        0x08
#define VFS_ATTR_SYSTEM        0x10
#define VFS_ATTR_ARCHIVE       0x20
#define VFS_ATTR_ENCRYPTED     0x40
#define VFS_ATTR_COMPRESSED    0x80

/* Extended File Attributes */
#define VFS_XATTR_IMMUTABLE    0x0100
#define VFS_XATTR_APPEND_ONLY  0x0200
#define VFS_XATTR_NO_DUMP      0x0400
#define VFS_XATTR_NO_ATIME     0x0800
#define VFS_XATTR_SYNC         0x1000
#define VFS_XATTR_JOURNAL_DATA 0x2000

/* File Open Modes */
#define VFS_OPEN_READ          0x01
#define VFS_OPEN_WRITE         0x02
#define VFS_OPEN_APPEND        0x04
#define VFS_OPEN_CREATE        0x08
#define VFS_OPEN_TRUNCATE      0x10
#define VFS_OPEN_DIRECT        0x20
#define VFS_OPEN_SYNC          0x40
#define VFS_OPEN_NONBLOCK      0x80
#define VFS_OPEN_TEMPORARY     0x100
#define VFS_OPEN_EXCLUSIVE     0x200

/* Mount Flags */
#define VFS_MOUNT_READONLY     0x01
#define VFS_MOUNT_NOEXEC       0x02
#define VFS_MOUNT_NOSUID       0x04
#define VFS_MOUNT_NODEV        0x08
#define VFS_MOUNT_SYNC         0x10
#define VFS_MOUNT_REMOUNT      0x20
#define VFS_MOUNT_FORCE        0x40
#define VFS_MOUNT_JOURNAL      0x80

/* Journal Options */
#define VFS_JOURNAL_METADATA   0x01  /* Journal only metadata changes */
#define VFS_JOURNAL_DATA       0x02  /* Journal both metadata and data */
#define VFS_JOURNAL_ORDERED    0x04  /* Write data before metadata commits */
#define VFS_JOURNAL_ASYNC      0x08  /* Asynchronous journal commits */

/* Seek Origins */
#define VFS_SEEK_SET           0
#define VFS_SEEK_CUR           1
#define VFS_SEEK_END           2

/* Maximum limits */
#define VFS_MAX_PATH           256
#define VFS_MAX_FILENAME       128
#define VFS_MAX_MOUNTS         16
#define VFS_MAX_CACHE_BLOCKS   256
#define VFS_MAX_OPEN_FILES     64

/* Cache control flags */
#define VFS_CACHE_READ         0x01
#define VFS_CACHE_WRITE        0x02
#define VFS_CACHE_METADATA     0x04
#define VFS_CACHE_DISABLE      0x08

/* Forward declarations */
typedef struct vfs_file_s vfs_file_t;
typedef struct vfs_dirent_s vfs_dirent_t;
typedef struct vfs_stat_s vfs_stat_t;
typedef struct vfs_filesystem_s vfs_filesystem_t;
typedef struct vfs_mount_s vfs_mount_t;
typedef struct vfs_journal_s vfs_journal_t;
typedef struct vfs_cache_s vfs_cache_t;
typedef struct vfs_transaction_s vfs_transaction_t;

/**
 * VFS file entry - represents a directory entry
 */
struct vfs_dirent_s {
    char name[VFS_MAX_FILENAME];
    uint32_t type;           /* File type (file, directory, etc) */
    uint32_t attributes;     /* File attributes */
    uint64_t size;           /* File size in bytes */
    uint32_t time_create;    /* Creation time */
    uint32_t time_modify;    /* Last modification time */
    uint32_t time_access;    /* Last access time */
    uint32_t inode;          /* Inode number (filesystem-specific) */
};

/**
 * VFS file statistics
 */
struct vfs_stat_s {
    uint32_t dev;            /* Device ID */
    uint32_t ino;            /* Inode number */
    uint32_t mode;           /* File mode */
    uint32_t links;          /* Number of hard links */
    uint32_t uid;            /* User ID of owner */
    uint32_t gid;            /* Group ID of owner */
    uint32_t rdev;           /* Device ID (if special file) */
    uint64_t size;           /* Total size in bytes */
    uint32_t block_size;     /* Block size for filesystem I/O */
    uint32_t blocks;         /* Number of blocks allocated */
    uint32_t time_access;    /* Time of last access */
    uint32_t time_modify;    /* Time of last modification */
    uint32_t time_create;    /* Time of creation */
    uint32_t flags;          /* User defined flags */
    uint32_t generation;     /* File generation number */
    uint32_t attributes;     /* Extended attributes */
};

/**
 * VFS Cache Block Structure
 */
struct vfs_cache_block_s {
    uint32_t block_id;       /* Block identifier (usually sector number) */
    uint32_t dev_id;         /* Device identifier */
    uint8_t* data;           /* Block data */
    uint32_t size;           /* Block size */
    uint8_t dirty;           /* Whether block has been modified */
    uint32_t access_count;   /* Number of times accessed (for LRU) */
    uint32_t last_access;    /* Last access time */
    struct vfs_cache_block_s* next; /* Next in hash chain */
};

/**
 * VFS Cache Structure 
 */
struct vfs_cache_s {
    struct vfs_cache_block_s* blocks[VFS_MAX_CACHE_BLOCKS];
    uint32_t block_size;     /* Size of each block */
    uint32_t num_blocks;     /* Number of blocks in cache */
    uint32_t hits;           /* Cache hit counter */
    uint32_t misses;         /* Cache miss counter */
    uint8_t enabled;         /* Whether cache is enabled */
    uint8_t flags;           /* Cache flags */
};

/**
 * VFS Journal Entry Types
 */
enum vfs_journal_entry_type {
    VFS_JOURNAL_START_TX = 1,    /* Start of transaction */
    VFS_JOURNAL_COMMIT_TX,       /* Commit transaction */
    VFS_JOURNAL_ABORT_TX,        /* Abort transaction */
    VFS_JOURNAL_METADATA,        /* Metadata update */
    VFS_JOURNAL_DATA,            /* Data block */
    VFS_JOURNAL_CHECKPOINT       /* Checkpoint marker */
};

/**
 * VFS Journal Entry Header
 */
struct vfs_journal_entry_header {
    uint32_t magic;              /* Magic number to identify valid entries */
    uint8_t entry_type;          /* Type of journal entry */
    uint32_t size;               /* Size of this entry including header */
    uint32_t sequence;           /* Sequence number */
    uint32_t transaction_id;     /* Transaction identifier */
    uint32_t checksum;           /* Entry checksum */
};

/**
 * VFS Journal Structure
 */
struct vfs_journal_s {
    uint32_t dev_id;             /* Device ID for this journal */
    uint64_t start_offset;       /* Starting offset of journal on device */
    uint64_t size;               /* Total size of journal area */
    uint64_t used;               /* Amount of journal space used */
    uint32_t block_size;         /* Journal block size */
    uint32_t flags;              /* Journal flags */
    uint32_t current_tx;         /* Current transaction ID */
    uint8_t enabled;             /* Whether journaling is enabled */
    struct vfs_transaction_s* active_tx; /* Currently active transaction */
};

/**
 * VFS Transaction Structure
 */
struct vfs_transaction_s {
    uint32_t id;                 /* Transaction ID */
    uint32_t state;              /* Current state */
    uint32_t num_operations;     /* Number of operations in this transaction */
    void* operations;            /* List of operations in this transaction */
    struct vfs_transaction_s* next; /* Next transaction in queue */
};

/**
 * VFS Filesystem type structure - defines operations for a filesystem
 */
struct vfs_filesystem_s {
    const char* name;        /* Filesystem type name (e.g., "fat12", "ext2") */
    
    /* Filesystem operations */
    int (*mount)(vfs_mount_t* mount_point);
    int (*unmount)(vfs_mount_t* mount_point);
    
    /* File operations */
    int (*open)(vfs_mount_t* mount_point, const char* path, int flags, vfs_file_t** file);
    int (*close)(vfs_file_t* file);
    int (*read)(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read);
    int (*write)(vfs_file_t* file, const void* buffer, uint32_t size, uint32_t* bytes_written);
    int (*seek)(vfs_file_t* file, int64_t offset, int whence);
    int (*tell)(vfs_file_t* file, uint64_t* offset);
    int (*flush)(vfs_file_t* file);
    int (*stat)(vfs_mount_t* mount_point, const char* path, vfs_stat_t* stat);
    int (*truncate)(vfs_file_t* file, uint64_t size);
    int (*chmod)(vfs_mount_t* mount_point, const char* path, uint32_t mode);
    
    /* Directory operations */
    int (*opendir)(vfs_mount_t* mount_point, const char* path, vfs_file_t** dir);
    int (*readdir)(vfs_file_t* dir, vfs_dirent_t* dirent);
    int (*closedir)(vfs_file_t* dir);
    int (*mkdir)(vfs_mount_t* mount_point, const char* path, uint32_t mode);
    int (*rmdir)(vfs_mount_t* mount_point, const char* path);
    
    /* File management */
    int (*unlink)(vfs_mount_t* mount_point, const char* path);
    int (*rename)(vfs_mount_t* mount_point, const char* oldpath, const char* newpath);
    int (*link)(vfs_mount_t* mount_point, const char* oldpath, const char* newpath);
    int (*symlink)(vfs_mount_t* mount_point, const char* oldpath, const char* newpath);
    int (*readlink)(vfs_mount_t* mount_point, const char* path, char* buffer, size_t size);
    
    /* Extended attribute operations */
    int (*getxattr)(vfs_mount_t* mount_point, const char* path, const char* name, void* value, size_t size);
    int (*setxattr)(vfs_mount_t* mount_point, const char* path, const char* name, const void* value, size_t size, int flags);
    int (*listxattr)(vfs_mount_t* mount_point, const char* path, char* list, size_t size);
    int (*removexattr)(vfs_mount_t* mount_point, const char* path, const char* name);
    
    /* Filesystem info */
    int (*statfs)(vfs_mount_t* mount_point, uint64_t* total, uint64_t* free);
    int (*sync)(vfs_mount_t* mount_point);
    
    /* Journal operations */
    int (*journal_create)(vfs_mount_t* mount_point, uint64_t size, uint32_t flags);
    int (*journal_start)(vfs_mount_t* mount_point);
    int (*journal_stop)(vfs_mount_t* mount_point);
    int (*journal_begin_tx)(vfs_mount_t* mount_point);
    int (*journal_commit_tx)(vfs_mount_t* mount_point);
    int (*journal_abort_tx)(vfs_mount_t* mount_point);
    
    /* Cache operations */
    int (*cache_read)(vfs_mount_t* mount_point, uint32_t block, void* buffer);
    int (*cache_write)(vfs_mount_t* mount_point, uint32_t block, const void* buffer);
    int (*cache_flush)(vfs_mount_t* mount_point, uint32_t block);
    int (*cache_invalidate)(vfs_mount_t* mount_point, uint32_t block);
};

/**
 * VFS Mount point structure
 */
struct vfs_mount_s {
    char mount_point[VFS_MAX_PATH];        /* Path where this fs is mounted */
    char device[VFS_MAX_PATH];             /* Device path if applicable */
    vfs_filesystem_t* fs_type;             /* Filesystem type */
    void* fs_data;                         /* Filesystem-specific data */
    int flags;                             /* Mount flags */
    vfs_journal_t* journal;                /* Journal if enabled */
    vfs_cache_t* cache;                    /* Cache if enabled */
    uint8_t readonly;                      /* Whether mounted read-only */
    struct vfs_mount_s* next;              /* Next mount point in chain */
};

/**
 * VFS File handle structure
 */
struct vfs_file_s {
    vfs_mount_t* mount;                    /* Mount point this file belongs to */
    char path[VFS_MAX_PATH];               /* Path within filesystem */
    int flags;                             /* File open flags */
    uint64_t position;                     /* Current position */
    void* fs_data;                         /* Filesystem-specific data */
    uint8_t* cache_buffer;                 /* Read/write buffer for this file */
    uint32_t cache_size;                   /* Size of the cache buffer */
    uint8_t cache_dirty;                   /* Whether cache needs to be flushed */
    uint32_t mode;                         /* File mode/permissions */
    uint32_t references;                   /* Reference counter */
    struct vfs_file_s* next;               /* For open file tracking */
};

/* VFS API Function Declarations */

/**
 * Initialize the virtual filesystem
 * 
 * @return 0 on success, negative error code on failure
 */
int vfs_init(void);

/**
 * Register a filesystem type
 * 
 * @param fs_type Filesystem type structure
 * @return 0 on success, negative error code on failure
 */
int vfs_register_fs(vfs_filesystem_t* fs_type);

/**
 * Mount a filesystem to a mount point
 * 
 * @param fs_name Filesystem type name
 * @param device Device path or identifier
 * @param mount_point Path to mount point
 * @param flags Mount flags
 * @return 0 on success, negative error code on failure
 */
int vfs_mount(const char* fs_name, const char* device, const char* mount_point, int flags);

/**
 * Unmount a filesystem
 * 
 * @param mount_point Path to mount point
 * @return 0 on success, negative error code on failure
 */
int vfs_unmount(const char* mount_point);

/**
 * Open a file
 * 
 * @param path Path to file
 * @param flags Open flags (VFS_OPEN_*)
 * @param file Output file handle
 * @return 0 on success, negative error code on failure
 */
int vfs_open(const char* path, int flags, vfs_file_t** file);

/**
 * Close a file
 * 
 * @param file File handle
 * @return 0 on success, negative error code on failure
 */
int vfs_close(vfs_file_t* file);

/**
 * Read from a file
 * 
 * @param file File handle
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @param bytes_read Output number of bytes actually read
 * @return 0 on success, negative error code on failure
 */
int vfs_read(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read);

/**
 * Write to a file
 * 
 * @param file File handle
 * @param buffer Buffer to write from
 * @param size Number of bytes to write
 * @param bytes_written Output number of bytes actually written
 * @return 0 on success, negative error code on failure
 */
int vfs_write(vfs_file_t* file, const void* buffer, uint32_t size, uint32_t* bytes_written);

/**
 * Seek to a position in file
 * 
 * @param file File handle
 * @param offset Offset
 * @param whence Seek origin (VFS_SEEK_*)
 * @return 0 on success, negative error code on failure
 */
int vfs_seek(vfs_file_t* file, int64_t offset, int whence);

/**
 * Get current position in file
 * 
 * @param file File handle
 * @param offset Output offset
 * @return 0 on success, negative error code on failure
 */
int vfs_tell(vfs_file_t* file, uint64_t* offset);

/**
 * Flush file buffers
 * 
 * @param file File handle
 * @return 0 on success, negative error code on failure
 */
int vfs_flush(vfs_file_t* file);

/**
 * Truncate or extend a file to specified size
 * 
 * @param path Path to the file
 * @param size New size for the file
 * @return 0 on success, negative error code on failure
 */
int vfs_truncate(const char* path, uint64_t size);

/**
 * Change file permissions
 * 
 * @param path Path to the file
 * @param mode New mode/permissions
 * @return 0 on success, negative error code on failure
 */
int vfs_chmod(const char* path, uint32_t mode);

/**
 * Get file status
 * 
 * @param path Path to file
 * @param stat Output stat structure
 * @return 0 on success, negative error code on failure
 */
int vfs_stat(const char* path, vfs_stat_t* stat);

/**
 * Open a directory
 * 
 * @param path Path to directory
 * @param dir Output directory handle
 * @return 0 on success, negative error code on failure
 */
int vfs_opendir(const char* path, vfs_file_t** dir);

/**
 * Read directory entry
 * 
 * @param dir Directory handle
 * @param dirent Output directory entry
 * @return 0 on success, 1 on end of directory, negative error code on failure
 */
int vfs_readdir(vfs_file_t* dir, vfs_dirent_t* dirent);

/**
 * Close a directory
 * 
 * @param dir Directory handle
 * @return 0 on success, negative error code on failure
 */
int vfs_closedir(vfs_file_t* dir);

/**
 * Create a directory
 * 
 * @param path Path to directory
 * @param mode Mode/permissions for the new directory
 * @return 0 on success, negative error code on failure
 */
int vfs_mkdir(const char* path, uint32_t mode);

/**
 * Remove a directory
 * 
 * @param path Path to directory
 * @return 0 on success, negative error code on failure
 */
int vfs_rmdir(const char* path);

/**
 * Delete a file
 * 
 * @param path Path to file
 * @return 0 on success, negative error code on failure
 */
int vfs_unlink(const char* path);

/**
 * Create a hard link
 * 
 * @param oldpath Path to existing file
 * @param newpath Path for new link
 * @return 0 on success, negative error code on failure
 */
int vfs_link(const char* oldpath, const char* newpath);

/**
 * Create a symbolic link
 * 
 * @param oldpath Target path
 * @param newpath Path for new symlink
 * @return 0 on success, negative error code on failure
 */
int vfs_symlink(const char* oldpath, const char* newpath);

/**
 * Read the target of a symbolic link
 * 
 * @param path Path to symlink
 * @param buffer Buffer to store link target
 * @param size Size of buffer
 * @return 0 on success, negative error code on failure
 */
int vfs_readlink(const char* path, char* buffer, size_t size);

/**
 * Rename a file
 * 
 * @param oldpath Old path
 * @param newpath New path
 * @return 0 on success, negative error code on failure
 */
int vfs_rename(const char* oldpath, const char* newpath);

/**
 * Get filesystem statistics
 * 
 * @param path Path to any file in the filesystem
 * @param total Output total size in bytes
 * @param free Output free size in bytes
 * @return 0 on success, negative error code on failure
 */
int vfs_statfs(const char* path, uint64_t* total, uint64_t* free);

/**
 * Sync all pending changes to filesystem
 * 
 * @return 0 on success, negative error code on failure
 */
int vfs_sync(void);

/**
 * Get an extended attribute for a file
 * 
 * @param path Path to file
 * @param name Name of extended attribute
 * @param value Buffer to store value
 * @param size Size of buffer
 * @return Size of attribute value, or negative error code on failure
 */
int vfs_getxattr(const char* path, const char* name, void* value, size_t size);

/**
 * Set an extended attribute for a file
 * 
 * @param path Path to file
 * @param name Name of extended attribute
 * @param value Value to set
 * @param size Size of value
 * @param flags Flags controlling behavior
 * @return 0 on success, negative error code on failure
 */
int vfs_setxattr(const char* path, const char* name, const void* value, size_t size, int flags);

/**
 * List extended attributes for a file
 * 
 * @param path Path to file
 * @param list Buffer to store list of attribute names
 * @param size Size of buffer
 * @return Size of attribute list, or negative error code on failure
 */
int vfs_listxattr(const char* path, char* list, size_t size);

/**
 * Remove an extended attribute
 * 
 * @param path Path to file
 * @param name Name of extended attribute to remove
 * @return 0 on success, negative error code on failure
 */
int vfs_removexattr(const char* path, const char* name);

/**
 * Begin a journal transaction
 * 
 * @param mount Mount point to work on
 * @return Transaction ID on success, negative error code on failure
 */
int vfs_journal_begin_tx(vfs_mount_t* mount);

/**
 * Commit a journal transaction
 * 
 * @param mount Mount point 
 * @param tx_id Transaction ID
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_commit_tx(vfs_mount_t* mount, int tx_id);

/**
 * Abort a journal transaction
 * 
 * @param mount Mount point
 * @param tx_id Transaction ID
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_abort_tx(vfs_mount_t* mount, int tx_id);

/**
 * Create a new journal for a mounted filesystem
 * 
 * @param mount_point Path to mount point
 * @param size Size of journal in bytes
 * @param flags Journal flags (VFS_JOURNAL_*)
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_create(const char* mount_point, uint64_t size, uint32_t flags);

/**
 * Start journaling for a mounted filesystem
 * 
 * @param mount_point Path to mount point
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_start(const char* mount_point);

/**
 * Stop journaling for a mounted filesystem
 * 
 * @param mount_point Path to mount point
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_stop(const char* mount_point);

/**
 * Initialize the cache system
 * 
 * @param block_size Size of each cache block
 * @param num_blocks Number of cache blocks
 * @param flags Cache flags
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_init(uint32_t block_size, uint32_t num_blocks, uint8_t flags);

/**
 * Enable or disable caching for a mount point
 * 
 * @param mount_point Path to mount point
 * @param enable Whether to enable caching
 * @param flags Cache flags
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_control(const char* mount_point, uint8_t enable, uint8_t flags);

/**
 * Flush all dirty cache blocks to disk
 * 
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_flush_all(void);

/**
 * Invalidate all cache blocks for a specific device
 * 
 * @param device Device identifier
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_invalidate(const char* device);

/**
 * Get cache statistics
 * 
 * @param hits Output variable for cache hits
 * @param misses Output variable for cache misses
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_get_stats(uint32_t* hits, uint32_t* misses);

/**
 * Repair filesystem on specified mount point
 * 
 * @param mount_point Path to mount point
 * @param flags Repair options
 * @return 0 on success, negative error code on failure
 */
int vfs_fsck(const char* mount_point, uint32_t flags);

/**
 * Format a device with specified filesystem
 * 
 * @param fs_name Filesystem type name
 * @param device Device to format
 * @param label Volume label
 * @param flags Format options
 * @return 0 on success, negative error code on failure
 */
int vfs_format(const char* fs_name, const char* device, const char* label, uint32_t flags);

/**
 * Get error message for error code
 * 
 * @param error Error code
 * @return String describing the error
 */
const char* vfs_strerror(int error);

#endif /* VFS_H */