#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>

// Error codes for filesystem operations
#define ISO9660_SUCCESS         0
#define ISO9660_ERR_NOT_FOUND  -1
#define ISO9660_ERR_BAD_FORMAT -2
#define ISO9660_ERR_IO_ERROR   -3
#define ISO9660_ERR_INVALID_ARG -4

// File attributes
#define ISO9660_ATTR_HIDDEN     0x01
#define ISO9660_ATTR_DIRECTORY  0x02
#define ISO9660_ATTR_ASSOCIATED 0x04
#define ISO9660_ATTR_RECORD     0x08
#define ISO9660_ATTR_PROTECTED  0x10
#define ISO9660_ATTR_MULTI_EXT  0x80

// ISO9660 standard identifiers
#define ISO9660_STANDARD_ID "CD001"

// Volume descriptor types
#define ISO9660_BOOT_RECORD          0
#define ISO9660_PRIMARY_DESCRIPTOR   1
#define ISO9660_SUPPLEMENTARY_DESC   2
#define ISO9660_VOLUME_PARTITION     3
#define ISO9660_TERMINATOR           255

// Fixed sizes
#define ISO9660_SECTOR_SIZE 2048
#define ISO9660_LOGICAL_BLOCK_SIZE 2048

// File entry structure for directory listing
typedef struct {
    char     name[256];         // Filename
    uint8_t  attributes;        // File attributes
    uint32_t size;              // File size in bytes
    uint32_t location;          // Starting sector
    uint8_t  recording_date[7]; // YY MM DD HH MM SS TZ
} iso9660_file_entry_t;

// Volume descriptor structure
typedef struct {
    uint8_t  type;
    char     id[5];            // "CD001"
    uint8_t  version;
    uint8_t  reserved1;
    char     system_id[32];
    char     volume_id[32];
    uint8_t  reserved2[8];
    uint32_t volume_space_size[2]; // Little endian, Big endian
    uint8_t  reserved3[32];
    uint16_t volume_set_size[2];
    uint16_t volume_sequence_number[2];
    uint16_t logical_block_size[2];
    uint32_t path_table_size[2];
    uint32_t type_l_path_table;
    uint32_t opt_type_l_path_table;
    uint32_t type_m_path_table;
    uint32_t opt_type_m_path_table;
    uint8_t  root_directory_record[34];
    char     volume_set_id[128];
    char     publisher_id[128];
    char     preparer_id[128];
    char     application_id[128];
    char     copyright_file_id[38];
    char     abstract_file_id[36];
    char     bibliographic_file_id[37];
    uint8_t  creation_date[17];
    uint8_t  modification_date[17];
    uint8_t  expiration_date[17];
    uint8_t  effective_date[17];
    uint8_t  file_structure_version;
    uint8_t  reserved4;
    uint8_t  application_data[512];
    uint8_t  reserved5[653];
} __attribute__((packed)) iso9660_volume_descriptor_t;

// Directory record structure
typedef struct {
    uint8_t  length;
    uint8_t  ext_attr_length;
    uint32_t extent_location[2];  // Little endian, Big endian
    uint32_t data_length[2];      // Little endian, Big endian
    uint8_t  recording_date[7];   // YY MM DD HH MM SS TZ
    uint8_t  file_flags;
    uint8_t  file_unit_size;
    uint8_t  interleave_gap_size;
    uint16_t volume_sequence_number[2];
    uint8_t  filename_length;
    char     filename[];          // Variable length
} __attribute__((packed)) iso9660_directory_record_t;

// Initialize the ISO9660 filesystem
int iso9660_init(const char* device);

// Read file data into a buffer
// Returns: Number of bytes read or an error code (negative value)
int iso9660_read_file(const char* path, char* buffer, int size);

// List files in a directory
// Returns: Number of entries found or an error code (negative value)
int iso9660_list_directory(const char* path, iso9660_file_entry_t* entries, int max_entries);

// Check if a file exists
// Returns: 1 if file exists, 0 if not, or an error code (negative value)
int iso9660_file_exists(const char* path);

// Get file size
// Returns: File size or an error code (negative value)
int iso9660_get_file_size(const char* path);

// Get boot information from El Torito boot record (if present)
// Returns: 0 on success or an error code (negative value)
int iso9660_get_boot_info(uint32_t* boot_catalog_sector, uint32_t* boot_image_sector, uint32_t* boot_image_size);

// Read raw sector data
// Returns: Number of bytes read or an error code (negative value)
int iso9660_read_sector(uint32_t sector, void* buffer, uint32_t count);

// Parse a Joliet or Rock Ridge filename if present
// Returns: Length of the parsed name or 0 if not found
int iso9660_parse_extended_name(const iso9660_directory_record_t* record, char* buffer, int size);

#endif // ISO9660_H