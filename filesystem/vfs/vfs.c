#include "vfs.h"
#include "../../kernel/logging/log.h"
#include <string.h>

/* Global VFS state */
static vfs_filesystem_t* registered_filesystems[VFS_MAX_MOUNTS] = {0};
static vfs_mount_t* mount_points = NULL;
static int vfs_initialized = 0;

// Mutex locks for thread synchronization - needed in a real OS
static mutex_t vfs_lock;             // Global VFS lock for operations on mount points
static mutex_t fs_registry_lock;     // Lock for filesystem type registry operations

/* String utility functions */
static void vfs_copy_path(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1 && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int vfs_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

/* Path manipulation */
static int vfs_is_root_path(const char* path) {
    return path[0] == '/' && path[1] == '\0';
}

static int vfs_is_absolute_path(const char* path) {
    return path[0] == '/';
}

static void vfs_normalize_path(const char* path, char* normalized, size_t max_len) {
    size_t len = 0;
    size_t i = 0;
    
    /* Ensure path starts with a slash */
    if (path[0] != '/') {
        normalized[len++] = '/';
    }
    
    while (path[i] && len < max_len - 1) {
        /* Skip multiple consecutive slashes */
        if (path[i] == '/' && (i == 0 || path[i-1] == '/')) {
            i++;
            continue;
        }
        
        /* Handle "." component */
        if (path[i] == '.' && (i == 0 || path[i-1] == '/')) {
            if (path[i+1] == '/' || path[i+1] == '\0') {
                i += (path[i+1] == '/') ? 2 : 1;
                continue;
            }
            
            /* Handle ".." component */
            if (path[i+1] == '.' && (path[i+2] == '/' || path[i+2] == '\0')) {
                /* Go back one level if not at root */
                if (len > 1) {
                    len--;
                    while (len > 0 && normalized[len-1] != '/') {
                        len--;
                    }
                }
                i += (path[i+2] == '/') ? 3 : 2;
                continue;
            }
        }
        
        /* Copy normal path component */
        normalized[len++] = path[i++];
    }
    
    /* Ensure there's always at least a root slash */
    if (len == 0) {
        normalized[len++] = '/';
    }
    
    /* Remove trailing slash unless it's the root */
    if (len > 1 && normalized[len-1] == '/') {
        len--;
    }
    
    normalized[len] = '\0';
}

static int vfs_path_is_prefix(const char* prefix, const char* path) {
    while (*prefix) {
        if (*prefix != *path) {
            return 0;
        }
        prefix++;
        path++;
    }
    
    /* If prefix ended but path continues, it must continue with a slash or be at the end */
    if (*path != '\0' && *path != '/') {
        return 0;
    }
    
    return 1;
}

static vfs_mount_t* vfs_find_mount_point(const char* path) {
    char normalized_path[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized_path, VFS_MAX_PATH);
    
    /* Find the longest matching mount point */
    vfs_mount_t* best_match = NULL;
    size_t best_match_len = 0;
    
    for (vfs_mount_t* mount = mount_points; mount; mount = mount->next) {
        size_t mount_len = strlen(mount->mount_point);
        
        /* Check if this mount point is a prefix of our path and longer than our current best match */
        if (mount_len > best_match_len && vfs_path_is_prefix(mount->mount_point, normalized_path)) {
            best_match = mount;
            best_match_len = mount_len;
        }
    }
    
    return best_match;
}

static void vfs_extract_relative_path(const char* full_path, const char* mount_point, char* relative_path, size_t max_len) {
    char normalized_path[VFS_MAX_PATH];
    vfs_normalize_path(full_path, normalized_path, VFS_MAX_PATH);
    
    size_t mount_len = strlen(mount_point);
    
    /* Skip the mount point prefix */
    const char* rel_start = normalized_path + mount_len;
    
    /* If we're exactly at the mount point, use slash */
    if (*rel_start == '\0') {
        relative_path[0] = '/';
        relative_path[1] = '\0';
        return;
    }
    
    /* Skip the slash if there's one */
    if (*rel_start == '/') {
        rel_start++;
    }
    
    /* Copy to output, ensuring it starts with slash */
    relative_path[0] = '/';
    vfs_copy_path(relative_path + 1, rel_start, max_len - 1);
}

/* Initialize the VFS */
int vfs_init(void) {
    if (vfs_initialized) {
        /* Already initialized */
        return VFS_SUCCESS;
    }
    
    /* Initialize the mutexes */
    mutex_init(&vfs_lock);
    mutex_init(&fs_registry_lock);
    
    /* Clear the filesystem registry */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        registered_filesystems[i] = NULL;
    }
    
    /* Initialize mount point list */
    mount_points = NULL;
    
    vfs_initialized = 1;
    log_info("VFS", "Virtual File System initialized");
    
    return VFS_SUCCESS;
}

/* Register a filesystem type */
int vfs_register_fs(vfs_filesystem_t* fs_type) {
    int result;
    
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!fs_type || !fs_type->name) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Lock the filesystem registry for thread safety
    mutex_lock(&fs_registry_lock);
    
    /* Find an empty slot */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!registered_filesystems[i]) {
            registered_filesystems[i] = fs_type;
            log_info("VFS", "Registered filesystem type: %s", fs_type->name);
            result = VFS_SUCCESS;
            goto unlock_and_return;
        } else if (vfs_strcmp(registered_filesystems[i]->name, fs_type->name) == 0) {
            /* Already registered */
            result = VFS_ERR_EXISTS;
            goto unlock_and_return;
        }
    }
    
    result = VFS_ERR_NO_SPACE;
    
unlock_and_return:
    mutex_unlock(&fs_registry_lock);
    return result;
}

/* Find a registered filesystem type by name */
static vfs_filesystem_t* vfs_find_fs_type(const char* name) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (registered_filesystems[i] && vfs_strcmp(registered_filesystems[i]->name, name) == 0) {
            return registered_filesystems[i];
        }
    }
    
    return NULL;
}

/* Mount a filesystem */
int vfs_mount(const char* fs_name, const char* device, const char* mount_point, int flags) {
    char normalized_mount[VFS_MAX_PATH];
    int result;
    
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!fs_name || !mount_point) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Lock filesystem registry for reading
    mutex_lock(&fs_registry_lock);
    
    /* Find the filesystem type */
    vfs_filesystem_t* fs_type = vfs_find_fs_type(fs_name);
    
    mutex_unlock(&fs_registry_lock);
    
    if (!fs_type) {
        log_error("VFS", "Filesystem type not found: %s", fs_name);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Normalize mount point path */
    vfs_normalize_path(mount_point, normalized_mount, VFS_MAX_PATH);
    
    // Lock the VFS mount points list for writing
    mutex_lock(&vfs_lock);
    
    /* Check if mount point already exists */
    for (vfs_mount_t* mount = mount_points; mount; mount = mount->next) {
        if (vfs_strcmp(mount->mount_point, normalized_mount) == 0) {
            log_error("VFS", "Mount point already exists: %s", normalized_mount);
            mutex_unlock(&vfs_lock);
            return VFS_ERR_EXISTS;
        }
    }
    
    /* Allocate and initialize mount structure */
    vfs_mount_t* new_mount = (vfs_mount_t*)malloc(sizeof(vfs_mount_t));
    if (!new_mount) {
        log_error("VFS", "Failed to allocate memory for mount point");
        mutex_unlock(&vfs_lock);
        return VFS_ERR_NO_SPACE;
    }
    
    vfs_copy_path(new_mount->mount_point, normalized_mount, VFS_MAX_PATH);
    if (device) {
        vfs_copy_path(new_mount->device, device, VFS_MAX_PATH);
    } else {
        new_mount->device[0] = '\0';
    }
    new_mount->fs_type = fs_type;
    new_mount->fs_data = NULL;
    new_mount->flags = flags;
    new_mount->next = NULL;
    
    // Initialize mount-specific mutex
    mutex_init(&new_mount->lock);
    
    // Unlock before calling fs-specific handler which might take time
    mutex_unlock(&vfs_lock);
    
    /* Call filesystem-specific mount handler */
    if (fs_type->mount) {
        result = fs_type->mount(new_mount);
        if (result != VFS_SUCCESS) {
            log_error("VFS", "Filesystem-specific mount failed for %s: %d", normalized_mount, result);
            free(new_mount);
            return result;
        }
    }
    
    // Lock again to update the mount points list
    mutex_lock(&vfs_lock);
    
    /* Add to mount list */
    if (!mount_points) {
        mount_points = new_mount;
    } else {
        /* Add at the end */
        vfs_mount_t* mount = mount_points;
        while (mount->next) {
            mount = mount->next;
        }
        mount->next = new_mount;
    }
    
    mutex_unlock(&vfs_lock);
    
    log_info("VFS", "Mounted %s on %s (type: %s)", device ? device : "none", normalized_mount, fs_name);
    
    return VFS_SUCCESS;
}

/* Unmount a filesystem */
int vfs_unmount(const char* mount_point) {
    char normalized_mount[VFS_MAX_PATH];
    
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!mount_point) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Normalize mount point path */
    vfs_normalize_path(mount_point, normalized_mount, VFS_MAX_PATH);
    
    // Lock the VFS mount points list for writing
    mutex_lock(&vfs_lock);
    
    /* Find the mount point */
    vfs_mount_t* prev = NULL;
    vfs_mount_t* mount = mount_points;
    
    while (mount) {
        if (vfs_strcmp(mount->mount_point, normalized_mount) == 0) {
            break;
        }
        prev = mount;
        mount = mount->next;
    }
    
    if (!mount) {
        log_error("VFS", "Mount point not found: %s", normalized_mount);
        mutex_unlock(&vfs_lock);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Call filesystem-specific unmount handler */
    if (mount->fs_type && mount->fs_type->unmount) {
        int result = mount->fs_type->unmount(mount);
        if (result != VFS_SUCCESS) {
            log_error("VFS", "Filesystem-specific unmount failed for %s: %d", normalized_mount, result);
            mutex_unlock(&vfs_lock);
            return result;
        }
    }
    
    /* Remove from mount list */
    if (prev) {
        prev->next = mount->next;
    } else {
        mount_points = mount->next;
    }
    
    log_info("VFS", "Unmounted %s", normalized_mount);
    
    /* Free mount structure */
    free(mount);
    
    mutex_unlock(&vfs_lock);
    
    return VFS_SUCCESS;
}

/* Open a file */
int vfs_open(const char* path, int flags, vfs_file_t** file) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path || !file) {
        return VFS_ERR_INVALID_ARG;
    }

    // Lock VFS while searching for mount point
    mutex_lock(&vfs_lock);
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        log_error("VFS", "No mount point for path: %s", path);
        mutex_unlock(&vfs_lock);
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Extract the relative path within the filesystem */
    char relative_path[VFS_MAX_PATH];
    vfs_extract_relative_path(path, mount->mount_point, relative_path, VFS_MAX_PATH);
    
    // We have the mount point, now lock that specific filesystem
    mutex_lock(&mount->lock);
    
    // VFS global lock can be released now that we have the mount-specific lock
    mutex_unlock(&vfs_lock);
    
    /* Allocate a file handle */
    vfs_file_t* new_file = (vfs_file_t*)malloc(sizeof(vfs_file_t));
    if (!new_file) {
        log_error("VFS", "Failed to allocate memory for file handle");
        mutex_unlock(&mount->lock);
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize the file handle */
    new_file->mount = mount;
    vfs_copy_path(new_file->path, relative_path, VFS_MAX_PATH);
    new_file->flags = flags;
    new_file->position = 0;
    new_file->fs_data = NULL;
    
    // Initialize file-specific lock for concurrent read/write operations
    mutex_init(&new_file->lock);
    
    /* Call filesystem-specific open handler */
    if (mount->fs_type && mount->fs_type->open) {
        int result = mount->fs_type->open(mount, relative_path, flags, &new_file);
        if (result != VFS_SUCCESS) {
            log_error("VFS", "Filesystem-specific open failed for %s: %d", path, result);
            free(new_file);
            mutex_unlock(&mount->lock);
            return result;
        }
    }
    
    // Release the mount lock now that the file is open
    mutex_unlock(&mount->lock);
    
    /* Return the file handle */
    *file = new_file;
    
    return VFS_SUCCESS;
}

/* Close a file */
int vfs_close(vfs_file_t* file) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!file) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Call filesystem-specific close handler */
    if (file->mount && file->mount->fs_type && file->mount->fs_type->close) {
        int result = file->mount->fs_type->close(file);
        if (result != VFS_SUCCESS) {
            log_error("VFS", "Filesystem-specific close failed: %d", result);
            /* Continue with cleanup anyway */
        }
    }
    
    /* Free the file handle */
    free(file);
    
    return VFS_SUCCESS;
}

/* Read from a file */
int vfs_read(vfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read) {
    int result;
    
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!file || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Check if file was opened for reading */
    if (!(file->flags & VFS_OPEN_READ)) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Lock the file for thread safety during read
    mutex_lock(&file->lock);
    
    /* Call filesystem-specific read handler */
    if (file->mount && file->mount->fs_type && file->mount->fs_type->read) {
        result = file->mount->fs_type->read(file, buffer, size, bytes_read);
    } else {
        if (bytes_read) {
            *bytes_read = 0;
        }
        result = VFS_ERR_UNSUPPORTED;
    }
    
    // Unlock the file
    mutex_unlock(&file->lock);
    
    return result;
}

/* Write to a file */
int vfs_write(vfs_file_t* file, const void* buffer, uint32_t size, uint32_t* bytes_written) {
    int result;
    
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!file || !buffer) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Check if file was opened for writing */
    if (!(file->flags & (VFS_OPEN_WRITE | VFS_OPEN_APPEND))) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Lock the file for thread safety during write
    mutex_lock(&file->lock);
    
    /* Call filesystem-specific write handler */
    if (file->mount && file->mount->fs_type && file->mount->fs_type->write) {
        result = file->mount->fs_type->write(file, buffer, size, bytes_written);
    } else {
        if (bytes_written) {
            *bytes_written = 0;
        }
        result = VFS_ERR_UNSUPPORTED;
    }
    
    // Unlock the file
    mutex_unlock(&file->lock);
    
    return result;
}

/* Seek within a file */
int vfs_seek(vfs_file_t* file, int64_t offset, int whence) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!file) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Call filesystem-specific seek handler */
    if (file->mount && file->mount->fs_type && file->mount->fs_type->seek) {
        return file->mount->fs_type->seek(file, offset, whence);
    }
    
    /* Basic fallback implementation if filesystem doesn't provide seek */
    if (whence == VFS_SEEK_SET) {
        if (offset < 0) {
            return VFS_ERR_INVALID_ARG;
        }
        file->position = offset;
    } else if (whence == VFS_SEEK_CUR) {
        if ((offset < 0 && (uint64_t)(-offset) > file->position) ||
            (offset > 0 && file->position + offset < file->position)) {
            return VFS_ERR_INVALID_ARG;
        }
        file->position += offset;
    } else if (whence == VFS_SEEK_END) {
        /* Cannot implement without knowing file size */
        return VFS_ERR_UNSUPPORTED;
    } else {
        return VFS_ERR_INVALID_ARG;
    }
    
    return VFS_SUCCESS;
}

/* Get current position in file */
int vfs_tell(vfs_file_t* file, uint64_t* offset) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!file || !offset) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Call filesystem-specific tell handler */
    if (file->mount && file->mount->fs_type && file->mount->fs_type->tell) {
        return file->mount->fs_type->tell(file, offset);
    }
    
    /* Basic fallback implementation */
    *offset = file->position;
    
    return VFS_SUCCESS;
}

/* Flush file buffers */
int vfs_flush(vfs_file_t* file) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!file) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Call filesystem-specific flush handler */
    if (file->mount && file->mount->fs_type && file->mount->fs_type->flush) {
        return file->mount->fs_type->flush(file);
    }
    
    /* No flush handler, assume success */
    return VFS_SUCCESS;
}

/* Get file status */
int vfs_stat(const char* path, vfs_stat_t* stat) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path || !stat) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Extract the relative path within the filesystem */
    char relative_path[VFS_MAX_PATH];
    vfs_extract_relative_path(path, mount->mount_point, relative_path, VFS_MAX_PATH);
    
    /* Call filesystem-specific stat handler */
    if (mount->fs_type && mount->fs_type->stat) {
        return mount->fs_type->stat(mount, relative_path, stat);
    }
    
    return VFS_ERR_UNSUPPORTED;
}

/* Open a directory */
int vfs_opendir(const char* path, vfs_file_t** dir) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path || !dir) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Extract the relative path within the filesystem */
    char relative_path[VFS_MAX_PATH];
    vfs_extract_relative_path(path, mount->mount_point, relative_path, VFS_MAX_PATH);
    
    /* Allocate a file handle for the directory */
    vfs_file_t* new_dir = (vfs_file_t*)malloc(sizeof(vfs_file_t));
    if (!new_dir) {
        return VFS_ERR_NO_SPACE;
    }
    
    /* Initialize the directory handle */
    new_dir->mount = mount;
    vfs_copy_path(new_dir->path, relative_path, VFS_MAX_PATH);
    new_dir->flags = 0;  /* No special flags for directories */
    new_dir->position = 0;
    new_dir->fs_data = NULL;
    
    /* Call filesystem-specific opendir handler */
    if (mount->fs_type && mount->fs_type->opendir) {
        int result = mount->fs_type->opendir(mount, relative_path, &new_dir);
        if (result != VFS_SUCCESS) {
            free(new_dir);
            return result;
        }
    } else {
        free(new_dir);
        return VFS_ERR_UNSUPPORTED;
    }
    
    /* Return the directory handle */
    *dir = new_dir;
    
    return VFS_SUCCESS;
}

/* Read directory entry */
int vfs_readdir(vfs_file_t* dir, vfs_dirent_t* dirent) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!dir || !dirent) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Call filesystem-specific readdir handler */
    if (dir->mount && dir->mount->fs_type && dir->mount->fs_type->readdir) {
        return dir->mount->fs_type->readdir(dir, dirent);
    }
    
    return VFS_ERR_UNSUPPORTED;
}

/* Close a directory */
int vfs_closedir(vfs_file_t* dir) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!dir) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Call filesystem-specific closedir handler */
    if (dir->mount && dir->mount->fs_type && dir->mount->fs_type->closedir) {
        int result = dir->mount->fs_type->closedir(dir);
        if (result != VFS_SUCCESS) {
            /* Continue with cleanup anyway */
        }
    }
    
    /* Free the directory handle */
    free(dir);
    
    return VFS_SUCCESS;
}

/* Create a directory */
int vfs_mkdir(const char* path) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Extract the relative path within the filesystem */
    char relative_path[VFS_MAX_PATH];
    vfs_extract_relative_path(path, mount->mount_point, relative_path, VFS_MAX_PATH);
    
    /* Call filesystem-specific mkdir handler */
    if (mount->fs_type && mount->fs_type->mkdir) {
        return mount->fs_type->mkdir(mount, relative_path);
    }
    
    return VFS_ERR_UNSUPPORTED;
}

/* Remove a directory */
int vfs_rmdir(const char* path) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Extract the relative path within the filesystem */
    char relative_path[VFS_MAX_PATH];
    vfs_extract_relative_path(path, mount->mount_point, relative_path, VFS_MAX_PATH);
    
    /* Call filesystem-specific rmdir handler */
    if (mount->fs_type && mount->fs_type->rmdir) {
        return mount->fs_type->rmdir(mount, relative_path);
    }
    
    return VFS_ERR_UNSUPPORTED;
}

/* Delete a file */
int vfs_unlink(const char* path) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Extract the relative path within the filesystem */
    char relative_path[VFS_MAX_PATH];
    vfs_extract_relative_path(path, mount->mount_point, relative_path, VFS_MAX_PATH);
    
    /* Call filesystem-specific unlink handler */
    if (mount->fs_type && mount->fs_type->unlink) {
        return mount->fs_type->unlink(mount, relative_path);
    }
    
    return VFS_ERR_UNSUPPORTED;
}

/* Rename a file */
int vfs_rename(const char* oldpath, const char* newpath) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!oldpath || !newpath) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for the old path */
    vfs_mount_t* old_mount = vfs_find_mount_point(oldpath);
    if (!old_mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Find the mount point for the new path */
    vfs_mount_t* new_mount = vfs_find_mount_point(newpath);
    if (!new_mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Check if both paths are on the same filesystem */
    if (old_mount != new_mount) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Extract the relative paths within the filesystem */
    char old_relative[VFS_MAX_PATH];
    char new_relative[VFS_MAX_PATH];
    vfs_extract_relative_path(oldpath, old_mount->mount_point, old_relative, VFS_MAX_PATH);
    vfs_extract_relative_path(newpath, new_mount->mount_point, new_relative, VFS_MAX_PATH);
    
    /* Call filesystem-specific rename handler */
    if (old_mount->fs_type && old_mount->fs_type->rename) {
        return old_mount->fs_type->rename(old_mount, old_relative, new_relative);
    }
    
    return VFS_ERR_UNSUPPORTED;
}

/* Get filesystem statistics */
int vfs_statfs(const char* path, uint64_t* total, uint64_t* free) {
    if (!vfs_initialized) {
        return VFS_ERR_UNKNOWN;
    }
    
    if (!path || !total || !free) {
        return VFS_ERR_INVALID_ARG;
    }
    
    /* Find the mount point for this path */
    vfs_mount_t* mount = vfs_find_mount_point(path);
    if (!mount) {
        return VFS_ERR_NOT_FOUND;
    }
    
    /* Call filesystem-specific statfs handler */
    if (mount->fs_type && mount->fs_type->statfs) {
        return mount->fs_type->statfs(mount, total, free);
    }
    
    return VFS_ERR_UNSUPPORTED;
}