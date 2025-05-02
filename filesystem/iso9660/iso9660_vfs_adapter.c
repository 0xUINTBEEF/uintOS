#include "../vfs/vfs.h"
#include "iso9660.h"
#include "../../kernel/logging/log.h"
#include <string.h>

/* ISO9660 implementation for VFS */

/* Convert ISO9660 error codes to VFS error codes */
static int iso9660_to_vfs_error(int iso_error) {
    switch (iso_error) {
        case ISO9660_SUCCESS:       return VFS_SUCCESS;
        case ISO9660_ERR_NOT_FOUND: return VFS_ERR_NOT_FOUND;
        case ISO9660_ERR_BAD_FORMAT: return VFS_ERR_UNKNOWN;
        case ISO9660_ERR_IO_ERROR:  return VFS_ERR_IO_ERROR;
        case ISO9660_ERR_INVALID_ARG: return VFS_ERR_INVALID_ARG;
        default:                    return VFS_ERR_UNKNOWN;
    }
}

/* Convert ISO9660 attributes to VFS attributes */
static uint32_t iso9660_to_vfs_attr(uint8_t iso_attr) {
    uint32_t vfs_attr = VFS_ATTR_READ; // ISO9660 is read-only
    
    if (iso_attr & ISO9660_ATTR_DIRECTORY) {
        vfs_attr |= VFS_ATTR_EXECUTE; // Directories are executable
    }
    
    if (iso_attr & ISO9660_ATTR_HIDDEN) {
        vfs_attr |= VFS_ATTR_HIDDEN;
    }
    
    return vfs_attr;
}

/* Helper to normalize paths */
static void normalize_iso9660_path(const char* vfs_path, char* iso_path, int max_len) {
    /* Skip initial slash if present */
    if (vfs_path[0] == '/') {
        strncpy(iso_path, vfs_path, max_len - 1);
    } else {
        /* Add a leading slash if not present */
        iso_path[0] = '/';
        strncpy(iso_path + 1, vfs_path, max_len - 2);
    }
    iso_path[max_len - 1] = '\0';
    
    /* Convert empty path to root directory */
    if (iso_path[0] == '\0' || (iso_path[0] == '/' && iso_path[1] == '\0')) {
        iso_path[0] = '/';
        iso_path[1] = '\0';
    }
}

/* Structure to keep directory reading state */
typedef struct {
    iso9660_file_entry_t entries[64]; /* Cache of directory entries */
    int num_entries;                /* Number of entries in the cache */
    int current_index;              /* Current position in the cache */
    char path[VFS_MAX_PATH];        /* Directory path */
} iso9660_dir_data_t;

/* Structure to keep file reading state */
typedef struct {
    char filepath[VFS_MAX_PATH];    /* File path */
    uint32_t size;                 /* Size of the file */
    uint32_t position;             /* Current position in file */
} iso9660_file_data_t;

/* Mount function for ISO9660 */
static int iso9660_vfs_mount(vfs_mount_t* mount) {
    log_info("ISO9660-VFS", "Mounting ISO9660 filesystem on %s", mount->mount_point);
    
    /* Initialize ISO9660 on specified device */
    int result = iso9660_init(mount->device[0] ? mount->device : "default_device");
    if (result != ISO9660_SUCCESS) {
        log_error("ISO9660-VFS", "Failed to initialize ISO9660 filesystem: %d", result);
        return iso9660_to_vfs_error(result);
    }
    
    /* No special mount data needed for our ISO9660 implementation */
    mount->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Unmount function for ISO9660 */
static int iso9660_vfs_unmount(vfs_mount_t* mount) {
    log_info("ISO9660-VFS", "Unmounting ISO9660 filesystem from %s", mount->mount_point);
    
    /* No special unmount procedure needed for our ISO9660 implementation */
    return VFS_SUCCESS;
}

/* Open function for ISO9660 */
static int iso9660_vfs_open(vfs_mount_t* mount, const char* path, int flags, vfs_file_t** file) {
    char iso_path[VFS_MAX_PATH];
    
    log_debug("ISO9660-VFS", "Opening %s with flags %x", path, flags);
    
    /* ISO9660 is read-only, so check for write operations */
    if (flags & (VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE)) {
        log_error("ISO9660-VFS", "Cannot write to ISO9660 filesystem (read-only)");
        return VFS_ERR_READONLY;
    }
    
    /* Normalize path for ISO9660 */
    normalize_iso9660_path(path, iso_path, VFS_MAX_PATH);
    
    /* Check if the file exists */
    int exists = iso9660_file_exists(iso_path);
    if (!exists) {
        log_error("ISO9660-VFS", "File not found: %s", iso_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    int file_size = iso9660_get_file_size(iso_path);
    if (file_size < 0) {
        log_error("ISO9660-VFS", "Error getting file size: %s (%d)", iso_path, file_size);
        return iso9660_to_vfs_error(file_size);
    }
    
    /* Create ISO9660 file data structure */
    iso9660_file_data_t* file_data = (iso9660_file_data_t*)malloc(sizeof(iso9660_file_data_t));
    if (!file_data) {
        log_error("ISO9660-VFS", "Failed to allocate memory for file handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize file data */
    strncpy(file_data->filepath, iso_path, VFS_MAX_PATH - 1);
    file_data->filepath[VFS_MAX_PATH - 1] = '\0';
    file_data->size = file_size;
    file_data->position = 0;
    
    /* Store file data in the file handle */
    (*file)->fs_data = file_data;
    
    log_debug("ISO9660-VFS", "File opened successfully: %s (size: %d bytes)", iso_path, file_size);
    
    return VFS_SUCCESS;
}

/* Close function for ISO9660 */
static int iso9660_vfs_close(vfs_file_t* file) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the file data */
    free(file->fs_data);
    file->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Read function for ISO9660 */
static int iso9660_vfs_read(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    iso9660_file_data_t* file_data = (iso9660_file_data_t*)file->fs_data;
    
    /* Check if we're at the end of the file */
    if (file_data->position >= file_data->size) {
        *bytes_read = 0;
        return VFS_SUCCESS;
    }
    
    /* Adjust size to not read past the end of the file */
    if (file_data->position + size > file_data->size) {
        size = file_data->size - file_data->position;
    }
    
    /* Allocate a buffer for the entire file or a portion of it */
    char* read_buffer = NULL;
    int result;
    
    if (file_data->position == 0 && size == file_data->size) {
        /* Reading the entire file directly into the caller's buffer */
        result = iso9660_read_file(file_data->filepath, buffer, size);
    } else {
        /* Reading a portion of the file, we need to handle seeking */
        read_buffer = (char*)malloc(file_data->size);
        if (!read_buffer) {
            log_error("ISO9660-VFS", "Failed to allocate memory for file read");
            return VFS_ERR_NO_SPACE;
        }
        
        result = iso9660_read_file(file_data->filepath, read_buffer, file_data->size);
        if (result >= 0) {
            /* Copy the requested portion to the user's buffer */
            memcpy(buffer, read_buffer + file_data->position, size);
        }
        
        free(read_buffer);
    }
    
    if (result < 0) {
        log_error("ISO9660-VFS", "Error reading file: %s (%d)", file_data->filepath, result);
        return iso9660_to_vfs_error(result);
    }
    
    /* Update position and return read size */
    file_data->position += size;
    *bytes_read = size;
    
    return VFS_SUCCESS;
}

/* Seek function for ISO9660 */
static int iso9660_vfs_seek(vfs_file_t* file, int64_t offset, int whence) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    iso9660_file_data_t* file_data = (iso9660_file_data_t*)file->fs_data;
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

/* Tell function for ISO9660 */
static int iso9660_vfs_tell(vfs_file_t* file, uint64_t* offset) {
    if (!file || !file->fs_data || !offset) {
        return VFS_ERR_INVALID_ARG;
    }
    
    iso9660_file_data_t* file_data = (iso9660_file_data_t*)file->fs_data;
    
    *offset = file_data->position;
    
    return VFS_SUCCESS;
}

/* Stat function for ISO9660 */
static int iso9660_vfs_stat(vfs_mount_t* mount, const char* path, vfs_stat_t* stat) {
    char iso_path[VFS_MAX_PATH];
    normalize_iso9660_path(path, iso_path, VFS_MAX_PATH);
    
    /* Check if the file/directory exists */
    int exists = iso9660_file_exists(iso_path);
    if (!exists) {
        log_error("ISO9660-VFS", "File not found: %s", iso_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    int file_size = iso9660_get_file_size(iso_path);
    if (file_size < 0) {
        log_error("ISO9660-VFS", "Error getting file stats: %s (%d)", iso_path, file_size);
        return iso9660_to_vfs_error(file_size);
    }
    
    /* Fill in the stat structure */
    stat->size = file_size;
    
    /* For our simplified implementation, we don't have all ISO9660 details */
    stat->dev = 0;
    stat->ino = 0;
    stat->mode = 0444; /* read-only mode */
    stat->links = 1;
    stat->uid = 0;
    stat->gid = 0;
    stat->rdev = 0;
    stat->block_size = ISO9660_SECTOR_SIZE;
    stat->blocks = (file_size + ISO9660_SECTOR_SIZE - 1) / ISO9660_SECTOR_SIZE;
    stat->time_access = 0;
    stat->time_modify = 0;
    stat->time_create = 0;
    
    return VFS_SUCCESS;
}

/* Opendir function for ISO9660 */
static int iso9660_vfs_opendir(vfs_mount_t* mount, const char* path, vfs_file_t** dir) {
    char iso_path[VFS_MAX_PATH];
    normalize_iso9660_path(path, iso_path, VFS_MAX_PATH);
    
    log_debug("ISO9660-VFS", "Opening directory: %s", iso_path);
    
    /* Create directory data structure */
    iso9660_dir_data_t* dir_data = (iso9660_dir_data_t*)malloc(sizeof(iso9660_dir_data_t));
    if (!dir_data) {
        log_error("ISO9660-VFS", "Failed to allocate memory for directory handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize directory data */
    strncpy(dir_data->path, iso_path, VFS_MAX_PATH - 1);
    dir_data->path[VFS_MAX_PATH - 1] = '\0';
    
    /* Get directory entries */
    dir_data->num_entries = iso9660_list_directory(iso_path, dir_data->entries, 64);
    
    if (dir_data->num_entries < 0) {
        log_error("ISO9660-VFS", "Error listing directory: %s (%d)", iso_path, dir_data->num_entries);
        free(dir_data);
        return iso9660_to_vfs_error(dir_data->num_entries);
    }
    
    dir_data->current_index = 0;
    
    /* Store directory data in the file handle */
    (*dir)->fs_data = dir_data;
    
    log_debug("ISO9660-VFS", "Directory opened successfully: %s (entries: %d)", iso_path, dir_data->num_entries);
    
    return VFS_SUCCESS;
}

/* Readdir function for ISO9660 */
static int iso9660_vfs_readdir(vfs_file_t* dir, vfs_dirent_t* dirent) {
    if (!dir || !dir->fs_data || !dirent) {
        return VFS_ERR_INVALID_ARG;
    }
    
    iso9660_dir_data_t* dir_data = (iso9660_dir_data_t*)dir->fs_data;
    
    /* Check if we've read all entries */
    if (dir_data->current_index >= dir_data->num_entries) {
        return 1; /* End of directory */
    }
    
    /* Copy entry data */
    iso9660_file_entry_t* entry = &dir_data->entries[dir_data->current_index];
    strncpy(dirent->name, entry->name, VFS_MAX_FILENAME - 1);
    dirent->name[VFS_MAX_FILENAME - 1] = '\0';
    
    dirent->size = entry->size;
    dirent->attributes = iso9660_to_vfs_attr(entry->attributes);
    
    /* Determine file type */
    dirent->type = (entry->attributes & ISO9660_ATTR_DIRECTORY) ? 
                   VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
    
    /* Convert ISO9660 date format to timestamp 
     * Format: YY MM DD HH MM SS TZ */
    uint32_t timestamp = 
        ((uint32_t)entry->recording_date[0] + 100) << 25 | // Year (since 1900)
        ((uint32_t)entry->recording_date[1]) << 21 |       // Month
        ((uint32_t)entry->recording_date[2]) << 16 |       // Day
        ((uint32_t)entry->recording_date[3]) << 11 |       // Hour
        ((uint32_t)entry->recording_date[4]) << 5 |        // Minute
        ((uint32_t)entry->recording_date[5]);              // Second
    
    /* Set all timestamps to the same value since ISO9660 only has one */
    dirent->time_create = timestamp;
    dirent->time_modify = timestamp;
    dirent->time_access = timestamp;
    
    /* Move to next entry */
    dir_data->current_index++;
    
    return VFS_SUCCESS;
}

/* Closedir function for ISO9660 */
static int iso9660_vfs_closedir(vfs_file_t* dir) {
    if (!dir || !dir->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the directory data */
    free(dir->fs_data);
    dir->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Create the ISO9660 VFS filesystem type structure */
vfs_filesystem_t iso9660_vfs_fs = {
    .name = "iso9660",
    .mount = iso9660_vfs_mount,
    .unmount = iso9660_vfs_unmount,
    .open = iso9660_vfs_open,
    .close = iso9660_vfs_close,
    .read = iso9660_vfs_read,
    .write = NULL, /* ISO9660 is read-only */
    .seek = iso9660_vfs_seek,
    .tell = iso9660_vfs_tell,
    .flush = NULL, /* Not needed for ISO9660 */
    .stat = iso9660_vfs_stat,
    .opendir = iso9660_vfs_opendir,
    .readdir = iso9660_vfs_readdir,
    .closedir = iso9660_vfs_closedir,
    .mkdir = NULL, /* ISO9660 is read-only */
    .rmdir = NULL, /* ISO9660 is read-only */
    .unlink = NULL, /* ISO9660 is read-only */
    .rename = NULL, /* ISO9660 is read-only */
    .statfs = NULL, /* ISO9660 statfs not implemented yet */
};

/* Register ISO9660 with the VFS */
void register_iso9660_with_vfs(void) {
    log_info("ISO9660-VFS", "Registering ISO9660 filesystem with VFS");
    vfs_register_fs(&iso9660_vfs_fs);
}