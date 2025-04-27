#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>

// Error codes for filesystem operations
#define FAT12_SUCCESS          0
#define FAT12_ERR_NOT_FOUND   -1
#define FAT12_ERR_NO_SPACE    -2
#define FAT12_ERR_BAD_FORMAT  -3
#define FAT12_ERR_IO_ERROR    -4
#define FAT12_ERR_INVALID_ARG -5

// File attributes
#define FAT12_ATTR_READ_ONLY  0x01
#define FAT12_ATTR_HIDDEN     0x02
#define FAT12_ATTR_SYSTEM     0x04
#define FAT12_ATTR_VOLUME_ID  0x08
#define FAT12_ATTR_DIRECTORY  0x10
#define FAT12_ATTR_ARCHIVE    0x20

// File entry structure for directory listing
typedef struct {
    char name[13]; // 8.3 filename + null terminator
    uint8_t attributes;
    uint32_t size;
    uint16_t cluster;
    uint16_t create_date;
    uint16_t create_time;
    uint16_t last_access_date;
    uint16_t last_modified_date;
    uint16_t last_modified_time;
} fat12_file_entry_t;

// Initialize the FAT12 filesystem
void fat12_init();

// Read file data into a buffer
// Returns: Number of bytes read or an error code (negative value)
int fat12_read_file(const char* filename, char* buffer, int size);

// List files in a directory
// Returns: Number of entries found or an error code (negative value)
int fat12_list_directory(const char* path, fat12_file_entry_t* entries, int max_entries);

// Check if a file exists
// Returns: 1 if file exists, 0 if not, or an error code (negative value)
int fat12_file_exists(const char* filename);

// Get file size
// Returns: File size or an error code (negative value)
int fat12_get_file_size(const char* filename);

// Internal helper function to read a sector from disk
int read_sector(uint32_t sector, void* buffer);

#endif // FAT12_H