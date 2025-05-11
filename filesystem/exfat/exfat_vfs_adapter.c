#include "exfat.h"
#include "../vfs/vfs.h"
#include "../../kernel/logging/log.h"
#include <string.h>
#include <stdlib.h>

/* exFAT implementation for VFS */

/* Convert exFAT error codes to VFS error codes */
static int exfat_to_vfs_error(int exfat_error) {
    switch (exfat_error) {
        case EXFAT_SUCCESS:       return VFS_SUCCESS;
        case EXFAT_ERR_NOT_FOUND: return VFS_ERR_NOT_FOUND;
        case EXFAT_ERR_EXISTS:    return VFS_ERR_EXISTS;
        case EXFAT_ERR_NO_SPACE:  return VFS_ERR_NO_SPACE;
        case EXFAT_ERR_BAD_FORMAT: return VFS_ERR_UNKNOWN;
        case EXFAT_ERR_IO_ERROR:  return VFS_ERR_IO_ERROR;
        case EXFAT_ERR_INVALID_ARG: return VFS_ERR_INVALID_ARG;
        case EXFAT_ERR_PERMISSION: return VFS_ERR_PERMISSION;
        case EXFAT_ERR_CORRUPTED: return VFS_ERR_CORRUPTED;
        case EXFAT_ERR_UNSUPPORTED: return VFS_ERR_UNSUPPORTED;
        default:                  return VFS_ERR_UNKNOWN;
    }
}

/* Convert exFAT attributes to VFS attributes */
static uint32_t exfat_to_vfs_attr(uint8_t exfat_attr) {
    uint32_t vfs_attr = 0;
    
    if (exfat_attr & EXFAT_ATTR_READ_ONLY) {
        vfs_attr |= VFS_ATTR_READ;
    } else {
        vfs_attr |= (VFS_ATTR_READ | VFS_ATTR_WRITE);
    }
    
    if (exfat_attr & EXFAT_ATTR_DIRECTORY) {
        vfs_attr |= VFS_ATTR_EXECUTE; /* Directories are executable in UNIX-like systems */
    }
    
    if (exfat_attr & EXFAT_ATTR_HIDDEN) {
        vfs_attr |= VFS_ATTR_HIDDEN;
    }
    
    if (exfat_attr & EXFAT_ATTR_SYSTEM) {
        vfs_attr |= VFS_ATTR_SYSTEM;
    }
    
    if (exfat_attr & EXFAT_ATTR_ARCHIVE) {
        vfs_attr |= VFS_ATTR_ARCHIVE;
    }
    
    return vfs_attr;
}

/* Helper to normalize paths for exFAT */
static void normalize_exfat_path(const char* vfs_path, char* exfat_path, int max_len) {
    /* Skip initial slash if present - exFAT implementation expects no leading slash */
    if (vfs_path[0] == '/') {
        strncpy(exfat_path, vfs_path + 1, max_len - 1);
    } else {
        /* Copy path as is */
        strncpy(exfat_path, vfs_path, max_len - 1);
    }
    exfat_path[max_len - 1] = '\0';
    
    /* Convert empty path to root directory */
    if (exfat_path[0] == '\0') {
        exfat_path[0] = '/';
        exfat_path[1] = '\0';
    }
}

/* Structure to keep directory reading state */
typedef struct {
    exfat_file_entry_t entries[EXFAT_MAX_ENTRIES]; /* Cache of directory entries */
    int num_entries;                /* Number of entries in the cache */
    int current_index;              /* Current position in the cache */
    char path[VFS_MAX_PATH];        /* Directory path */
} exfat_dir_data_t;

/* Structure to keep file reading state */
typedef struct {
    char filename[VFS_MAX_PATH];    /* Filename */
    uint32_t file_size;            /* Size of the file */
    uint32_t current_position;     /* Current position in file */
} exfat_file_data_t;

/* Mount function for exFAT */
static int exfat_vfs_mount(vfs_mount_t* mount) {
    log_info("exFAT-VFS", "Mounting exFAT filesystem on %s", mount->mount_point);
    
    /* Initialize exFAT on specified device */
    int result = exfat_init(mount->device[0] ? mount->device : "default_device");
    if (result != EXFAT_SUCCESS) {
        log_error("exFAT-VFS", "Failed to initialize exFAT filesystem: %d", result);
        return exfat_to_vfs_error(result);
    }
    
    /* No special mount data needed for exFAT */
    mount->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Unmount function for exFAT */
static int exfat_vfs_unmount(vfs_mount_t* mount) {
    log_info("exFAT-VFS", "Unmounting exFAT filesystem from %s", mount->mount_point);
    
    /* No special unmount procedure needed */
    return VFS_SUCCESS;
}

/* Open function for exFAT */
static int exfat_vfs_open(vfs_mount_t* mount, const char* path, int flags, vfs_file_t** file) {
    char exfat_path[VFS_MAX_PATH];
    
    log_debug("exFAT-VFS", "Opening %s with flags %x", path, flags);
    
    /* Normalize the path for exFAT */
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    /* Check if file exists */
    int exists = exfat_file_exists(exfat_path);
    
    /* Handle file creation */
    if (exists <= 0 && (flags & VFS_OPEN_CREATE)) {
        log_debug("exFAT-VFS", "Creating new file: %s", exfat_path);
        
        /* Create an empty file */
        int result = exfat_write_file(exfat_path, "", 0, EXFAT_WRITE_CREATE);
        if (result < 0) {
            log_error("exFAT-VFS", "Failed to create file: %s (%d)", exfat_path, result);
            return exfat_to_vfs_error(result);
        }
    } else if (exists <= 0) {
        log_error("exFAT-VFS", "File not found: %s", exfat_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    int file_size = exfat_get_file_size(exfat_path);
    if (file_size < 0) {
        log_error("exFAT-VFS", "Error getting file size: %s (%d)", exfat_path, file_size);
        return exfat_to_vfs_error(file_size);
    }
    
    /* Create exFAT file data structure */
    exfat_file_data_t* file_data = (exfat_file_data_t*)malloc(sizeof(exfat_file_data_t));
    if (!file_data) {
        log_error("exFAT-VFS", "Failed to allocate memory for file handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize file data */
    strncpy(file_data->filename, exfat_path, VFS_MAX_PATH - 1);
    file_data->filename[VFS_MAX_PATH - 1] = '\0';
    file_data->file_size = file_size;
    file_data->current_position = 0;
    
    /* If truncate flag is set, truncate the file */
    if (flags & VFS_OPEN_TRUNCATE) {
        log_debug("exFAT-VFS", "Truncating file: %s", exfat_path);
        
        /* Truncate by writing empty string */
        int result = exfat_write_file(exfat_path, "", 0, EXFAT_WRITE_TRUNCATE);
        if (result < 0) {
            free(file_data);
            log_error("exFAT-VFS", "Failed to truncate file: %s (%d)", exfat_path, result);
            return exfat_to_vfs_error(result);
        }
        
        file_data->file_size = 0;
    }
    
    /* Store file data in the file handle */
    (*file)->fs_data = file_data;
    
    log_debug("exFAT-VFS", "File opened successfully: %s (size: %d bytes)", exfat_path, file_size);
    
    return VFS_SUCCESS;
}

/* Close function for exFAT */
static int exfat_vfs_close(vfs_file_t* file) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the file data */
    free(file->fs_data);
    file->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Read function for exFAT */
static int exfat_vfs_read(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    exfat_file_data_t* file_data = (exfat_file_data_t*)file->fs_data;
    
    /* Check if we're at the end of the file */
    if (file_data->current_position >= file_data->file_size) {
        *bytes_read = 0;
        return VFS_SUCCESS;
    }
    
    /* Adjust size to not read past the end of the file */
    if (file_data->current_position + size > file_data->file_size) {
        size = file_data->file_size - file_data->current_position;
    }
    
    /* Allocate a buffer for the entire file */
    char* file_buffer = (char*)malloc(file_data->file_size);
    if (!file_buffer) {
        log_error("exFAT-VFS", "Failed to allocate memory for file read");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Read the entire file */
    int result = exfat_read_file(file_data->filename, file_buffer, file_data->file_size);
    if (result < 0) {
        free(file_buffer);
        log_error("exFAT-VFS", "Error reading file: %s (%d)", file_data->filename, result);
        return exfat_to_vfs_error(result);
    }
    
    /* Copy the requested portion to the user's buffer */
    memcpy(buffer, file_buffer + file_data->current_position, size);
    
    /* Free the temporary buffer */
    free(file_buffer);
    
    /* Update position and return read size */
    file_data->current_position += size;
    *bytes_read = size;
    
    return VFS_SUCCESS;
}

/* Write function for exFAT */
static int exfat_vfs_write(vfs_file_t* file, const void* buffer, uint32_t size, uint32_t* bytes_written) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    exfat_file_data_t* file_data = (exfat_file_data_t*)file->fs_data;
    
    log_debug("exFAT-VFS", "Writing %d bytes to file %s at position %d", 
             size, file_data->filename, file_data->current_position);
    
    /* Allocate a buffer for the entire file */
    char* file_buffer = (char*)malloc(file_data->file_size + size);
    if (!file_buffer) {
        log_error("exFAT-VFS", "Failed to allocate memory for file write");
        return VFS_ERR_NO_SPACE;
    }
    
    /* If we're not overwriting the entire file, we need to read the existing contents first */
    if (file_data->current_position > 0 || file_data->current_position + size < file_data->file_size) {
        int result = exfat_read_file(file_data->filename, file_buffer, file_data->file_size);
        if (result < 0) {
            free(file_buffer);
            log_error("exFAT-VFS", "Error reading file for write: %s (%d)", file_data->filename, result);
            return exfat_to_vfs_error(result);
        }
    }
    
    /* Copy the new data at the current position */
    memcpy(file_buffer + file_data->current_position, buffer, size);
    
    /* Calculate the new file size */
    uint32_t new_file_size = file_data->current_position + size;
    if (new_file_size < file_data->file_size) {
        new_file_size = file_data->file_size;
    }
    
    /* Write the file */
    int result = exfat_write_file(file_data->filename, file_buffer, new_file_size, EXFAT_WRITE_TRUNCATE);
    
    /* Free the buffer */
    free(file_buffer);
    
    if (result < 0) {
        log_error("exFAT-VFS", "Error writing file: %s (%d)", file_data->filename, result);
        return exfat_to_vfs_error(result);
    }
    
    /* Update position and file size */
    file_data->current_position += size;
    if (file_data->current_position > file_data->file_size) {
        file_data->file_size = file_data->current_position;
    }
    
    /* Return the number of bytes written */
    *bytes_written = size;
    
    return VFS_SUCCESS;
}

/* Seek function for exFAT */
static int exfat_vfs_seek(vfs_file_t* file, int64_t offset, int whence) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    exfat_file_data_t* file_data = (exfat_file_data_t*)file->fs_data;
    int64_t new_position = 0;
    
    /* Calculate new position based on seek parameters */
    if (whence == VFS_SEEK_SET) {
        new_position = offset;
    } else if (whence == VFS_SEEK_CUR) {
        new_position = file_data->current_position + offset;
    } else if (whence == VFS_SEEK_END) {
        new_position = file_data->file_size + offset;
    } else {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Check boundaries */
    if (new_position < 0 || new_position > file_data->file_size) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Update position */
    file_data->current_position = new_position;
    
    return VFS_SUCCESS;
}

/* Tell function for exFAT */
static int exfat_vfs_tell(vfs_file_t* file, uint64_t* offset) {
    if (!file || !file->fs_data || !offset) {
        return VFS_ERR_INVALID_ARG;
    }
    
    exfat_file_data_t* file_data = (exfat_file_data_t*)file->fs_data;
    
    *offset = file_data->current_position;
    
    return VFS_SUCCESS;
}

/* Stat function for exFAT */
static int exfat_vfs_stat(vfs_mount_t* mount, const char* path, vfs_stat_t* stat) {
    char exfat_path[VFS_MAX_PATH];
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    /* Check if the file/directory exists */
    int exists = exfat_file_exists(exfat_path);
    if (exists <= 0) {
        return exfat_to_vfs_error(exists);
    }
    
    /* Get file size */
    int file_size = exfat_get_file_size(exfat_path);
    if (file_size < 0) {
        return exfat_to_vfs_error(file_size);
    }
    
    /* Fill in the stat structure */
    stat->size = file_size;
    
    /* We can't get all information from exFAT, so fill in some defaults */
    stat->dev = 0;
    stat->ino = 0;
    stat->mode = 0;
    stat->links = 1;
    stat->uid = 0;
    stat->gid = 0;
    stat->rdev = 0;
    stat->block_size = 4096; /* Default cluster size for exFAT */
    stat->blocks = (file_size + 4095) / 4096;
    stat->time_access = 0;
    stat->time_modify = 0;
    stat->time_create = 0;
    
    return VFS_SUCCESS;
}

/* Opendir function for exFAT */
static int exfat_vfs_opendir(vfs_mount_t* mount, const char* path, vfs_file_t** dir) {
    char exfat_path[VFS_MAX_PATH];
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    log_debug("exFAT-VFS", "Opening directory: %s", exfat_path);
    
    /* Create directory data structure */
    exfat_dir_data_t* dir_data = (exfat_dir_data_t*)malloc(sizeof(exfat_dir_data_t));
    if (!dir_data) {
        log_error("exFAT-VFS", "Failed to allocate memory for directory handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize directory data */
    strncpy(dir_data->path, exfat_path, VFS_MAX_PATH - 1);
    dir_data->path[VFS_MAX_PATH - 1] = '\0';
    
    /* Get directory entries - caching them upfront */
    dir_data->num_entries = exfat_list_directory(exfat_path, dir_data->entries, EXFAT_MAX_ENTRIES);
    
    if (dir_data->num_entries < 0) {
        log_error("exFAT-VFS", "Error listing directory: %s (%d)", exfat_path, dir_data->num_entries);
        free(dir_data);
        return exfat_to_vfs_error(dir_data->num_entries);
    }
    
    dir_data->current_index = 0;
    
    /* Store directory data in the file handle */
    (*dir)->fs_data = dir_data;
    
    log_debug("exFAT-VFS", "Directory opened successfully: %s (entries: %d)", exfat_path, dir_data->num_entries);
    
    return VFS_SUCCESS;
}

/* Readdir function for exFAT */
static int exfat_vfs_readdir(vfs_file_t* dir, vfs_dirent_t* dirent) {
    if (!dir || !dir->fs_data || !dirent) {
        return VFS_ERR_INVALID_ARG;
    }
    
    exfat_dir_data_t* dir_data = (exfat_dir_data_t*)dir->fs_data;
    
    /* Check if we've read all entries */
    if (dir_data->current_index >= dir_data->num_entries) {
        return 1; /* End of directory */
    }
    
    /* Copy entry data */
    exfat_file_entry_t* entry = &dir_data->entries[dir_data->current_index];
    strncpy(dirent->name, entry->name, VFS_MAX_FILENAME - 1);
    dirent->name[VFS_MAX_FILENAME - 1] = '\0';
    
    dirent->size = entry->size;
    dirent->attributes = exfat_to_vfs_attr(entry->attributes);
    
    /* Determine file type */
    dirent->type = (entry->attributes & EXFAT_ATTR_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
    
    /* Copy timestamps */
    dirent->time_create = entry->create_date << 16 | entry->create_time;
    dirent->time_modify = entry->last_modified_date << 16 | entry->last_modified_time;
    dirent->time_access = entry->last_access_date << 16;
    
    /* Move to next entry */
    dir_data->current_index++;
    
    return VFS_SUCCESS;
}

/* Closedir function for exFAT */
static int exfat_vfs_closedir(vfs_file_t* dir) {
    if (!dir || !dir->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the directory data */
    free(dir->fs_data);
    dir->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Mkdir function for exFAT */
static int exfat_vfs_mkdir(vfs_mount_t* mount, const char* path, uint32_t mode) {
    char exfat_path[VFS_MAX_PATH];
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    log_debug("exFAT-VFS", "Creating directory: %s", exfat_path);
    
    /* Check if the directory already exists */
    int exists = exfat_file_exists(exfat_path);
    if (exists > 0) {
        log_error("exFAT-VFS", "Directory or file already exists: %s", exfat_path);
        return VFS_ERR_EXISTS;
    }
    
    /* Convert VFS mode to exFAT attributes */
    uint16_t exfat_mode = 0;
    if (!(mode & VFS_ATTR_WRITE)) {
        exfat_mode |= EXFAT_ATTR_READ_ONLY;
    }
    
    /* Create the directory */
    int result = exfat_mkdir(exfat_path, exfat_mode);
    if (result < 0) {
        log_error("exFAT-VFS", "Failed to create directory: %s (%d)", exfat_path, result);
        return exfat_to_vfs_error(result);
    }
    
    log_debug("exFAT-VFS", "Directory created successfully: %s", exfat_path);
    return VFS_SUCCESS;
}

/* Rmdir function for exFAT */
static int exfat_vfs_rmdir(vfs_mount_t* mount, const char* path) {
    char exfat_path[VFS_MAX_PATH];
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    log_debug("exFAT-VFS", "Removing directory: %s", exfat_path);
    
    /* Check if the directory exists */
    int exists = exfat_file_exists(exfat_path);
    if (exists <= 0) {
        log_error("exFAT-VFS", "Directory not found: %s", exfat_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check if it's a directory by getting directory entries */
    exfat_file_entry_t entries[2];
    int result = exfat_list_directory(exfat_path, entries, 2);
    
    if (result < 0) {
        log_error("exFAT-VFS", "Path is not a directory: %s", exfat_path);
        return VFS_ERR_NOT_DIR;
    }
    
    /* Check if the directory is empty - should return 0 entries for an empty directory */
    if (result > 0) {
        log_error("exFAT-VFS", "Directory is not empty: %s", exfat_path);
        return VFS_ERR_NOT_EMPTY;
    }
    
    /* Remove the directory */
    result = exfat_remove(exfat_path);
    if (result < 0) {
        log_error("exFAT-VFS", "Failed to remove directory: %s (%d)", exfat_path, result);
        return exfat_to_vfs_error(result);
    }
    
    log_debug("exFAT-VFS", "Directory removed successfully: %s", exfat_path);
    return VFS_SUCCESS;
}

/* Unlink function for exFAT */
static int exfat_vfs_unlink(vfs_mount_t* mount, const char* path) {
    char exfat_path[VFS_MAX_PATH];
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    log_debug("exFAT-VFS", "Deleting file: %s", exfat_path);
    
    /* Check if the file exists */
    int exists = exfat_file_exists(exfat_path);
    if (exists <= 0) {
        log_error("exFAT-VFS", "File not found: %s", exfat_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check if it's a regular file by getting file size - directories have size 0 */
    int file_size = exfat_get_file_size(exfat_path);
    if (file_size < 0) {
        log_error("exFAT-VFS", "Error getting file size: %s", exfat_path);
        return exfat_to_vfs_error(file_size);
    }

    /* For directories, use rmdir instead */
    if (file_size == 0) {
        log_error("exFAT-VFS", "Path is a directory, use rmdir: %s", exfat_path);
        return VFS_ERR_NOT_FILE;
    }
    
    /* Delete the file */
    int result = exfat_remove(exfat_path);
    
    if (result < 0) {
        log_error("exFAT-VFS", "Error deleting file: %s (%d)", exfat_path, result);
        return exfat_to_vfs_error(result);
    }
    
    log_debug("exFAT-VFS", "File deleted successfully: %s", exfat_path);
    return VFS_SUCCESS;
}

/* Rename function for exFAT */
static int exfat_vfs_rename(vfs_mount_t* mount, const char* old_path, const char* new_path) {
    char exfat_old_path[VFS_MAX_PATH];
    char exfat_new_path[VFS_MAX_PATH];
    
    normalize_exfat_path(old_path, exfat_old_path, VFS_MAX_PATH);
    normalize_exfat_path(new_path, exfat_new_path, VFS_MAX_PATH);
    
    log_debug("exFAT-VFS", "Renaming %s to %s", exfat_old_path, exfat_new_path);
    
    /* Check if the source exists */
    int exists = exfat_file_exists(exfat_old_path);
    if (exists <= 0) {
        log_error("exFAT-VFS", "Source path not found: %s", exfat_old_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check if destination exists */
    exists = exfat_file_exists(exfat_new_path);
    if (exists > 0) {
        log_error("exFAT-VFS", "Destination already exists: %s", exfat_new_path);
        return VFS_ERR_EXISTS;
    }
    
    /* Rename the file */
    int result = exfat_rename(exfat_old_path, exfat_new_path);
    if (result < 0) {
        log_error("exFAT-VFS", "Error renaming: %s to %s (%d)", exfat_old_path, exfat_new_path, result);
        return exfat_to_vfs_error(result);
    }
    
    log_debug("exFAT-VFS", "Renamed successfully: %s to %s", exfat_old_path, exfat_new_path);
    return VFS_SUCCESS;
}

/* Statfs function for exFAT */
static int exfat_vfs_statfs(vfs_mount_t* mount, uint64_t* total, uint64_t* free) {
    if (!mount || !total || !free) {
        return VFS_ERR_INVALID_ARG;
    }
    
    log_debug("exFAT-VFS", "Getting filesystem information for %s", mount->mount_point);
    
    /* Call the exFAT implementation to get filesystem information */
    exfat_fs_info_t fs_info;
    int result = exfat_get_fs_info(&fs_info);
    if (result < 0) {
        log_error("exFAT-VFS", "Failed to get filesystem information: %d", result);
        return exfat_to_vfs_error(result);
    }
    
    /* Calculate total and free space */
    *total = (uint64_t)fs_info.total_clusters * fs_info.cluster_size;
    *free = (uint64_t)fs_info.free_clusters * fs_info.cluster_size;
    
    log_debug("exFAT-VFS", "Filesystem information: %s, %llu/%llu bytes free", 
             fs_info.volume_label, *free, *total);
    
    return VFS_SUCCESS;
}

/* Flush function for exFAT */
static int exfat_vfs_flush(vfs_file_t* file) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* No specific flush operation needed for our exFAT implementation */
    /* All writes are immediately committed to the disk image */
    
    return VFS_SUCCESS;
}

/* Truncate function for exFAT */
static int exfat_vfs_truncate(vfs_file_t* file, uint64_t size) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    exfat_file_data_t* file_data = (exfat_file_data_t*)file->fs_data;
    
    if (size == file_data->file_size) {
        /* No change needed */
        return VFS_SUCCESS;
    }
    
    log_debug("exFAT-VFS", "Truncating file %s to size %llu", file_data->filename, size);
    
    /* Allocate a buffer for the new file size */
    char* buffer = NULL;
    
    if (size > 0) {
        buffer = (char*)malloc(size);
        if (!buffer) {
            log_error("exFAT-VFS", "Failed to allocate memory for truncate operation");
            return VFS_ERR_NO_SPACE;
        }
        
        /* If we're truncating to a larger size, read the existing content */
        if (size > file_data->file_size) {
            /* Read the existing file content */
            int result = exfat_read_file(file_data->filename, buffer, file_data->file_size);
            if (result < 0) {
                free(buffer);
                log_error("exFAT-VFS", "Error reading file for truncate: %s (%d)", file_data->filename, result);
                return exfat_to_vfs_error(result);
            }
            
            /* Zero-fill the extended part */
            memset(buffer + file_data->file_size, 0, size - file_data->file_size);
        } else {
            /* Read just the portion we're keeping */
            int result = exfat_read_file(file_data->filename, buffer, size);
            if (result < 0) {
                free(buffer);
                log_error("exFAT-VFS", "Error reading file for truncate: %s (%d)", file_data->filename, result);
                return exfat_to_vfs_error(result);
            }
        }
    }
    
    /* Write the new file content or empty file */
    int result;
    if (size > 0) {
        result = exfat_write_file(file_data->filename, buffer, size, EXFAT_WRITE_TRUNCATE);
        free(buffer);
    } else {
        result = exfat_write_file(file_data->filename, "", 0, EXFAT_WRITE_TRUNCATE);
    }
    
    if (result < 0) {
        log_error("exFAT-VFS", "Error truncating file: %s (%d)", file_data->filename, result);
        return exfat_to_vfs_error(result);
    }
    
    /* Update file size */
    file_data->file_size = size;
    
    /* If current position is beyond new file size, adjust it */
    if (file_data->current_position > size) {
        file_data->current_position = size;
    }
    
    return VFS_SUCCESS;
}

/* Chmod function for exFAT */
static int exfat_vfs_chmod(vfs_mount_t* mount, const char* path, uint32_t mode) {
    /* exFAT doesn't fully support UNIX-style permissions, but we can handle read-only flag */
    char exfat_path[VFS_MAX_PATH];
    normalize_exfat_path(path, exfat_path, VFS_MAX_PATH);
    
    log_debug("exFAT-VFS", "Changing mode for %s to %x", exfat_path, mode);
    
    /* Currently not supported directly by the exFAT implementation */
    /* For a full implementation, we would need to read the file entry,
       update its attributes, and write it back */
    
    return VFS_ERR_UNSUPPORTED;
}

/* Create the exFAT VFS filesystem type structure */
vfs_filesystem_t exfat_vfs_fs = {
    .name = "exfat",
    .mount = exfat_vfs_mount,
    .unmount = exfat_vfs_unmount,
    .open = exfat_vfs_open,
    .close = exfat_vfs_close,
    .read = exfat_vfs_read,
    .write = exfat_vfs_write,
    .seek = exfat_vfs_seek,
    .tell = exfat_vfs_tell,
    .flush = exfat_vfs_flush, /* Not needed for exFAT */
    .stat = exfat_vfs_stat,
    .opendir = exfat_vfs_opendir,
    .readdir = exfat_vfs_readdir,
    .closedir = exfat_vfs_closedir,
    .mkdir = exfat_vfs_mkdir,
    .rmdir = exfat_vfs_rmdir,
    .unlink = exfat_vfs_unlink,
    .rename = exfat_vfs_rename,
    .statfs = exfat_vfs_statfs,
};

/* Register exFAT with the VFS */
void register_exfat_with_vfs(void) {
    log_info("exFAT-VFS", "Registering exFAT filesystem with VFS");
    vfs_register_fs(&exfat_vfs_fs);
}