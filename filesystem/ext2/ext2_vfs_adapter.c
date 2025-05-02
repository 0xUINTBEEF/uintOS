#include "../vfs/vfs.h"
#include "ext2.h"
#include "../../kernel/logging/log.h"
#include <string.h>

/* EXT2 implementation for VFS */

/* Convert EXT2 error codes to VFS error codes */
static int ext2_to_vfs_error(int ext2_error) {
    switch (ext2_error) {
        case EXT2_SUCCESS:       return VFS_SUCCESS;
        case EXT2_ERR_NOT_FOUND: return VFS_ERR_NOT_FOUND;
        case EXT2_ERR_NO_SPACE:  return VFS_ERR_NO_SPACE;
        case EXT2_ERR_BAD_FORMAT: return VFS_ERR_UNKNOWN;
        case EXT2_ERR_IO_ERROR:  return VFS_ERR_IO_ERROR;
        case EXT2_ERR_INVALID_ARG: return VFS_ERR_INVALID_ARG;
        case EXT2_ERR_PERMISSION: return VFS_ERR_READONLY;
        case EXT2_ERR_CORRUPTED: return VFS_ERR_UNKNOWN;
        default:                 return VFS_ERR_UNKNOWN;
    }
}

/* Convert EXT2 mode to VFS attributes */
static uint32_t ext2_to_vfs_attr(uint16_t ext2_mode) {
    uint32_t vfs_attr = 0;
    
    // Check read/write/execute permissions for owner (user)
    if (ext2_mode & EXT2_S_IRUSR) vfs_attr |= VFS_ATTR_READ;
    if (ext2_mode & EXT2_S_IWUSR) vfs_attr |= VFS_ATTR_WRITE;
    if (ext2_mode & EXT2_S_IXUSR) vfs_attr |= VFS_ATTR_EXECUTE;
    
    // Check file type
    if ((ext2_mode & EXT2_S_IFMT) == EXT2_S_IFREG) {
        // Regular file
    } else if ((ext2_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
        // Directory - special attribute handling
        vfs_attr |= VFS_ATTR_EXECUTE; // Directories are executable in UNIX-like systems
    }
    
    return vfs_attr;
}

/* Convert EXT2 mode to VFS file type */
static uint32_t ext2_to_vfs_type(uint16_t ext2_mode) {
    switch (ext2_mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG: return VFS_TYPE_FILE;
        case EXT2_S_IFDIR: return VFS_TYPE_DIRECTORY;
        case EXT2_S_IFLNK: return VFS_TYPE_SYMLINK;
        case EXT2_S_IFCHR:
        case EXT2_S_IFBLK:
        case EXT2_S_IFIFO:
        case EXT2_S_IFSOCK:
            return VFS_TYPE_DEVICE;
        default:
            return VFS_TYPE_FILE;
    }
}

/* Helper to normalize paths */
static void normalize_ext2_path(const char* vfs_path, char* ext2_path, int max_len) {
    /* Skip initial slash if present */
    if (vfs_path[0] == '/') {
        strncpy(ext2_path, vfs_path, max_len - 1);
    } else {
        /* Add a leading slash if not present */
        ext2_path[0] = '/';
        strncpy(ext2_path + 1, vfs_path, max_len - 2);
    }
    ext2_path[max_len - 1] = '\0';
    
    /* Convert empty path to root */
    if (ext2_path[0] == '/' && ext2_path[1] == '\0') {
        // This is already root, leave as is
    }
}

/* Structure to keep directory reading state */
typedef struct {
    ext2_file_entry_t entries[64]; /* Cache of directory entries */
    int num_entries;               /* Number of entries in the cache */
    int current_index;             /* Current position in the cache */
    char path[VFS_MAX_PATH];       /* Directory path */
} ext2_dir_data_t;

/* Structure to keep file reading state */
typedef struct {
    char filepath[VFS_MAX_PATH];   /* File path */
    uint32_t size;                /* Size of the file */
    uint32_t position;            /* Current position in file */
} ext2_file_data_t;

/* Mount function for EXT2 */
static int ext2_vfs_mount(vfs_mount_t* mount) {
    log_info("EXT2-VFS", "Mounting EXT2 filesystem on %s", mount->mount_point);
    
    /* Initialize EXT2 on specified device */
    int result = ext2_init(mount->device[0] ? mount->device : "default_device");
    if (result != EXT2_SUCCESS) {
        log_error("EXT2-VFS", "Failed to initialize EXT2 filesystem: %d", result);
        return ext2_to_vfs_error(result);
    }
    
    /* No special mount data needed for our EXT2 implementation */
    mount->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Unmount function for EXT2 */
static int ext2_vfs_unmount(vfs_mount_t* mount) {
    log_info("EXT2-VFS", "Unmounting EXT2 filesystem from %s", mount->mount_point);
    
    /* No special unmount procedure needed for our EXT2 implementation */
    return VFS_SUCCESS;
}

/* Open function for EXT2 */
static int ext2_vfs_open(vfs_mount_t* mount, const char* path, int flags, vfs_file_t** file) {
    char ext2_path[VFS_MAX_PATH];
    
    log_debug("EXT2-VFS", "Opening %s with flags %x", path, flags);
    
    /* Normalize the path for EXT2 */
    normalize_ext2_path(path, ext2_path, VFS_MAX_PATH);
    
    /* Check if file exists */
    int exists = ext2_file_exists(ext2_path);
    
    /* Handle file creation */
    if (!exists && (flags & VFS_OPEN_CREATE)) {
        log_debug("EXT2-VFS", "Creating new file: %s", ext2_path);
        
        /* Create an empty file */
        int result = ext2_write_file(ext2_path, "", 0, 1); /* 1 = create flag */
        if (result < 0) {
            log_error("EXT2-VFS", "Failed to create file: %s (%d)", ext2_path, result);
            return ext2_to_vfs_error(result);
        }
    } else if (!exists) {
        log_error("EXT2-VFS", "File not found: %s", ext2_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    int file_size = ext2_get_file_size(ext2_path);
    if (file_size < 0) {
        log_error("EXT2-VFS", "Error getting file size: %s (%d)", ext2_path, file_size);
        return ext2_to_vfs_error(file_size);
    }
    
    /* Create EXT2 file data structure */
    ext2_file_data_t* file_data = (ext2_file_data_t*)malloc(sizeof(ext2_file_data_t));
    if (!file_data) {
        log_error("EXT2-VFS", "Failed to allocate memory for file handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize file data */
    strncpy(file_data->filepath, ext2_path, VFS_MAX_PATH - 1);
    file_data->filepath[VFS_MAX_PATH - 1] = '\0';
    file_data->size = file_size;
    file_data->position = 0;
    
    /* If truncate flag is set, truncate the file */
    if (flags & VFS_OPEN_TRUNCATE) {
        log_debug("EXT2-VFS", "Truncating file: %s", ext2_path);
        
        /* Truncate by writing empty string */
        int result = ext2_write_file(ext2_path, "", 0, 2); /* 2 = truncate flag */
        if (result < 0) {
            free(file_data);
            log_error("EXT2-VFS", "Failed to truncate file: %s (%d)", ext2_path, result);
            return ext2_to_vfs_error(result);
        }
        
        file_data->size = 0;
    }
    
    /* Store file data in the file handle */
    (*file)->fs_data = file_data;
    
    log_debug("EXT2-VFS", "File opened successfully: %s (size: %d bytes)", ext2_path, file_size);
    
    return VFS_SUCCESS;
}

/* Close function for EXT2 */
static int ext2_vfs_close(vfs_file_t* file) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the file data */
    free(file->fs_data);
    file->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Read function for EXT2 */
static int ext2_vfs_read(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    ext2_file_data_t* file_data = (ext2_file_data_t*)file->fs_data;
    
    /* Check if we're at the end of the file */
    if (file_data->position >= file_data->size) {
        *bytes_read = 0;
        return VFS_SUCCESS;
    }
    
    /* Adjust size to not read past the end of the file */
    if (file_data->position + size > file_data->size) {
        size = file_data->size - file_data->position;
    }
    
    /* Allocate a buffer for the entire file */
    char* file_buffer = (char*)malloc(file_data->size);
    if (!file_buffer) {
        log_error("EXT2-VFS", "Failed to allocate memory for file read");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Read the entire file */
    int result = ext2_read_file(file_data->filepath, file_buffer, file_data->size);
    if (result < 0) {
        free(file_buffer);
        log_error("EXT2-VFS", "Error reading file: %s (%d)", file_data->filepath, result);
        return ext2_to_vfs_error(result);
    }
    
    /* Copy the requested portion to the user's buffer */
    memcpy(buffer, file_buffer + file_data->position, size);
    
    /* Free the temporary buffer */
    free(file_buffer);
    
    /* Update position and return read size */
    file_data->position += size;
    *bytes_read = size;
    
    return VFS_SUCCESS;
}

/* Write function for EXT2 */
static int ext2_vfs_write(vfs_file_t* file, const void* buffer, uint32_t size, uint32_t* bytes_written) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    ext2_file_data_t* file_data = (ext2_file_data_t*)file->fs_data;
    
    /* If we have existing content and the position is not at the start, we need
     * to read the file first, modify it, and write it back */
    if ((file_data->size > 0 && file_data->position > 0) || 
        (file->flags & VFS_OPEN_APPEND)) {
        
        /* If append mode, position at the end */
        if (file->flags & VFS_OPEN_APPEND) {
            file_data->position = file_data->size;
        }
        
        /* Allocate a buffer for the entire new file content */
        uint32_t new_size = file_data->position + size;
        if (new_size < file_data->size) {
            new_size = file_data->size;
        }
        
        char* new_content = (char*)malloc(new_size);
        if (!new_content) {
            log_error("EXT2-VFS", "Failed to allocate memory for file write");
            return VFS_ERR_NO_SPACE;
        }
        
        /* If there's existing content, read it */
        if (file_data->size > 0) {
            int read_result = ext2_read_file(file_data->filepath, new_content, file_data->size);
            if (read_result < 0) {
                free(new_content);
                log_error("EXT2-VFS", "Error reading existing file content: %s (%d)", file_data->filepath, read_result);
                return ext2_to_vfs_error(read_result);
            }
        }
        
        /* Copy new data at the current position */
        memcpy(new_content + file_data->position, buffer, size);
        
        /* Write the entire file back */
        int write_result = ext2_write_file(file_data->filepath, new_content, new_size, 0);
        free(new_content);
        
        if (write_result < 0) {
            log_error("EXT2-VFS", "Error writing to file: %s (%d)", file_data->filepath, write_result);
            return ext2_to_vfs_error(write_result);
        }
        
        /* Update file size and position */
        file_data->size = new_size;
        file_data->position += size;
        
        /* Return success */
        *bytes_written = size;
        return VFS_SUCCESS;
    } 
    else {
        /* Simple case: writing to the beginning of the file or an empty file */
        int write_result = ext2_write_file(file_data->filepath, buffer, size, 0);
        
        if (write_result < 0) {
            log_error("EXT2-VFS", "Error writing to file: %s (%d)", file_data->filepath, write_result);
            return ext2_to_vfs_error(write_result);
        }
        
        /* Update file size and position */
        file_data->size = size;
        file_data->position = size;
        
        /* Return success */
        *bytes_written = size;
        return VFS_SUCCESS;
    }
}

/* Seek function for EXT2 */
static int ext2_vfs_seek(vfs_file_t* file, int64_t offset, int whence) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    ext2_file_data_t* file_data = (ext2_file_data_t*)file->fs_data;
    int64_t new_position = 0;
    
    /* Calculate new position based on seek parameters */
    if (whence == VFS_SEEK_SET) {
        new_position = offset;
    } else if (whence == VFS_SEEK_CUR) {
        new_position = file_data->position + offset;
    } else if (whence == VFS_SEEK_END) {
        new_position = file_data->size + offset;
    } else {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Check boundaries */
    if (new_position < 0) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Update position */
    file_data->position = new_position;
    
    return VFS_SUCCESS;
}

/* Tell function for EXT2 */
static int ext2_vfs_tell(vfs_file_t* file, uint64_t* offset) {
    if (!file || !file->fs_data || !offset) {
        return VFS_ERR_INVALID_ARG;
    }
    
    ext2_file_data_t* file_data = (ext2_file_data_t*)file->fs_data;
    
    *offset = file_data->position;
    
    return VFS_SUCCESS;
}

/* Stat function for EXT2 */
static int ext2_vfs_stat(vfs_mount_t* mount, const char* path, vfs_stat_t* stat) {
    char ext2_path[VFS_MAX_PATH];
    normalize_ext2_path(path, ext2_path, VFS_MAX_PATH);
    
    /* Check if the file/directory exists */
    int exists = ext2_file_exists(ext2_path);
    if (!exists) {
        log_error("EXT2-VFS", "File not found: %s", ext2_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    int file_size = ext2_get_file_size(ext2_path);
    if (file_size < 0) {
        log_error("EXT2-VFS", "Error getting file stats: %s (%d)", ext2_path, file_size);
        return ext2_to_vfs_error(file_size);
    }
    
    /* Fill in the stat structure */
    stat->size = file_size;
    
    /* For our simplified implementation, we don't have all the EXT2 details */
    stat->dev = 0;
    stat->ino = 0;
    stat->mode = 0;
    stat->links = 1;
    stat->uid = 0;
    stat->gid = 0;
    stat->rdev = 0;
    stat->block_size = 1024; /* Default EXT2 block size */
    stat->blocks = (file_size + 1023) / 1024;
    stat->time_access = 0;
    stat->time_modify = 0;
    stat->time_create = 0;
    
    return VFS_SUCCESS;
}

/* Opendir function for EXT2 */
static int ext2_vfs_opendir(vfs_mount_t* mount, const char* path, vfs_file_t** dir) {
    char ext2_path[VFS_MAX_PATH];
    normalize_ext2_path(path, ext2_path, VFS_MAX_PATH);
    
    log_debug("EXT2-VFS", "Opening directory: %s", ext2_path);
    
    /* Create directory data structure */
    ext2_dir_data_t* dir_data = (ext2_dir_data_t*)malloc(sizeof(ext2_dir_data_t));
    if (!dir_data) {
        log_error("EXT2-VFS", "Failed to allocate memory for directory handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize directory data */
    strncpy(dir_data->path, ext2_path, VFS_MAX_PATH - 1);
    dir_data->path[VFS_MAX_PATH - 1] = '\0';
    
    /* Get directory entries - caching them upfront */
    dir_data->num_entries = ext2_list_directory(ext2_path, dir_data->entries, 64);
    
    if (dir_data->num_entries < 0) {
        log_error("EXT2-VFS", "Error listing directory: %s (%d)", ext2_path, dir_data->num_entries);
        free(dir_data);
        return ext2_to_vfs_error(dir_data->num_entries);
    }
    
    dir_data->current_index = 0;
    
    /* Store directory data in the file handle */
    (*dir)->fs_data = dir_data;
    
    log_debug("EXT2-VFS", "Directory opened successfully: %s (entries: %d)", ext2_path, dir_data->num_entries);
    
    return VFS_SUCCESS;
}

/* Readdir function for EXT2 */
static int ext2_vfs_readdir(vfs_file_t* dir, vfs_dirent_t* dirent) {
    if (!dir || !dir->fs_data || !dirent) {
        return VFS_ERR_INVALID_ARG;
    }
    
    ext2_dir_data_t* dir_data = (ext2_dir_data_t*)dir->fs_data;
    
    /* Check if we've read all entries */
    if (dir_data->current_index >= dir_data->num_entries) {
        return 1; /* End of directory */
    }
    
    /* Copy entry data */
    ext2_file_entry_t* entry = &dir_data->entries[dir_data->current_index];
    strncpy(dirent->name, entry->name, VFS_MAX_FILENAME - 1);
    dirent->name[VFS_MAX_FILENAME - 1] = '\0';
    
    dirent->size = entry->size;
    dirent->attributes = ext2_to_vfs_attr(entry->mode);
    dirent->type = ext2_to_vfs_type(entry->mode);
    
    /* Copy timestamps */
    dirent->time_create = entry->ctime;
    dirent->time_modify = entry->mtime;
    dirent->time_access = entry->atime;
    
    /* Move to next entry */
    dir_data->current_index++;
    
    return VFS_SUCCESS;
}

/* Closedir function for EXT2 */
static int ext2_vfs_closedir(vfs_file_t* dir) {
    if (!dir || !dir->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the directory data */
    free(dir->fs_data);
    dir->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Mkdir function for EXT2 */
static int ext2_vfs_mkdir(vfs_mount_t* mount, const char* path) {
    char ext2_path[VFS_MAX_PATH];
    normalize_ext2_path(path, ext2_path, VFS_MAX_PATH);
    
    log_debug("EXT2-VFS", "Creating directory: %s", ext2_path);
    
    /* Create the directory with default permissions */
    int result = ext2_mkdir(ext2_path, 0755); /* rwxr-xr-x */
    
    if (result != EXT2_SUCCESS) {
        log_error("EXT2-VFS", "Error creating directory: %s (%d)", ext2_path, result);
        return ext2_to_vfs_error(result);
    }
    
    log_debug("EXT2-VFS", "Directory created successfully: %s", ext2_path);
    return VFS_SUCCESS;
}

/* Rmdir function for EXT2 */
static int ext2_vfs_rmdir(vfs_mount_t* mount, const char* path) {
    char ext2_path[VFS_MAX_PATH];
    normalize_ext2_path(path, ext2_path, VFS_MAX_PATH);
    
    log_debug("EXT2-VFS", "Removing directory: %s", ext2_path);
    
    /* Remove the directory */
    int result = ext2_remove(ext2_path);
    
    if (result != EXT2_SUCCESS) {
        log_error("EXT2-VFS", "Error removing directory: %s (%d)", ext2_path, result);
        return ext2_to_vfs_error(result);
    }
    
    log_debug("EXT2-VFS", "Directory removed successfully: %s", ext2_path);
    return VFS_SUCCESS;
}

/* Unlink function for EXT2 */
static int ext2_vfs_unlink(vfs_mount_t* mount, const char* path) {
    char ext2_path[VFS_MAX_PATH];
    normalize_ext2_path(path, ext2_path, VFS_MAX_PATH);
    
    log_debug("EXT2-VFS", "Deleting file: %s", ext2_path);
    
    /* Delete the file */
    int result = ext2_remove(ext2_path);
    
    if (result != EXT2_SUCCESS) {
        log_error("EXT2-VFS", "Error deleting file: %s (%d)", ext2_path, result);
        return ext2_to_vfs_error(result);
    }
    
    log_debug("EXT2-VFS", "File deleted successfully: %s", ext2_path);
    return VFS_SUCCESS;
}

/* Create the EXT2 VFS filesystem type structure */
vfs_filesystem_t ext2_vfs_fs = {
    .name = "ext2",
    .mount = ext2_vfs_mount,
    .unmount = ext2_vfs_unmount,
    .open = ext2_vfs_open,
    .close = ext2_vfs_close,
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .seek = ext2_vfs_seek,
    .tell = ext2_vfs_tell,
    .flush = NULL, /* Not needed for our EXT2 implementation */
    .stat = ext2_vfs_stat,
    .opendir = ext2_vfs_opendir,
    .readdir = ext2_vfs_readdir,
    .closedir = ext2_vfs_closedir,
    .mkdir = ext2_vfs_mkdir,
    .rmdir = ext2_vfs_rmdir,
    .unlink = ext2_vfs_unlink,
    .rename = NULL, /* EXT2 rename not implemented yet */
    .statfs = NULL, /* EXT2 statfs not implemented yet */
};

/* Register EXT2 with the VFS */
void register_ext2_with_vfs(void) {
    log_info("EXT2-VFS", "Registering EXT2 filesystem with VFS");
    vfs_register_fs(&ext2_vfs_fs);
}