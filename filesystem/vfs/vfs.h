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
#define VFS_ERR_UNKNOWN        -11

/* File Types */
#define VFS_TYPE_FILE          1
#define VFS_TYPE_DIRECTORY     2
#define VFS_TYPE_SYMLINK       3
#define VFS_TYPE_DEVICE        4

/* File Attributes */
#define VFS_ATTR_READ          0x01
#define VFS_ATTR_WRITE         0x02
#define VFS_ATTR_EXECUTE       0x04
#define VFS_ATTR_HIDDEN        0x08
#define VFS_ATTR_SYSTEM        0x10
#define VFS_ATTR_ARCHIVE       0x20

/* File Open Modes */
#define VFS_OPEN_READ          0x01
#define VFS_OPEN_WRITE         0x02
#define VFS_OPEN_APPEND        0x04
#define VFS_OPEN_CREATE        0x08
#define VFS_OPEN_TRUNCATE      0x10

/* Seek Origins */
#define VFS_SEEK_SET           0
#define VFS_SEEK_CUR           1
#define VFS_SEEK_END           2

/* Maximum path length */
#define VFS_MAX_PATH           256

/* Maximum filename length */
#define VFS_MAX_FILENAME       128

/* Maximum mounted filesystems */
#define VFS_MAX_MOUNTS         8

/* Forward declarations */
typedef struct vfs_file_s vfs_file_t;
typedef struct vfs_dirent_s vfs_dirent_t;
typedef struct vfs_stat_s vfs_stat_t;
typedef struct vfs_filesystem_s vfs_filesystem_t;
typedef struct vfs_mount_s vfs_mount_t;

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
    
    /* Directory operations */
    int (*opendir)(vfs_mount_t* mount_point, const char* path, vfs_file_t** dir);
    int (*readdir)(vfs_file_t* dir, vfs_dirent_t* dirent);
    int (*closedir)(vfs_file_t* dir);
    int (*mkdir)(vfs_mount_t* mount_point, const char* path);
    int (*rmdir)(vfs_mount_t* mount_point, const char* path);
    
    /* File management */
    int (*unlink)(vfs_mount_t* mount_point, const char* path);
    int (*rename)(vfs_mount_t* mount_point, const char* oldpath, const char* newpath);
    
    /* Filesystem info */
    int (*statfs)(vfs_mount_t* mount_point, uint64_t* total, uint64_t* free);
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
 * @return 0 on success, negative error code on failure
 */
int vfs_mkdir(const char* path);

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

#endif /* VFS_H */