#ifndef EXFAT_H
#define EXFAT_H

#include <stdint.h>

/* exFAT Success and Error Codes */
#define EXFAT_SUCCESS          0
#define EXFAT_ERR_NOT_FOUND   -1
#define EXFAT_ERR_EXISTS      -2
#define EXFAT_ERR_IO_ERROR    -3
#define EXFAT_ERR_NO_SPACE    -4
#define EXFAT_ERR_INVALID_ARG -5
#define EXFAT_ERR_BAD_FORMAT  -6
#define EXFAT_ERR_PERMISSION  -7
#define EXFAT_ERR_NOT_DIR     -8
#define EXFAT_ERR_NOT_FILE    -9
#define EXFAT_ERR_NOT_EMPTY   -10
#define EXFAT_ERR_CORRUPTED   -11
#define EXFAT_ERR_UNSUPPORTED -12

/* exFAT Attributes */
#define EXFAT_ATTR_READ_ONLY  0x01
#define EXFAT_ATTR_HIDDEN     0x02
#define EXFAT_ATTR_SYSTEM     0x04
#define EXFAT_ATTR_VOLUME_ID  0x08
#define EXFAT_ATTR_DIRECTORY  0x10
#define EXFAT_ATTR_ARCHIVE    0x20

/* exFAT Write Flags */
#define EXFAT_WRITE_CREATE    0x01
#define EXFAT_WRITE_TRUNCATE  0x02
#define EXFAT_WRITE_APPEND    0x04
#define EXFAT_WRITE_SYNC      0x08

/* exFAT Constants */
#define EXFAT_MAX_ENTRIES     64

/* exFAT Filesystem Information Structure */
typedef struct {
    char volume_label[12];
    uint32_t volume_id;
    uint16_t bytes_per_sector;
    uint32_t cluster_size;
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint32_t root_dir_cluster;
} exfat_fs_info_t;

/* exFAT File Entry Structure */
typedef struct {
    char name[256];             /* File name */
    uint32_t size;              /* File size in bytes */
    uint8_t attributes;         /* File attributes */
    uint32_t first_cluster;     /* First cluster of file data */
    uint16_t create_date;       /* Creation date */
    uint16_t create_time;       /* Creation time */
    uint16_t last_modified_date; /* Last modified date */
    uint16_t last_modified_time; /* Last modified time */
    uint16_t last_access_date;   /* Last access date */
} exfat_file_entry_t;

/* Initialize exFAT filesystem */
int exfat_init(const char* device);

/* Check if a file exists and return type */
int exfat_file_exists(const char* path);

/* Get the size of a file */
int exfat_get_file_size(const char* path);

/* Convert a path to a cluster number */
int exfat_path_to_cluster(const char* path);

/* Read a file from the filesystem */
int exfat_read_file(const char* path, void* buffer, uint32_t size);

/* Write a file to the filesystem */
int exfat_write_file(const char* path, const void* buffer, uint32_t size, uint32_t flags);

/* List directory contents */
int exfat_list_directory(const char* path, exfat_file_entry_t* entries, uint32_t max_entries);

/* Create a directory */
int exfat_mkdir(const char* path, uint16_t mode);

/* Remove a file or directory */
int exfat_remove(const char* path);

/* Rename a file or directory */
int exfat_rename(const char* old_path, const char* new_path);

/* Get filesystem information */
int exfat_get_fs_info(exfat_fs_info_t* info);

/* Read a cluster from disk */
int exfat_read_cluster(uint32_t cluster, void* buffer);

/* Write a cluster to disk */
int exfat_write_cluster(uint32_t cluster, const void* buffer);

/* Allocate a cluster for a new file or directory */
int exfat_allocate_cluster(void);

/* Free a previously allocated cluster */
int exfat_free_cluster(uint32_t cluster);

#endif /* EXFAT_H */