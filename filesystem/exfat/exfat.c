#include "exfat.h"
#include "../../kernel/logging/log.h"
#include <string.h>
#include <stdlib.h>

/* Global state */
static uint8_t* disk_image = NULL;
static uint32_t disk_size = 0;
static exfat_fs_info_t fs_info;
static int exfat_initialized = 0;

/* Helper function to parse a path into components */
static int parse_path(const char* path, char* dir_path, char* filename) {
    const char* last_slash = strrchr(path, '/');
    
    if (last_slash) {
        /* Copy the directory part */
        size_t dir_len = last_slash - path;
        if (dir_len >= 255) return EXFAT_ERR_INVALID_ARG;
        
        strncpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
        
        /* If dir_path is empty, it's the root directory */
        if (dir_path[0] == '\0') {
            strcpy(dir_path, "/");
        }
        
        /* Copy the filename part */
        strncpy(filename, last_slash + 1, 255);
        filename[255] = '\0';
    } else {
        /* No slash - treat entire path as filename in root directory */
        strcpy(dir_path, "/");
        strncpy(filename, path, 255);
        filename[255] = '\0';
    }
    
    return EXFAT_SUCCESS;
}

/* Initialize exFAT filesystem */
int exfat_init(const char* device) {
    if (exfat_initialized) {
        log_info("exFAT", "exFAT filesystem already initialized");
        return EXFAT_SUCCESS;
    }
    
    log_info("exFAT", "Initializing exFAT filesystem on device %s", device ? device : "default");
    
    /* In a real OS, we'd access the device here.
     * For simplicity, we'll create a simulated disk image */
    disk_size = 16 * 1024 * 1024; /* 16MB disk */
    disk_image = (uint8_t*)malloc(disk_size);
    
    if (!disk_image) {
        log_error("exFAT", "Failed to allocate disk image");
        return EXFAT_ERR_NO_SPACE;
    }
    
    /* Initialize disk image as an exFAT filesystem */
    memset(disk_image, 0, disk_size);
    
    /* Set up filesystem information */
    strcpy(fs_info.volume_label, "EXFAT_DISK");
    fs_info.volume_id = 0x12345678;
    fs_info.bytes_per_sector = 512;
    fs_info.cluster_size = 4096; /* 8 sectors per cluster */
    fs_info.total_clusters = (disk_size / fs_info.cluster_size);
    fs_info.free_clusters = fs_info.total_clusters - 10; /* Reserve some clusters for system structures */
    fs_info.root_dir_cluster = 2; /* Root directory starts at cluster 2 */
    
    /* Create a simple directory structure in the root directory */
    exfat_file_entry_t root_entries[3];
    
    /* README.TXT file */
    strcpy(root_entries[0].name, "README.TXT");
    root_entries[0].size = 37;
    root_entries[0].attributes = EXFAT_ATTR_ARCHIVE;
    root_entries[0].first_cluster = 3;
    root_entries[0].create_date = 0x5345; /* Some date value */
    root_entries[0].create_time = 0x6123; /* Some time value */
    root_entries[0].last_modified_date = 0x5345;
    root_entries[0].last_modified_time = 0x6123;
    root_entries[0].last_access_date = 0x5345;
    
    /* Content for README.TXT */
    char* readme_content = "uintOS - A simple educational OS\r\n";
    memcpy(&disk_image[3 * fs_info.cluster_size], readme_content, strlen(readme_content));
    
    /* SYSTEM directory */
    strcpy(root_entries[1].name, "SYSTEM");
    root_entries[1].size = 0; /* Directories have size 0 */
    root_entries[1].attributes = EXFAT_ATTR_DIRECTORY;
    root_entries[1].first_cluster = 4;
    root_entries[1].create_date = 0x5345;
    root_entries[1].create_time = 0x6123;
    root_entries[1].last_modified_date = 0x5345;
    root_entries[1].last_modified_time = 0x6123;
    root_entries[1].last_access_date = 0x5345;
    
    /* LOGS directory */
    strcpy(root_entries[2].name, "LOGS");
    root_entries[2].size = 0; /* Directories have size 0 */
    root_entries[2].attributes = EXFAT_ATTR_DIRECTORY;
    root_entries[2].first_cluster = 5;
    root_entries[2].create_date = 0x5345;
    root_entries[2].create_time = 0x6123;
    root_entries[2].last_modified_date = 0x5345;
    root_entries[2].last_modified_time = 0x6123;
    root_entries[2].last_access_date = 0x5345;
    
    /* Store directory entries in root directory cluster */
    memcpy(&disk_image[fs_info.root_dir_cluster * fs_info.cluster_size], 
           root_entries, sizeof(exfat_file_entry_t) * 3);
    
    /* Create a file in SYSTEM directory */
    exfat_file_entry_t system_entries[1];
    strcpy(system_entries[0].name, "CONFIG.SYS");
    system_entries[0].size = 15;
    system_entries[0].attributes = EXFAT_ATTR_ARCHIVE;
    system_entries[0].first_cluster = 6;
    system_entries[0].create_date = 0x5345;
    system_entries[0].create_time = 0x6123;
    system_entries[0].last_modified_date = 0x5345;
    system_entries[0].last_modified_time = 0x6123;
    system_entries[0].last_access_date = 0x5345;
    
    /* Store directory entries in SYSTEM directory cluster */
    memcpy(&disk_image[4 * fs_info.cluster_size], 
           system_entries, sizeof(exfat_file_entry_t));
    
    /* Content for CONFIG.SYS */
    char* config_content = "SYSTEM CONFIG\r\n";
    memcpy(&disk_image[6 * fs_info.cluster_size], config_content, strlen(config_content));
    
    /* Create a file in LOGS directory */
    exfat_file_entry_t logs_entries[1];
    strcpy(logs_entries[0].name, "SYSTEM.LOG");
    logs_entries[0].size = 22;
    logs_entries[0].attributes = EXFAT_ATTR_ARCHIVE;
    logs_entries[0].first_cluster = 7;
    logs_entries[0].create_date = 0x5345;
    logs_entries[0].create_time = 0x6123;
    logs_entries[0].last_modified_date = 0x5345;
    logs_entries[0].last_modified_time = 0x6123;
    logs_entries[0].last_access_date = 0x5345;
    
    /* Store directory entries in LOGS directory cluster */
    memcpy(&disk_image[5 * fs_info.cluster_size], 
           logs_entries, sizeof(exfat_file_entry_t));
    
    /* Content for SYSTEM.LOG */
    char* log_content = "System startup log...\r\n";
    memcpy(&disk_image[7 * fs_info.cluster_size], log_content, strlen(log_content));
    
    exfat_initialized = 1;
    log_info("exFAT", "exFAT filesystem initialized successfully");
    
    return EXFAT_SUCCESS;
}

/* Check if a file exists and return type */
int exfat_file_exists(const char* path) {
    if (!exfat_initialized || !path) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* For root directory */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        return 1; /* Root directory always exists */
    }
    
    /* Get directory and filename */
    char dir_path[256];
    char filename[256];
    
    int result = parse_path(path, dir_path, filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Find parent directory's cluster */
    int dir_cluster;
    if (strcmp(dir_path, "/") == 0) {
        dir_cluster = fs_info.root_dir_cluster;
    } else {
        dir_cluster = exfat_path_to_cluster(dir_path);
    }
    
    if (dir_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Read directory entries */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(dir_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Search for file */
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, filename) == 0) {
            return 1;
        }
    }
    
    return EXFAT_ERR_NOT_FOUND;
}

/* Get the size of a file */
int exfat_get_file_size(const char* path) {
    if (!exfat_initialized || !path) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Root directory has no size */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        return 0;
    }
    
    /* Get directory and filename */
    char dir_path[256];
    char filename[256];
    
    int result = parse_path(path, dir_path, filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Find parent directory's cluster */
    int dir_cluster;
    if (strcmp(dir_path, "/") == 0) {
        dir_cluster = fs_info.root_dir_cluster;
    } else {
        dir_cluster = exfat_path_to_cluster(dir_path);
    }
    
    if (dir_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Read directory entries */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(dir_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Search for file */
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, filename) == 0) {
            return entries[i].size;
        }
    }
    
    return EXFAT_ERR_NOT_FOUND;
}

/* Convert a path to a cluster number */
int exfat_path_to_cluster(const char* path) {
    if (!exfat_initialized || !path) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Handle root directory */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        return fs_info.root_dir_cluster;
    }
    
    /* Skip leading slash if present */
    if (path[0] == '/') {
        path++;
    }
    
    /* Start from root directory */
    int current_cluster = fs_info.root_dir_cluster;
    
    /* Parse path components */
    char component[256];
    int i = 0, j = 0;
    
    while (path[i]) {
        if (path[i] == '/') {
            /* End of component */
            component[j] = '\0';
            j = 0;
            
            /* Find this component in the current directory */
            exfat_file_entry_t entries[EXFAT_MAX_ENTRIES];
            char current_path[256] = "/";
            int found = 0;
            
            int count = exfat_list_directory(current_path, entries, EXFAT_MAX_ENTRIES);
            if (count < 0) {
                return count;
            }
            
            for (int k = 0; k < count; k++) {
                if (strcmp(entries[k].name, component) == 0) {
                    if (!(entries[k].attributes & EXFAT_ATTR_DIRECTORY)) {
                        return EXFAT_ERR_NOT_DIR;
                    }
                    
                    current_cluster = entries[k].first_cluster;
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                return EXFAT_ERR_NOT_FOUND;
            }
            
            i++; /* Skip the slash */
        } else {
            /* Add character to current component */
            component[j++] = path[i++];
            if (j >= 255) {
                return EXFAT_ERR_INVALID_ARG;
            }
        }
    }
    
    /* Handle the last component */
    if (j > 0) {
        component[j] = '\0';
        
        /* Find this component in the current directory */
        exfat_file_entry_t entries[EXFAT_MAX_ENTRIES];
        char current_path[256] = "/";
        int found = 0;
        
        int count = exfat_list_directory(current_path, entries, EXFAT_MAX_ENTRIES);
        if (count < 0) {
            return count;
        }
        
        for (int k = 0; k < count; k++) {
            if (strcmp(entries[k].name, component) == 0) {
                current_cluster = entries[k].first_cluster;
                found = 1;
                break;
            }
        }
        
        if (!found) {
            return EXFAT_ERR_NOT_FOUND;
        }
    }
    
    return current_cluster;
}

/* Read a file from the filesystem */
int exfat_read_file(const char* path, void* buffer, uint32_t size) {
    if (!exfat_initialized || !path || !buffer) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Get directory and filename */
    char dir_path[256];
    char filename[256];
    
    int result = parse_path(path, dir_path, filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Find parent directory's cluster */
    int dir_cluster;
    if (strcmp(dir_path, "/") == 0) {
        dir_cluster = fs_info.root_dir_cluster;
    } else {
        dir_cluster = exfat_path_to_cluster(dir_path);
    }
    
    if (dir_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Read directory entries */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(dir_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Search for file */
    int file_found = 0;
    uint32_t file_size = 0;
    uint32_t file_cluster = 0;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, filename) == 0) {
            file_found = 1;
            file_size = entries[i].size;
            file_cluster = entries[i].first_cluster;
            break;
        }
    }
    
    if (!file_found) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Check if the file is a directory */
    if (file_size == 0 && file_cluster > 0) {
        return EXFAT_ERR_NOT_FILE;
    }
    
    /* Check the buffer size */
    if (size < file_size) {
        return EXFAT_ERR_NO_SPACE;
    }
    
    /* Read data from the file's clusters */
    memcpy(buffer, &disk_image[file_cluster * fs_info.cluster_size], file_size);
    
    return file_size;
}

/* Write a file to the filesystem */
int exfat_write_file(const char* path, const void* buffer, uint32_t size, uint32_t flags) {
    if (!exfat_initialized || !path || (!buffer && size > 0)) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Get directory and filename */
    char dir_path[256];
    char filename[256];
    
    int result = parse_path(path, dir_path, filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Find parent directory's cluster */
    int dir_cluster;
    if (strcmp(dir_path, "/") == 0) {
        dir_cluster = fs_info.root_dir_cluster;
    } else {
        dir_cluster = exfat_path_to_cluster(dir_path);
    }
    
    if (dir_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Read directory entries */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(dir_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Search for existing file */
    int file_found = 0;
    uint32_t file_size = 0;
    uint32_t file_cluster = 0;
    int file_index = -1;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, filename) == 0) {
            file_found = 1;
            file_size = entries[i].size;
            file_cluster = entries[i].first_cluster;
            file_index = i;
            break;
        }
    }
    
    /* Handle file creation */
    if (!file_found && !(flags & EXFAT_WRITE_CREATE)) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Allocate a new cluster if needed */
    if (!file_found || (flags & EXFAT_WRITE_TRUNCATE)) {
        file_cluster = exfat_allocate_cluster();
        if (file_cluster <= 0) {
            return EXFAT_ERR_NO_SPACE;
        }
    }
    
    /* Write data to the file's clusters */
    memcpy(&disk_image[file_cluster * fs_info.cluster_size], buffer, size);
    
    /* Update directory entry */
    if (!file_found) {
        /* Create new entry */
        exfat_file_entry_t new_entry;
        strcpy(new_entry.name, filename);
        new_entry.size = size;
        new_entry.attributes = EXFAT_ATTR_ARCHIVE;
        new_entry.first_cluster = file_cluster;
        new_entry.create_date = 0x5345;  /* Some date value */
        new_entry.create_time = 0x6123;  /* Some time value */
        new_entry.last_modified_date = 0x5345;
        new_entry.last_modified_time = 0x6123;
        new_entry.last_access_date = 0x5345;
        
        /* Add entry to directory */
        memcpy(&disk_image[dir_cluster * fs_info.cluster_size + count * sizeof(exfat_file_entry_t)],
               &new_entry, sizeof(exfat_file_entry_t));
        
    } else {
        /* Update existing entry */
        entries[file_index].size = size;
        entries[file_index].first_cluster = file_cluster;
        entries[file_index].last_modified_date = 0x5345;
        entries[file_index].last_modified_time = 0x6123;
        
        /* Write back to directory */
        memcpy(&disk_image[dir_cluster * fs_info.cluster_size],
               entries, count * sizeof(exfat_file_entry_t));
    }
    
    return size;
}

/* List directory contents */
int exfat_list_directory(const char* path, exfat_file_entry_t* entries, uint32_t max_entries) {
    if (!exfat_initialized || !path || !entries || max_entries == 0) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Find directory's cluster */
    int dir_cluster;
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        dir_cluster = fs_info.root_dir_cluster;
    } else {
        dir_cluster = exfat_path_to_cluster(path);
    }
    
    if (dir_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Simulate reading directory entries from disk */
    exfat_file_entry_t* dir_data = (exfat_file_entry_t*)&disk_image[dir_cluster * fs_info.cluster_size];
    
    /* Count entries */
    int entry_count = 0;
    for (uint32_t i = 0; i < (fs_info.cluster_size / sizeof(exfat_file_entry_t)); i++) {
        if (dir_data[i].name[0] != '\0') {
            entry_count++;
        } else {
            break;
        }
    }
    
    /* Copy entries to output buffer */
    int count = (entry_count < max_entries) ? entry_count : max_entries;
    for (int i = 0; i < count; i++) {
        entries[i] = dir_data[i];
    }
    
    return count;
}

/* Create a directory */
int exfat_mkdir(const char* path, uint16_t mode) {
    if (!exfat_initialized || !path) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Check if path already exists */
    int exists = exfat_file_exists(path);
    if (exists > 0) {
        return EXFAT_ERR_EXISTS;
    }
    
    /* Get parent directory and new dir name */
    char parent_path[256];
    char dirname[256];
    
    int result = parse_path(path, parent_path, dirname);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Find parent directory's cluster */
    int parent_cluster;
    if (strcmp(parent_path, "/") == 0) {
        parent_cluster = fs_info.root_dir_cluster;
    } else {
        parent_cluster = exfat_path_to_cluster(parent_path);
    }
    
    if (parent_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Allocate a new cluster for the directory */
    int new_cluster = exfat_allocate_cluster();
    if (new_cluster <= 0) {
        return EXFAT_ERR_NO_SPACE;
    }
    
    /* Initialize directory with . and .. entries (optional for exFAT) */
    memset(&disk_image[new_cluster * fs_info.cluster_size], 0, fs_info.cluster_size);
    
    /* Add new directory entry to parent directory */
    exfat_file_entry_t new_entry;
    strcpy(new_entry.name, dirname);
    new_entry.size = 0;  /* Directories have size 0 */
    new_entry.attributes = EXFAT_ATTR_DIRECTORY;
    new_entry.first_cluster = new_cluster;
    new_entry.create_date = 0x5345;  /* Some date value */
    new_entry.create_time = 0x6123;  /* Some time value */
    new_entry.last_modified_date = 0x5345;
    new_entry.last_modified_time = 0x6123;
    new_entry.last_access_date = 0x5345;
    
    /* Read directory entries to find free slot */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(parent_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Add new entry at the end */
    memcpy(&disk_image[parent_cluster * fs_info.cluster_size + count * sizeof(exfat_file_entry_t)],
           &new_entry, sizeof(exfat_file_entry_t));
    
    return EXFAT_SUCCESS;
}

/* Remove a file or directory */
int exfat_remove(const char* path) {
    if (!exfat_initialized || !path) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Cannot remove root directory */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        return EXFAT_ERR_PERMISSION;
    }
    
    /* Get parent directory and filename */
    char parent_path[256];
    char filename[256];
    
    int result = parse_path(path, parent_path, filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Find parent directory's cluster */
    int parent_cluster;
    if (strcmp(parent_path, "/") == 0) {
        parent_cluster = fs_info.root_dir_cluster;
    } else {
        parent_cluster = exfat_path_to_cluster(parent_path);
    }
    
    if (parent_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Read directory entries */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(parent_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Search for file */
    int file_found = 0;
    int file_index = -1;
    uint32_t file_cluster = 0;
    uint8_t is_directory = 0;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, filename) == 0) {
            file_found = 1;
            file_index = i;
            file_cluster = entries[i].first_cluster;
            is_directory = (entries[i].attributes & EXFAT_ATTR_DIRECTORY) ? 1 : 0;
            break;
        }
    }
    
    if (!file_found) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* If it's a directory, check if it's empty */
    if (is_directory) {
        exfat_file_entry_t dir_entries[20];
        int dir_count = exfat_list_directory(path, dir_entries, 20);
        
        if (dir_count > 0) {
            return EXFAT_ERR_NOT_EMPTY;
        }
    }
    
    /* Mark the cluster as free (in a real filesystem, update FAT table) */
    exfat_free_cluster(file_cluster);
    
    /* Remove entry by shifting all following entries */
    if (file_index < count - 1) {
        memmove(&entries[file_index], &entries[file_index + 1], 
                (count - file_index - 1) * sizeof(exfat_file_entry_t));
    }
    
    /* Mark the last entry as empty */
    memset(&entries[count - 1], 0, sizeof(exfat_file_entry_t));
    
    /* Write back to directory */
    memcpy(&disk_image[parent_cluster * fs_info.cluster_size],
           entries, count * sizeof(exfat_file_entry_t));
    
    return EXFAT_SUCCESS;
}

/* Rename a file or directory */
int exfat_rename(const char* old_path, const char* new_path) {
    if (!exfat_initialized || !old_path || !new_path) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Cannot rename root directory */
    if (strcmp(old_path, "/") == 0 || strcmp(old_path, "") == 0) {
        return EXFAT_ERR_PERMISSION;
    }
    
    /* Check if new path exists */
    int exists = exfat_file_exists(new_path);
    if (exists > 0) {
        return EXFAT_ERR_EXISTS;
    }
    
    /* Get old parent directory and filename */
    char old_parent_path[256];
    char old_filename[256];
    
    int result = parse_path(old_path, old_parent_path, old_filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Get new parent directory and filename */
    char new_parent_path[256];
    char new_filename[256];
    
    result = parse_path(new_path, new_parent_path, new_filename);
    if (result != EXFAT_SUCCESS) {
        return result;
    }
    
    /* Check if the parent directories are the same */
    if (strcmp(old_parent_path, new_parent_path) != 0) {
        /* For simplicity, we don't support moving between directories */
        return EXFAT_ERR_UNSUPPORTED;
    }
    
    /* Find parent directory's cluster */
    int parent_cluster;
    if (strcmp(old_parent_path, "/") == 0) {
        parent_cluster = fs_info.root_dir_cluster;
    } else {
        parent_cluster = exfat_path_to_cluster(old_parent_path);
    }
    
    if (parent_cluster <= 0) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Read directory entries */
    exfat_file_entry_t entries[20]; /* Arbitrary limit */
    int count = exfat_list_directory(old_parent_path, entries, 20);
    
    if (count < 0) {
        return count;
    }
    
    /* Search for file */
    int file_found = 0;
    int file_index = -1;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, old_filename) == 0) {
            file_found = 1;
            file_index = i;
            break;
        }
    }
    
    if (!file_found) {
        return EXFAT_ERR_NOT_FOUND;
    }
    
    /* Update filename in directory entry */
    strcpy(entries[file_index].name, new_filename);
    
    /* Write back to directory */
    memcpy(&disk_image[parent_cluster * fs_info.cluster_size],
           entries, count * sizeof(exfat_file_entry_t));
    
    return EXFAT_SUCCESS;
}

/* Get filesystem information */
int exfat_get_fs_info(exfat_fs_info_t* info) {
    if (!exfat_initialized || !info) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Copy filesystem information */
    memcpy(info, &fs_info, sizeof(exfat_fs_info_t));
    
    return EXFAT_SUCCESS;
}

/* Read a cluster from disk */
int exfat_read_cluster(uint32_t cluster, void* buffer) {
    if (!exfat_initialized || !buffer || cluster < 2 || 
        cluster >= fs_info.total_clusters) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Copy cluster data */
    memcpy(buffer, &disk_image[cluster * fs_info.cluster_size], fs_info.cluster_size);
    
    return EXFAT_SUCCESS;
}

/* Write a cluster to disk */
int exfat_write_cluster(uint32_t cluster, const void* buffer) {
    if (!exfat_initialized || !buffer || cluster < 2 || 
        cluster >= fs_info.total_clusters) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* Copy cluster data */
    memcpy(&disk_image[cluster * fs_info.cluster_size], buffer, fs_info.cluster_size);
    
    return EXFAT_SUCCESS;
}

/* Allocate a cluster for a new file or directory */
int exfat_allocate_cluster(void) {
    if (!exfat_initialized) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* In a real filesystem, we would search the FAT for a free cluster.
     * For this simplified implementation, just use the next available cluster. */
    static uint32_t next_free_cluster = 8; /* First 8 clusters are used already */
    
    if (next_free_cluster >= fs_info.total_clusters) {
        return EXFAT_ERR_NO_SPACE;
    }
    
    uint32_t allocated_cluster = next_free_cluster++;
    fs_info.free_clusters--;
    
    return allocated_cluster;
}

/* Free a previously allocated cluster */
int exfat_free_cluster(uint32_t cluster) {
    if (!exfat_initialized || cluster < 2 || cluster >= fs_info.total_clusters) {
        return EXFAT_ERR_INVALID_ARG;
    }
    
    /* In a real filesystem, mark the cluster as free in the FAT.
     * For this simplified implementation, just clear the cluster data. */
    memset(&disk_image[cluster * fs_info.cluster_size], 0, fs_info.cluster_size);
    fs_info.free_clusters++;
    
    return EXFAT_SUCCESS;
}