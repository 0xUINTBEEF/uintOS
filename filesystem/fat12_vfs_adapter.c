#include "fat12.h"
#include "vfs/vfs.h"
#include "../kernel/logging/log.h"
#include <string.h>

/* FAT12 implementation for VFS */

/* Convert FAT12 error codes to VFS error codes */
static int fat12_to_vfs_error(int fat12_error) {
    switch (fat12_error) {
        case FAT12_SUCCESS:       return VFS_SUCCESS;
        case FAT12_ERR_NOT_FOUND: return VFS_ERR_NOT_FOUND;
        case FAT12_ERR_NO_SPACE:  return VFS_ERR_NO_SPACE;
        case FAT12_ERR_BAD_FORMAT: return VFS_ERR_UNKNOWN;
        case FAT12_ERR_IO_ERROR:  return VFS_ERR_IO_ERROR;
        case FAT12_ERR_INVALID_ARG: return VFS_ERR_INVALID_ARG;
        default:                  return VFS_ERR_UNKNOWN;
    }
}

/* Convert FAT12 attributes to VFS attributes */
static uint32_t fat12_to_vfs_attr(uint8_t fat12_attr) {
    uint32_t vfs_attr = 0;
    
    if (fat12_attr & FAT12_ATTR_READ_ONLY) {
        vfs_attr |= VFS_ATTR_READ;
    } else {
        vfs_attr |= (VFS_ATTR_READ | VFS_ATTR_WRITE);
    }
    
    if (fat12_attr & FAT12_ATTR_DIRECTORY) {
        vfs_attr |= VFS_ATTR_EXECUTE; /* Directories are executable in UNIX-like systems */
    }
    
    if (fat12_attr & FAT12_ATTR_HIDDEN) {
        vfs_attr |= VFS_ATTR_HIDDEN;
    }
    
    if (fat12_attr & FAT12_ATTR_SYSTEM) {
        vfs_attr |= VFS_ATTR_SYSTEM;
    }
    
    if (fat12_attr & FAT12_ATTR_ARCHIVE) {
        vfs_attr |= VFS_ATTR_ARCHIVE;
    }
    
    return vfs_attr;
}

/* Helper to normalize paths (remove leading slash since FAT12 doesn't use it) */
static void normalize_fat12_path(const char* vfs_path, char* fat12_path, int max_len) {
    /* Skip initial slash if present */
    if (vfs_path[0] == '/') {
        strncpy(fat12_path, vfs_path + 1, max_len - 1);
    } else {
        strncpy(fat12_path, vfs_path, max_len - 1);
    }
    fat12_path[max_len - 1] = '\0';
    
    /* Convert empty path to current directory */
    if (fat12_path[0] == '\0') {
        fat12_path[0] = '.';
        fat12_path[1] = '\0';
    }
}

/* Structure to keep directory reading state */
typedef struct {
    fat12_file_entry_t entries[20]; /* Cache of directory entries */
    int num_entries;                /* Number of entries in the cache */
    int current_index;              /* Current position in the cache */
    char path[VFS_MAX_PATH];        /* Directory path */
} fat12_dir_data_t;

/* Structure to keep file reading state */
typedef struct {
    char filename[VFS_MAX_PATH];    /* Filename */
    int file_size;                  /* Size of the file */
    int current_position;           /* Current position in file */
} fat12_file_data_t;

/* Mount function for FAT12 */
static int fat12_vfs_mount(vfs_mount_t* mount) {
    log_info("FAT12-VFS", "Mounting FAT12 filesystem on %s", mount->mount_point);
    
    /* Initialize FAT12 if needed */
    fat12_init();
    
    /* No special mount data needed for FAT12 */
    mount->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Unmount function for FAT12 */
static int fat12_vfs_unmount(vfs_mount_t* mount) {
    log_info("FAT12-VFS", "Unmounting FAT12 filesystem from %s", mount->mount_point);
    
    /* No special unmount procedure needed */
    return VFS_SUCCESS;
}

/* Open function for FAT12 */
static int fat12_vfs_open(vfs_mount_t* mount, const char* path, int flags, vfs_file_t** file) {
    char fat12_path[VFS_MAX_PATH];
    
    log_debug("FAT12-VFS", "Opening %s with flags %x", path, flags);
    
    /* Check that the file exists */
    normalize_fat12_path(path, fat12_path, VFS_MAX_PATH);
    
    /* For directories, we handle separately */
    if (flags & VFS_OPEN_CREATE) {
        /* FAT12 implementation doesn't support creating files yet */
        log_error("FAT12-VFS", "File creation not supported in FAT12");
        return VFS_ERR_UNSUPPORTED;
    }
    
    int exists = fat12_file_exists(fat12_path);
    if (exists <= 0) {
        log_error("FAT12-VFS", "File not found: %s", fat12_path);
        return fat12_to_vfs_error(exists);
    }
    
    /* Get file size */
    int file_size = fat12_get_file_size(fat12_path);
    if (file_size < 0) {
        log_error("FAT12-VFS", "Error getting file size: %s (%d)", fat12_path, file_size);
        return fat12_to_vfs_error(file_size);
    }
    
    /* Create FAT12 file data structure */
    fat12_file_data_t* file_data = (fat12_file_data_t*)malloc(sizeof(fat12_file_data_t));
    if (!file_data) {
        log_error("FAT12-VFS", "Failed to allocate memory for file handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize file data */
    strncpy(file_data->filename, fat12_path, VFS_MAX_PATH - 1);
    file_data->filename[VFS_MAX_PATH - 1] = '\0';
    file_data->file_size = file_size;
    file_data->current_position = 0;
    
    /* Store file data in the file handle */
    (*file)->fs_data = file_data;
    
    log_debug("FAT12-VFS", "File opened successfully: %s (size: %d bytes)", fat12_path, file_size);
    
    return VFS_SUCCESS;
}

/* Close function for FAT12 */
static int fat12_vfs_close(vfs_file_t* file) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the file data */
    free(file->fs_data);
    file->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Read function for FAT12 */
static int fat12_vfs_read(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    fat12_file_data_t* file_data = (fat12_file_data_t*)file->fs_data;
    
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
        log_error("FAT12-VFS", "Failed to allocate memory for file read");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Read the entire file - FAT12 implementation doesn't support partial reads */
    int result = fat12_read_file(file_data->filename, file_buffer, file_data->file_size);
    if (result < 0) {
        free(file_buffer);
        log_error("FAT12-VFS", "Error reading file: %s (%d)", file_data->filename, result);
        return fat12_to_vfs_error(result);
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

/* Write function for FAT12 */
static int fat12_vfs_write(vfs_file_t* file, const void* buffer, uint32_t size, uint32_t* bytes_written) {
    if (!file || !file->fs_data || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    fat12_file_data_t* file_data = (fat12_file_data_t*)file->fs_data;
    
    log_debug("FAT12-VFS", "Writing %d bytes to file %s at position %d", 
             size, file_data->filename, file_data->current_position);
    
    /* Allocate a buffer for the entire file */
    char* file_buffer = (char*)malloc(file_data->file_size + size);
    if (!file_buffer) {
        log_error("FAT12-VFS", "Failed to allocate memory for file write");
        return VFS_ERR_NO_SPACE;
    }
    
    /* If we're not overwriting the entire file, we need to read the existing contents first */
    if (file_data->current_position > 0 || file_data->current_position + size < file_data->file_size) {
        int result = fat12_read_file(file_data->filename, file_buffer, file_data->file_size);
        if (result < 0) {
            free(file_buffer);
            log_error("FAT12-VFS", "Error reading file for write: %s (%d)", file_data->filename, result);
            return fat12_to_vfs_error(result);
        }
    }
    
    /* Copy the new data at the current position */
    memcpy(file_buffer + file_data->current_position, buffer, size);
    
    /* Calculate the new file size */
    int new_file_size = file_data->current_position + size;
    if (new_file_size < file_data->file_size) {
        new_file_size = file_data->file_size;
    }
    
    /* Write the file */
    int result = fat12_write_file(file_data->filename, file_buffer, new_file_size, FAT12_WRITE_TRUNCATE);
    
    /* Free the buffer */
    free(file_buffer);
    
    if (result < 0) {
        log_error("FAT12-VFS", "Error writing file: %s (%d)", file_data->filename, result);
        return fat12_to_vfs_error(result);
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

/* Seek function for FAT12 */
static int fat12_vfs_seek(vfs_file_t* file, int64_t offset, int whence) {
    if (!file || !file->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    fat12_file_data_t* file_data = (fat12_file_data_t*)file->fs_data;
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

/* Tell function for FAT12 */
static int fat12_vfs_tell(vfs_file_t* file, uint64_t* offset) {
    if (!file || !file->fs_data || !offset) {
        return VFS_ERR_INVALID_ARG;
    }
    
    fat12_file_data_t* file_data = (fat12_file_data_t*)file->fs_data;
    
    *offset = file_data->current_position;
    
    return VFS_SUCCESS;
}

/* Stat function for FAT12 */
static int fat12_vfs_stat(vfs_mount_t* mount, const char* path, vfs_stat_t* stat) {
    char fat12_path[VFS_MAX_PATH];
    normalize_fat12_path(path, fat12_path, VFS_MAX_PATH);
    
    /* Check if the file/directory exists */
    int exists = fat12_file_exists(fat12_path);
    if (exists <= 0) {
        return fat12_to_vfs_error(exists);
    }
    
    /* Get file size */
    int file_size = fat12_get_file_size(fat12_path);
    if (file_size < 0) {
        return fat12_to_vfs_error(file_size);
    }
    
    /* Fill in the stat structure */
    stat->size = file_size;
    
    /* We can't get all information from FAT12, so fill in some defaults */
    stat->dev = 0;
    stat->ino = 0;
    stat->mode = 0;
    stat->links = 1;
    stat->uid = 0;
    stat->gid = 0;
    stat->rdev = 0;
    stat->block_size = 512; /* Default sector size */
    stat->blocks = (file_size + 511) / 512;
    stat->time_access = 0;
    stat->time_modify = 0;
    stat->time_create = 0;
    
    return VFS_SUCCESS;
}

/* Opendir function for FAT12 */
static int fat12_vfs_opendir(vfs_mount_t* mount, const char* path, vfs_file_t** dir) {
    char fat12_path[VFS_MAX_PATH];
    normalize_fat12_path(path, fat12_path, VFS_MAX_PATH);
    
    log_debug("FAT12-VFS", "Opening directory: %s", fat12_path);
    
    /* Create directory data structure */
    fat12_dir_data_t* dir_data = (fat12_dir_data_t*)malloc(sizeof(fat12_dir_data_t));
    if (!dir_data) {
        log_error("FAT12-VFS", "Failed to allocate memory for directory handle");
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize directory data */
    strncpy(dir_data->path, fat12_path, VFS_MAX_PATH - 1);
    dir_data->path[VFS_MAX_PATH - 1] = '\0';
    dir_data->num_entries = fat12_list_directory(fat12_path, dir_data->entries, 20);
    
    if (dir_data->num_entries < 0) {
        log_error("FAT12-VFS", "Error listing directory: %s (%d)", fat12_path, dir_data->num_entries);
        free(dir_data);
        return fat12_to_vfs_error(dir_data->num_entries);
    }
    
    dir_data->current_index = 0;
    
    /* Store directory data in the file handle */
    (*dir)->fs_data = dir_data;
    
    log_debug("FAT12-VFS", "Directory opened successfully: %s (entries: %d)", fat12_path, dir_data->num_entries);
    
    return VFS_SUCCESS;
}

/* Readdir function for FAT12 */
static int fat12_vfs_readdir(vfs_file_t* dir, vfs_dirent_t* dirent) {
    if (!dir || !dir->fs_data || !dirent) {
        return VFS_ERR_INVALID_ARG;
    }
    
    fat12_dir_data_t* dir_data = (fat12_dir_data_t*)dir->fs_data;
    
    /* Check if we've read all entries */
    if (dir_data->current_index >= dir_data->num_entries) {
        return 1; /* End of directory */
    }
    
    /* Copy entry data */
    fat12_file_entry_t* entry = &dir_data->entries[dir_data->current_index];
    strncpy(dirent->name, entry->name, VFS_MAX_FILENAME - 1);
    dirent->name[VFS_MAX_FILENAME - 1] = '\0';
    
    dirent->size = entry->size;
    dirent->attributes = fat12_to_vfs_attr(entry->attributes);
    
    /* Determine file type */
    dirent->type = (entry->attributes & FAT12_ATTR_DIRECTORY) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
    
    /* Copy timestamps */
    dirent->time_create = entry->create_date << 16 | entry->create_time;
    dirent->time_modify = entry->last_modified_date << 16 | entry->last_modified_time;
    dirent->time_access = entry->last_access_date << 16;
    
    /* Move to next entry */
    dir_data->current_index++;
    
    return VFS_SUCCESS;
}

/* Closedir function for FAT12 */
static int fat12_vfs_closedir(vfs_file_t* dir) {
    if (!dir || !dir->fs_data) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Free the directory data */
    free(dir->fs_data);
    dir->fs_data = NULL;
    
    return VFS_SUCCESS;
}

/* Mkdir function for FAT12 */
static int fat12_vfs_mkdir(vfs_mount_t* mount, const char* path, uint32_t mode) {
    char fat12_path[VFS_MAX_PATH];
    normalize_fat12_path(path, fat12_path, VFS_MAX_PATH);
    
    log_debug("FAT12-VFS", "Creating directory: %s", fat12_path);
    
    /* Check if the directory already exists */
    int exists = fat12_file_exists(fat12_path);
    if (exists > 0) {
        log_error("FAT12-VFS", "Directory or file already exists: %s", fat12_path);
        return VFS_ERR_EXISTS;
    }
    
    /* Convert VFS mode to FAT12 attributes */
    uint16_t fat12_mode = 0;
    if (!(mode & VFS_ATTR_WRITE)) {
        fat12_mode |= FAT12_ATTR_READ_ONLY;
    }
    
    /* Create the directory */
    int result = fat12_mkdir(fat12_path, fat12_mode);
    if (result < 0) {
        log_error("FAT12-VFS", "Failed to create directory: %s (%d)", fat12_path, result);
        return fat12_to_vfs_error(result);
    }
    
    log_debug("FAT12-VFS", "Directory created successfully: %s", fat12_path);
    return VFS_SUCCESS;
}

/* Rmdir function for FAT12 */
static int fat12_vfs_rmdir(vfs_mount_t* mount, const char* path) {
    char fat12_path[VFS_MAX_PATH];
    normalize_fat12_path(path, fat12_path, VFS_MAX_PATH);
    
    log_debug("FAT12-VFS", "Removing directory: %s", fat12_path);
    
    /* Check if the directory exists */
    int exists = fat12_file_exists(fat12_path);
    if (exists <= 0) {
        log_error("FAT12-VFS", "Directory not found: %s", fat12_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check if it's a directory by getting directory entries */
    fat12_file_entry_t entries[2];
    int result = fat12_list_directory(fat12_path, entries, 2);
    
    if (result < 0) {
        log_error("FAT12-VFS", "Path is not a directory: %s", fat12_path);
        return VFS_ERR_NOT_DIR;
    }
    
    /* Check if the directory is empty - should return 0 entries for an empty directory */
    if (result > 0) {
        log_error("FAT12-VFS", "Directory is not empty: %s", fat12_path);
        return VFS_ERR_NOT_EMPTY;
    }
    
    /* Remove the directory */
    result = fat12_remove(fat12_path);
    if (result < 0) {
        log_error("FAT12-VFS", "Failed to remove directory: %s (%d)", fat12_path, result);
        return fat12_to_vfs_error(result);
    }
    
    log_debug("FAT12-VFS", "Directory removed successfully: %s", fat12_path);
    return VFS_SUCCESS;
}

/* Unlink function for FAT12 */
static int fat12_vfs_unlink(vfs_mount_t* mount, const char* path) {
    char fat12_path[VFS_MAX_PATH];
    normalize_fat12_path(path, fat12_path, VFS_MAX_PATH);
    
    log_debug("FAT12-VFS", "Removing file: %s", fat12_path);
    
    /* Check if the file exists */
    int exists = fat12_file_exists(fat12_path);
    if (exists <= 0) {
        log_error("FAT12-VFS", "File not found: %s", fat12_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check that it's not a directory - try to list directory contents */
    fat12_file_entry_t entries[1];
    int result = fat12_list_directory(fat12_path, entries, 1);
    
    if (result >= 0) {
        log_error("FAT12-VFS", "Path is a directory, not a file: %s", fat12_path);
        return VFS_ERR_IS_DIR;
    }
    
    /* Remove the file */
    result = fat12_remove(fat12_path);
    if (result < 0) {
        log_error("FAT12-VFS", "Failed to remove file: %s (%d)", fat12_path, result);
        return fat12_to_vfs_error(result);
    }
    
    log_debug("FAT12-VFS", "File removed successfully: %s", fat12_path);
    return VFS_SUCCESS;
}

/* Rename function for FAT12 */
static int fat12_vfs_rename(vfs_mount_t* mount, const char* old_path, const char* new_path) {
    char fat12_old_path[VFS_MAX_PATH];
    char fat12_new_path[VFS_MAX_PATH];
    
    normalize_fat12_path(old_path, fat12_old_path, VFS_MAX_PATH);
    normalize_fat12_path(new_path, fat12_new_path, VFS_MAX_PATH);
    
    log_debug("FAT12-VFS", "Renaming file: %s to %s", fat12_old_path, fat12_new_path);
    
    /* Check if the source exists */
    int exists = fat12_file_exists(fat12_old_path);
    if (exists <= 0) {
        log_error("FAT12-VFS", "Source file/directory not found: %s", fat12_old_path);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check if the destination already exists */
    exists = fat12_file_exists(fat12_new_path);
    if (exists > 0) {
        log_error("FAT12-VFS", "Destination already exists: %s", fat12_new_path);
        return VFS_ERR_EXISTS;
    }
    
    /* Check if source is a directory by trying to list its contents */
    fat12_file_entry_t entries[1];
    int is_dir = fat12_list_directory(fat12_old_path, entries, 1) >= 0;
    
    /* Rename implementation based on whether it's a file or directory */
    if (is_dir) {
        /* For directories, we need to:
           1. Create new directory with same attributes
           2. Copy all contents to new directory
           3. Remove old directory
        */
        
        /* Get source directory attributes - since FAT12 doesn't have a direct way to get attributes,
           we'll use default attributes for now */
        uint16_t dir_mode = 0;
        
        /* Create the new directory */
        int result = fat12_mkdir(fat12_new_path, dir_mode);
        if (result < 0) {
            log_error("FAT12-VFS", "Failed to create destination directory: %s", fat12_new_path);
            return fat12_to_vfs_error(result);
        }
        
        /* List all entries in the source directory */
        fat12_file_entry_t dir_entries[256]; /* Arbitrary limit */
        int entry_count = fat12_list_directory(fat12_old_path, dir_entries, 256);
        
        if (entry_count < 0) {
            /* Error reading directory - clean up and return error */
            fat12_remove(fat12_new_path);
            return fat12_to_vfs_error(entry_count);
        }
        
        /* Copy each entry to the new directory */
        for (int i = 0; i < entry_count; i++) {
            char src_item_path[VFS_MAX_PATH];
            char dst_item_path[VFS_MAX_PATH];
            
            /* Build source and destination paths */
            snprintf(src_item_path, VFS_MAX_PATH, "%s/%s", fat12_old_path, dir_entries[i].name);
            snprintf(dst_item_path, VFS_MAX_PATH, "%s/%s", fat12_new_path, dir_entries[i].name);
            
            if (dir_entries[i].attributes & FAT12_ATTR_DIRECTORY) {
                /* Recursively rename subdirectories */
                result = fat12_vfs_rename(mount, src_item_path, dst_item_path);
            } else {
                /* For files, read source and write to destination */
                char* buffer = malloc(dir_entries[i].size);
                if (!buffer) {
                    /* Clean up and return error */
                    fat12_remove(fat12_new_path);
                    return VFS_ERR_NO_SPACE;
                }
                
                /* Read source file */
                result = fat12_read_file(src_item_path, buffer, dir_entries[i].size);
                if (result < 0) {
                    free(buffer);
                    fat12_remove(fat12_new_path);
                    return fat12_to_vfs_error(result);
                }
                
                /* Write to destination file */
                result = fat12_write_file(dst_item_path, buffer, dir_entries[i].size, FAT12_WRITE_CREATE);
                free(buffer);
                
                if (result < 0) {
                    fat12_remove(fat12_new_path);
                    return fat12_to_vfs_error(result);
                }
                
                /* Remove source file */
                fat12_remove(src_item_path);
            }
        }
        
        /* Remove source directory now that all contents have been moved */
        result = fat12_remove(fat12_old_path);
        if (result < 0) {
            log_warning("FAT12-VFS", "Failed to remove source directory after rename: %s", fat12_old_path);
            /* Continue anyway since the operation was mostly successful */
        }
    } else {
        /* For files, we need to:
           1. Read the entire source file
           2. Create the destination file
           3. Write the data to the destination
           4. Remove the source file
        */
        
        /* Get file size */
        int file_size = fat12_get_file_size(fat12_old_path);
        if (file_size < 0) {
            return fat12_to_vfs_error(file_size);
        }
        
        if (file_size > 0) {
            /* Allocate buffer for file contents */
            char* buffer = malloc(file_size);
            if (!buffer) {
                return VFS_ERR_NO_SPACE;
            }
            
            /* Read source file */
            int result = fat12_read_file(fat12_old_path, buffer, file_size);
            if (result < 0) {
                free(buffer);
                return fat12_to_vfs_error(result);
            }
            
            /* Write to destination file */
            result = fat12_write_file(fat12_new_path, buffer, file_size, FAT12_WRITE_CREATE);
            free(buffer);
            
            if (result < 0) {
                return fat12_to_vfs_error(result);
            }
        } else {
            /* Create empty file */
            int result = fat12_write_file(fat12_new_path, NULL, 0, FAT12_WRITE_CREATE);
            if (result < 0) {
                return fat12_to_vfs_error(result);
            }
        }
        
        /* Remove source file */
        int result = fat12_remove(fat12_old_path);
        if (result < 0) {
            log_warning("FAT12-VFS", "Failed to remove source file after rename: %s", fat12_old_path);
            /* Continue anyway since the operation was mostly successful */
        }
    }
    
    log_debug("FAT12-VFS", "Renamed successfully: %s to %s", fat12_old_path, fat12_new_path);
    return VFS_SUCCESS;
}

/* Statfs function for FAT12 */
static int fat12_vfs_statfs(vfs_mount_t* mount, vfs_statfs_t* statfs) {
    if (!mount || !statfs) {
        return VFS_ERR_INVALID_ARG;
    }
    
    log_debug("FAT12-VFS", "Getting filesystem information for %s", mount->mount_point);
    
    /* Call the FAT12 implementation to get filesystem information */
    fat12_fs_info_t fs_info;
    int result = fat12_get_fs_info(&fs_info);
    if (result < 0) {
        log_error("FAT12-VFS", "Failed to get filesystem information: %d", result);
        return fat12_to_vfs_error(result);
    }
    
    /* Fill in the statfs structure */
    statfs->type = 0x4d534f46; /* "FOSM" (FAT on SampleOS) as magic number */
    statfs->block_size = fs_info.cluster_size;
    statfs->blocks_total = fs_info.total_clusters;
    statfs->blocks_free = fs_info.free_clusters;
    statfs->blocks_available = fs_info.free_clusters; /* Same as free for FAT12 */
    statfs->files_total = 0; /* FAT12 doesn't track this */
    statfs->files_free = 0;  /* FAT12 doesn't track this */
    statfs->name_max = 12;   /* 8.3 filename format */
    
    /* Additional filesystem specific information */
    statfs->fs_id = fs_info.volume_id;
    strncpy(statfs->volume_name, fs_info.volume_label, sizeof(statfs->volume_name) - 1);
    statfs->volume_name[sizeof(statfs->volume_name) - 1] = '\0';
    
    log_debug("FAT12-VFS", "Filesystem information: %s, %d/%d blocks free", 
             fs_info.volume_label, fs_info.free_clusters, fs_info.total_clusters);
    
    return VFS_SUCCESS;
}

/* Create the FAT12 VFS filesystem type structure */
vfs_filesystem_t fat12_vfs_fs = {
    .name = "fat12",
    .mount = fat12_vfs_mount,
    .unmount = fat12_vfs_unmount,
    .open = fat12_vfs_open,
    .close = fat12_vfs_close,
    .read = fat12_vfs_read,
    .write = fat12_vfs_write, /* FAT12 write operation implemented */
    .seek = fat12_vfs_seek,
    .tell = fat12_vfs_tell,
    .flush = NULL, /* Not needed for FAT12 */
    .stat = fat12_vfs_stat,
    .opendir = fat12_vfs_opendir,
    .readdir = fat12_vfs_readdir,
    .closedir = fat12_vfs_closedir,
    .mkdir = fat12_vfs_mkdir, /* FAT12 mkdir implemented */
    .rmdir = fat12_vfs_rmdir, /* FAT12 rmdir implemented */
    .unlink = fat12_vfs_unlink, /* FAT12 unlink implemented */
    .rename = fat12_vfs_rename, /* FAT12 rename implemented */
    .statfs = fat12_vfs_statfs, /* FAT12 statfs implemented */
};

/* Register FAT12 with the VFS */
void register_fat12_with_vfs(void) {
    log_info("FAT12-VFS", "Registering FAT12 filesystem with VFS");
    vfs_register_fs(&fat12_vfs_fs);
}