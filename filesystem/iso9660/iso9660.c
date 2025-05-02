#include "iso9660.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../kernel/io.h"

// Static variables to store filesystem state
static iso9660_volume_descriptor_t primary_volume_descriptor;
static uint32_t root_directory_extent = 0;
static uint32_t root_directory_size = 0;
static char* device_path = NULL;
static int has_joliet = 0;
static iso9660_volume_descriptor_t joliet_volume_descriptor;

// Forward declarations for internal functions
static int read_raw_sector(uint32_t sector, void* buffer);
static int find_file_in_dir(const char* name, uint32_t dir_sector, uint32_t dir_size, 
                           iso9660_directory_record_t** record, void** sector_buffer);
static int parse_path(const char* path, iso9660_directory_record_t** record, void** buffer);
static int iso9660_name_compare(const char* name, const char* iso_name, int iso_name_len);
static void convert_date(const uint8_t* iso_date, char* output);

int iso9660_init(const char* device) {
    // Save device path for later use
    device_path = (char*)device;
    has_joliet = 0;
    
    // ISO9660 volume descriptors start at sector 16
    uint8_t sector_buffer[ISO9660_SECTOR_SIZE];
    int found_primary_descriptor = 0;
    
    // Read sectors until we find the primary volume descriptor or terminator
    for (uint32_t sector = 16; sector < 32; sector++) {
        if (read_raw_sector(sector, sector_buffer) != 0) {
            return ISO9660_ERR_IO_ERROR;
        }
        
        iso9660_volume_descriptor_t* desc = (iso9660_volume_descriptor_t*)sector_buffer;
        
        // Check standard identifier
        if (strncmp(desc->id, ISO9660_STANDARD_ID, 5) != 0) {
            continue;
        }
        
        // Process based on descriptor type
        if (desc->type == ISO9660_PRIMARY_DESCRIPTOR) {
            // Found primary descriptor, save it
            memcpy(&primary_volume_descriptor, desc, sizeof(iso9660_volume_descriptor_t));
            found_primary_descriptor = 1;
            
            // Get the root directory location and size
            iso9660_directory_record_t* root = (iso9660_directory_record_t*)primary_volume_descriptor.root_directory_record;
            root_directory_extent = root->extent_location[0]; // Little endian
            root_directory_size = root->data_length[0];       // Little endian
        }
        else if (desc->type == ISO9660_SUPPLEMENTARY_DESC) {
            // Check if this is a Joliet descriptor (UCS-2 escape sequence)
            if (desc->reserved3[0] == 0x25 && desc->reserved3[1] == 0x2F) {
                memcpy(&joliet_volume_descriptor, desc, sizeof(iso9660_volume_descriptor_t));
                has_joliet = 1;
                
                // If Joliet is found, use it for better filename support
                iso9660_directory_record_t* root = (iso9660_directory_record_t*)joliet_volume_descriptor.root_directory_record;
                root_directory_extent = root->extent_location[0]; // Little endian
                root_directory_size = root->data_length[0];       // Little endian
            }
        }
        else if (desc->type == ISO9660_TERMINATOR) {
            // Reached end of volume descriptors
            break;
        }
    }
    
    if (!found_primary_descriptor) {
        return ISO9660_ERR_BAD_FORMAT;
    }
    
    return ISO9660_SUCCESS;
}

int iso9660_read_file(const char* path, char* buffer, int size) {
    iso9660_directory_record_t* record = NULL;
    void* sector_buffer = NULL;
    
    // Find the file in the directory structure
    int result = parse_path(path, &record, &sector_buffer);
    if (result < 0) {
        return result;
    }
    
    // Check if it's a directory
    if (record->file_flags & ISO9660_ATTR_DIRECTORY) {
        if (sector_buffer) free(sector_buffer);
        return ISO9660_ERR_INVALID_ARG;
    }
    
    // Calculate how many bytes to read (min of file size and buffer size)
    uint32_t file_size = record->data_length[0]; // Little endian
    uint32_t bytes_to_read = (file_size < (uint32_t)size) ? file_size : (uint32_t)size;
    
    // Get file location
    uint32_t file_sector = record->extent_location[0]; // Little endian
    
    // Free the sector buffer as we don't need it anymore
    if (sector_buffer) free(sector_buffer);
    
    // Calculate how many full sectors to read
    uint32_t full_sectors = bytes_to_read / ISO9660_SECTOR_SIZE;
    uint32_t remaining_bytes = bytes_to_read % ISO9660_SECTOR_SIZE;
    uint32_t bytes_read = 0;
    
    // Read full sectors directly into the output buffer if possible
    if (full_sectors > 0) {
        result = iso9660_read_sector(file_sector, buffer, full_sectors);
        if (result < 0) {
            return result;
        }
        bytes_read = full_sectors * ISO9660_SECTOR_SIZE;
    }
    
    // Read any remaining partial sector
    if (remaining_bytes > 0) {
        uint8_t sector_data[ISO9660_SECTOR_SIZE];
        result = read_raw_sector(file_sector + full_sectors, sector_data);
        if (result < 0) {
            return result;
        }
        
        // Copy the remaining bytes
        memcpy(buffer + bytes_read, sector_data, remaining_bytes);
        bytes_read += remaining_bytes;
    }
    
    return bytes_read;
}

int iso9660_list_directory(const char* path, iso9660_file_entry_t* entries, int max_entries) {
    iso9660_directory_record_t* dir_record = NULL;
    void* dir_sector_buffer = NULL;
    
    // If path is empty or root, use root directory
    if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        // Use root directory directly
        uint8_t root_buffer[ISO9660_SECTOR_SIZE];
        if (read_raw_sector(root_directory_extent, root_buffer) != 0) {
            return ISO9660_ERR_IO_ERROR;
        }
        
        dir_record = (iso9660_directory_record_t*)root_buffer;
    } else {
        // Find the directory in the filesystem
        int result = parse_path(path, &dir_record, &dir_sector_buffer);
        if (result < 0) {
            return result;
        }
        
        // Check if it's a directory
        if (!(dir_record->file_flags & ISO9660_ATTR_DIRECTORY)) {
            if (dir_sector_buffer) free(dir_sector_buffer);
            return ISO9660_ERR_INVALID_ARG;
        }
    }
    
    // Get directory location and size
    uint32_t dir_sector = dir_record->extent_location[0]; // Little endian
    uint32_t dir_size = dir_record->data_length[0];       // Little endian
    
    // Free the sector buffer if we allocated one
    if (dir_sector_buffer) free(dir_sector_buffer);
    
    // Read the directory contents
    uint8_t* directory_buffer = malloc(dir_size);
    if (!directory_buffer) {
        return ISO9660_ERR_IO_ERROR;
    }
    
    // Read all directory sectors
    uint32_t sectors_to_read = (dir_size + ISO9660_SECTOR_SIZE - 1) / ISO9660_SECTOR_SIZE;
    for (uint32_t i = 0; i < sectors_to_read; i++) {
        if (read_raw_sector(dir_sector + i, directory_buffer + i * ISO9660_SECTOR_SIZE) != 0) {
            free(directory_buffer);
            return ISO9660_ERR_IO_ERROR;
        }
    }
    
    // Process the directory entries
    uint32_t offset = 0;
    int entries_found = 0;
    
    while (offset < dir_size && entries_found < max_entries) {
        iso9660_directory_record_t* record = (iso9660_directory_record_t*)(directory_buffer + offset);
        
        // Check for end of records
        if (record->length == 0) {
            // Move to the next sector boundary
            offset = ((offset / ISO9660_SECTOR_SIZE) + 1) * ISO9660_SECTOR_SIZE;
            continue;
        }
        
        // Skip "." and ".." entries
        if (record->filename_length == 1 && (record->filename[0] == 0 || record->filename[0] == 1)) {
            offset += record->length;
            continue;
        }
        
        // Process the entry
        if (entries) {
            // Convert the filename to a normal string
            char name_buffer[256];
            int name_len = 0;
            
            // Try to get extended name (Joliet/Rock Ridge)
            name_len = iso9660_parse_extended_name(record, name_buffer, sizeof(name_buffer));
            
            if (name_len == 0) {
                // No extended name found, use standard ISO9660 name
                for (int i = 0; i < record->filename_length; i++) {
                    if (record->filename[i] == ';') {
                        // Truncate at version separator
                        break;
                    }
                    name_buffer[i] = record->filename[i];
                    name_len++;
                }
                name_buffer[name_len] = '\0';
            }
            
            // Copy entry details
            strncpy(entries[entries_found].name, name_buffer, sizeof(entries[entries_found].name) - 1);
            entries[entries_found].name[sizeof(entries[entries_found].name) - 1] = '\0';
            entries[entries_found].attributes = record->file_flags;
            entries[entries_found].size = record->data_length[0]; // Little endian
            entries[entries_found].location = record->extent_location[0]; // Little endian
            memcpy(entries[entries_found].recording_date, record->recording_date, 7);
        }
        
        entries_found++;
        offset += record->length;
    }
    
    free(directory_buffer);
    return entries_found;
}

int iso9660_file_exists(const char* path) {
    iso9660_directory_record_t* record = NULL;
    void* sector_buffer = NULL;
    
    // Find the file in the directory structure
    int result = parse_path(path, &record, &sector_buffer);
    
    // Free the sector buffer if allocated
    if (sector_buffer) free(sector_buffer);
    
    return (result == 0) ? 1 : 0;
}

int iso9660_get_file_size(const char* path) {
    iso9660_directory_record_t* record = NULL;
    void* sector_buffer = NULL;
    
    // Find the file in the directory structure
    int result = parse_path(path, &record, &sector_buffer);
    if (result < 0) {
        return result;
    }
    
    // Get the file size
    uint32_t file_size = record->data_length[0]; // Little endian
    
    // Free the sector buffer
    if (sector_buffer) free(sector_buffer);
    
    return file_size;
}

int iso9660_get_boot_info(uint32_t* boot_catalog_sector, uint32_t* boot_image_sector, uint32_t* boot_image_size) {
    // This functionality is part of the El Torito specification
    // We need to scan for boot record (type 0) descriptor
    uint8_t sector_buffer[ISO9660_SECTOR_SIZE];
    
    // Read sectors looking for the boot record
    for (uint32_t sector = 16; sector < 32; sector++) {
        if (read_raw_sector(sector, sector_buffer) != 0) {
            return ISO9660_ERR_IO_ERROR;
        }
        
        iso9660_volume_descriptor_t* desc = (iso9660_volume_descriptor_t*)sector_buffer;
        
        // Check standard identifier
        if (strncmp(desc->id, ISO9660_STANDARD_ID, 5) != 0) {
            continue;
        }
        
        // Check if it's a boot record
        if (desc->type == ISO9660_BOOT_RECORD) {
            // Check for El Torito signature
            if (strncmp((char*)desc->reserved3, "EL TORITO SPECIFICATION", 23) == 0) {
                // Found El Torito boot record, get catalog sector
                *boot_catalog_sector = *(uint32_t*)desc->application_data;
                
                // Read the boot catalog
                if (read_raw_sector(*boot_catalog_sector, sector_buffer) != 0) {
                    return ISO9660_ERR_IO_ERROR;
                }
                
                // Parse the boot catalog - simplified for educational purposes
                // In a real implementation, we'd do full validation
                struct {
                    uint8_t header_id;
                    uint8_t platform_id;
                    uint16_t reserved;
                    char id_string[24];
                    uint16_t checksum;
                    uint8_t key55;
                    uint8_t keyAA;
                } __attribute__((packed)) *catalog_header = (void*)sector_buffer;
                
                struct {
                    uint8_t boot_indicator;
                    uint8_t boot_media_type;
                    uint16_t load_segment;
                    uint8_t system_type;
                    uint8_t reserved;
                    uint16_t sector_count;
                    uint32_t load_rba;
                    uint8_t reserved2[20];
                } __attribute__((packed)) *boot_entry = (void*)(sector_buffer + 32);
                
                // Validate header
                if (catalog_header->header_id != 1 || catalog_header->key55 != 0x55 || catalog_header->keyAA != 0xAA) {
                    return ISO9660_ERR_BAD_FORMAT;
                }
                
                // Get boot image info from the first entry
                *boot_image_sector = boot_entry->load_rba;
                *boot_image_size = boot_entry->sector_count * 512; // Size in bytes
                
                return ISO9660_SUCCESS;
            }
        }
        else if (desc->type == ISO9660_TERMINATOR) {
            break;
        }
    }
    
    return ISO9660_ERR_NOT_FOUND;
}

int iso9660_read_sector(uint32_t sector, void* buffer, uint32_t count) {
    // Read multiple contiguous sectors
    for (uint32_t i = 0; i < count; i++) {
        if (read_raw_sector(sector + i, (uint8_t*)buffer + i * ISO9660_SECTOR_SIZE) != 0) {
            return ISO9660_ERR_IO_ERROR;
        }
    }
    
    return count * ISO9660_SECTOR_SIZE;
}

int iso9660_parse_extended_name(const iso9660_directory_record_t* record, char* buffer, int size) {
    // This is a simplified implementation. In a real system, this would handle:
    // 1. Joliet UCS-2 encoded names
    // 2. Rock Ridge extensions
    
    // For Joliet, we'd convert UCS-2 to ASCII/UTF-8
    if (has_joliet) {
        int name_len = record->filename_length / 2; // UCS-2 characters are 2 bytes
        if (name_len > size - 1) {
            name_len = size - 1;
        }
        
        // Simple conversion: take only the second byte of each UCS-2 character
        // In a real implementation, proper Unicode conversion would be used
        for (int i = 0; i < name_len; i++) {
            buffer[i] = record->filename[i*2 + 1]; // Skip the first byte of each UCS-2 char
        }
        
        // Remove version suffix if present (;1)
        for (int i = 0; i < name_len; i++) {
            if (buffer[i] == ';') {
                buffer[i] = '\0';
                name_len = i;
                break;
            }
        }
        
        buffer[name_len] = '\0';
        return name_len;
    }
    
    // For Rock Ridge, we'd extract the NM (name) entry from the System Use area
    // This is a simplified placeholder - real implementation would be more complex
    
    return 0; // No extended name found/supported
}

// Internal function implementations

// Read a raw sector from the device
static int read_raw_sector(uint32_t sector, void* buffer) {
    // In a real OS, this would use block device operations
    // For this educational implementation, we'll simulate an ISO9660 file system in memory
    
    static int initialized = 0;
    static uint8_t iso_image[32 * 1024 * 1024];  // 32MB simulated ISO image
    
    // Initialize ISO image with a basic ISO9660 structure on first use
    if (!initialized) {
        initialized = 1;
        memset(iso_image, 0, sizeof(iso_image));
        
        // Create primary volume descriptor at sector 16
        iso9660_volume_descriptor_t* pvd = (iso9660_volume_descriptor_t*)(iso_image + 16 * ISO9660_SECTOR_SIZE);
        pvd->type = ISO9660_PRIMARY_DESCRIPTOR;
        memcpy(pvd->id, ISO9660_STANDARD_ID, 5);
        pvd->version = 1;
        
        // Fill in volume descriptor fields
        memcpy(pvd->system_id, "UINTOS              ", 16);
        memcpy(pvd->volume_id, "UINTOS_CDROM        ", 16);
        
        // Volume size (in sectors)
        pvd->volume_space_size[0] = 8192; // Little endian
        pvd->volume_space_size[1] = 0x0020; // Big endian (8192)
        
        // Logical block size
        pvd->logical_block_size[0] = ISO9660_LOGICAL_BLOCK_SIZE;
        pvd->logical_block_size[1] = 0x0008; // Big endian
        
        // Path table information
        pvd->path_table_size[0] = 10;  // Size of path table
        pvd->path_table_size[1] = 0x0A00; // Big endian
        pvd->type_l_path_table = 18;  // Sector of path table
        pvd->type_m_path_table = 0x1300; // Big endian
        
        // Create the root directory record
        iso9660_directory_record_t* root_record = (iso9660_directory_record_t*)pvd->root_directory_record;
        root_record->length = 34; // Size of directory record
        root_record->ext_attr_length = 0;
        root_record->extent_location[0] = 20; // Root directory starts at sector 20
        root_record->extent_location[1] = 0x1400; // Big endian
        root_record->data_length[0] = 2048; // Root directory size
        root_record->data_length[1] = 0x0008; // Big endian
        
        // Date - May 2, 2025
        root_record->recording_date[0] = 25;   // Years since 1900
        root_record->recording_date[1] = 5;    // Month
        root_record->recording_date[2] = 2;    // Day
        root_record->recording_date[3] = 12;   // Hour
        root_record->recording_date[4] = 0;    // Minute
        root_record->recording_date[5] = 0;    // Second
        root_record->recording_date[6] = 0;    // Timezone
        
        root_record->file_flags = ISO9660_ATTR_DIRECTORY;
        root_record->file_unit_size = 0;
        root_record->interleave_gap_size = 0;
        root_record->volume_sequence_number[0] = 1;
        root_record->volume_sequence_number[1] = 0x0100; // Big endian
        root_record->filename_length = 1;
        root_record->filename[0] = 0; // Root directory has a filename of 0x00
        
        // Set up Joliet supplementary volume descriptor at sector 17
        iso9660_volume_descriptor_t* svd = (iso9660_volume_descriptor_t*)(iso_image + 17 * ISO9660_SECTOR_SIZE);
        memcpy(svd, pvd, sizeof(iso9660_volume_descriptor_t));
        svd->type = ISO9660_SUPPLEMENTARY_DESC;
        
        // Set Joliet escape sequence
        svd->reserved3[0] = 0x25; // %
        svd->reserved3[1] = 0x2F; // /
        svd->reserved3[2] = 0x45; // E (Level 3)
        
        // Create volume descriptor set terminator at sector 18
        iso9660_volume_descriptor_t* term = (iso9660_volume_descriptor_t*)(iso_image + 18 * ISO9660_SECTOR_SIZE);
        term->type = ISO9660_TERMINATOR;
        memcpy(term->id, ISO9660_STANDARD_ID, 5);
        
        // Create root directory entries at sector 20
        uint8_t* root_dir = iso_image + 20 * ISO9660_SECTOR_SIZE;
        iso9660_directory_record_t* dir_entry = (iso9660_directory_record_t*)root_dir;
        
        // Current directory entry (.)
        dir_entry->length = 34; // Size of directory record
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 20; // Points to itself
        dir_entry->extent_location[1] = 0x1400; // Big endian
        dir_entry->data_length[0] = 2048; // Directory size
        dir_entry->data_length[1] = 0x0008; // Big endian
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = ISO9660_ATTR_DIRECTORY;
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100; // Big endian
        dir_entry->filename_length = 1;
        dir_entry->filename[0] = 0; // '.' - Current directory
        
        // Parent directory entry (..)
        dir_entry = (iso9660_directory_record_t*)(root_dir + dir_entry->length);
        dir_entry->length = 34; // Size of directory record
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 20; // For root, parent points to itself
        dir_entry->extent_location[1] = 0x1400; // Big endian
        dir_entry->data_length[0] = 2048; // Directory size
        dir_entry->data_length[1] = 0x0008; // Big endian
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = ISO9660_ATTR_DIRECTORY;
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100; // Big endian
        dir_entry->filename_length = 1;
        dir_entry->filename[0] = 1; // '..' - Parent directory
        
        // "README.TXT" file entry
        dir_entry = (iso9660_directory_record_t*)(root_dir + 34 + 34);
        dir_entry->length = 44; // Size of directory record + filename length + padding
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 21; // File starts at sector 21
        dir_entry->extent_location[1] = 0x1500; // Big endian
        dir_entry->data_length[0] = 45; // File size
        dir_entry->data_length[1] = 0x2D00; // Big endian
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = 0; // Regular file
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100; // Big endian
        dir_entry->filename_length = 12;
        memcpy(dir_entry->filename, "README.TXT;1", 12); // ISO9660 filenames include version (;1)
        
        // Add "DOCS" directory entry
        dir_entry = (iso9660_directory_record_t*)(root_dir + 34 + 34 + 44);
        dir_entry->length = 38; // Size of directory record + filename length + padding
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 22; // Directory starts at sector 22
        dir_entry->extent_location[1] = 0x1600; // Big endian
        dir_entry->data_length[0] = 2048; // Directory size
        dir_entry->data_length[1] = 0x0008; // Big endian
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = ISO9660_ATTR_DIRECTORY; // Directory
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100; // Big endian
        dir_entry->filename_length = 5;
        memcpy(dir_entry->filename, "DOCS", 4);
        dir_entry->filename[4] = 0; // No extension
        
        // Create README.TXT content at sector 21
        char* readme_content = "Welcome to the uintOS ISO9660 file system!\r\n";
        memcpy(iso_image + 21 * ISO9660_SECTOR_SIZE, readme_content, strlen(readme_content));
        
        // Create DOCS directory entries at sector 22
        uint8_t* docs_dir = iso_image + 22 * ISO9660_SECTOR_SIZE;
        dir_entry = (iso9660_directory_record_t*)docs_dir;
        
        // Current directory entry (.)
        dir_entry->length = 34;
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 22; // Points to itself
        dir_entry->extent_location[1] = 0x1600;
        dir_entry->data_length[0] = 2048;
        dir_entry->data_length[1] = 0x0008;
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = ISO9660_ATTR_DIRECTORY;
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100;
        dir_entry->filename_length = 1;
        dir_entry->filename[0] = 0;
        
        // Parent directory entry (..)
        dir_entry = (iso9660_directory_record_t*)(docs_dir + dir_entry->length);
        dir_entry->length = 34;
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 20; // Points to root directory
        dir_entry->extent_location[1] = 0x1400;
        dir_entry->data_length[0] = 2048;
        dir_entry->data_length[1] = 0x0008;
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = ISO9660_ATTR_DIRECTORY;
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100;
        dir_entry->filename_length = 1;
        dir_entry->filename[0] = 1;
        
        // "MANUAL.TXT" file entry
        dir_entry = (iso9660_directory_record_t*)(docs_dir + 34 + 34);
        dir_entry->length = 44;
        dir_entry->ext_attr_length = 0;
        dir_entry->extent_location[0] = 23; // File starts at sector 23
        dir_entry->extent_location[1] = 0x1700;
        dir_entry->data_length[0] = 32; // File size
        dir_entry->data_length[1] = 0x2000;
        memcpy(dir_entry->recording_date, root_record->recording_date, 7);
        dir_entry->file_flags = 0; // Regular file
        dir_entry->file_unit_size = 0;
        dir_entry->interleave_gap_size = 0;
        dir_entry->volume_sequence_number[0] = 1;
        dir_entry->volume_sequence_number[1] = 0x0100;
        dir_entry->filename_length = 12;
        memcpy(dir_entry->filename, "MANUAL.TXT;1", 12);
        
        // Create MANUAL.TXT content at sector 23
        char* manual_content = "uintOS User Manual\r\nVersion 1.0\r\n";
        memcpy(iso_image + 23 * ISO9660_SECTOR_SIZE, manual_content, strlen(manual_content));
        
        // Create a boot record at sector 19 (El Torito)
        iso9660_volume_descriptor_t* boot_rec = (iso9660_volume_descriptor_t*)(iso_image + 19 * ISO9660_SECTOR_SIZE);
        boot_rec->type = ISO9660_BOOT_RECORD;
        memcpy(boot_rec->id, ISO9660_STANDARD_ID, 5);
        boot_rec->version = 1;
        memcpy(boot_rec->system_id, "EL TORITO SPECIFICATION", 23);
        
        // Boot catalog pointer (sector 24)
        uint32_t* catalog_sector = (uint32_t*)boot_rec->application_data;
        *catalog_sector = 24;
        
        // Create boot catalog at sector 24
        uint8_t* boot_catalog = iso_image + 24 * ISO9660_SECTOR_SIZE;
        
        // Validation entry
        struct {
            uint8_t header_id;
            uint8_t platform_id;
            uint16_t reserved;
            char id_string[24];
            uint16_t checksum;
            uint8_t key55;
            uint8_t keyAA;
        } __attribute__((packed)) *catalog_header = (void*)boot_catalog;
        
        catalog_header->header_id = 1;
        catalog_header->platform_id = 0; // 80x86
        memcpy(catalog_header->id_string, "uintOS Bootable CD      ", 24);
        catalog_header->checksum = 0;  // Normally would be calculated
        catalog_header->key55 = 0x55;
        catalog_header->keyAA = 0xAA;
        
        // Initial/Default entry
        struct {
            uint8_t boot_indicator;
            uint8_t boot_media_type;
            uint16_t load_segment;
            uint8_t system_type;
            uint8_t reserved;
            uint16_t sector_count;
            uint32_t load_rba;
            uint8_t reserved2[20];
        } __attribute__((packed)) *boot_entry = (void*)(boot_catalog + 32);
        
        boot_entry->boot_indicator = 0x88; // Bootable
        boot_entry->boot_media_type = 0; // No emulation
        boot_entry->load_segment = 0; // Default
        boot_entry->system_type = 0;
        boot_entry->sector_count = 2; // 2 sectors (1KB)
        boot_entry->load_rba = 25; // Boot image at sector 25
        
        // Create a simple boot image at sector 25
        uint8_t* boot_image = iso_image + 25 * ISO9660_SECTOR_SIZE;
        memcpy(boot_image, "UINTOS BOOTABLE CD - SIMULATED BOOT IMAGE", 40);
    }

    // Read the requested sector from our in-memory image
    if (sector * ISO9660_SECTOR_SIZE >= sizeof(iso_image)) {
        return ISO9660_ERR_IO_ERROR;  // Out of bounds
    }
    
    memcpy(buffer, iso_image + sector * ISO9660_SECTOR_SIZE, ISO9660_SECTOR_SIZE);
    return 0;  // Success
}

// Find a file or directory in a specific directory by name
static int find_file_in_dir(const char* name, uint32_t dir_sector, uint32_t dir_size, 
                           iso9660_directory_record_t** record, void** sector_buffer) {
    // Allocate buffer for the directory contents
    uint8_t* directory_buffer = malloc(dir_size);
    if (!directory_buffer) {
        return ISO9660_ERR_IO_ERROR;
    }
    
    // Read all directory sectors
    uint32_t sectors_to_read = (dir_size + ISO9660_SECTOR_SIZE - 1) / ISO9660_SECTOR_SIZE;
    for (uint32_t i = 0; i < sectors_to_read; i++) {
        if (read_raw_sector(dir_sector + i, directory_buffer + i * ISO9660_SECTOR_SIZE) != 0) {
            free(directory_buffer);
            return ISO9660_ERR_IO_ERROR;
        }
    }
    
    // Process the directory entries
    uint32_t offset = 0;
    int found = 0;
    
    while (offset < dir_size) {
        iso9660_directory_record_t* curr_record = (iso9660_directory_record_t*)(directory_buffer + offset);
        
        // Check for end of records
        if (curr_record->length == 0) {
            // Move to the next sector boundary
            offset = ((offset / ISO9660_SECTOR_SIZE) + 1) * ISO9660_SECTOR_SIZE;
            if (offset >= dir_size) {
                break;
            }
            continue;
        }
        
        // Check if this entry matches our filename
        int matches = 0;
        
        // Try extended names first (Joliet/Rock Ridge)
        if (has_joliet) {
            char extended_name[256];
            int name_len = iso9660_parse_extended_name(curr_record, extended_name, sizeof(extended_name));
            if (name_len > 0) {
                if (strcmp(name, extended_name) == 0) {
                    matches = 1;
                }
            }
        }
        
        // If no match with extended name, try standard ISO9660 name
        if (!matches) {
            matches = iso9660_name_compare(name, curr_record->filename, curr_record->filename_length);
        }
        
        if (matches) {
            // Found the file/directory
            *record = curr_record;
            *sector_buffer = directory_buffer;
            return 0;
        }
        
        offset += curr_record->length;
    }
    
    // File not found
    free(directory_buffer);
    return ISO9660_ERR_NOT_FOUND;
}

// Parse a path to find a file or directory
static int parse_path(const char* path, iso9660_directory_record_t** record, void** buffer) {
    if (!path) {
        return ISO9660_ERR_INVALID_ARG;
    }
    
    // Start from root directory
    uint32_t current_dir_sector = root_directory_extent;
    uint32_t current_dir_size = root_directory_size;
    
    // Skip leading slash
    if (path[0] == '/') {
        path++;
    }
    
    // Handle empty path (root directory)
    if (path[0] == 0) {
        // Create a small buffer with the root directory record
        uint8_t* root_buffer = malloc(ISO9660_SECTOR_SIZE);
        if (!root_buffer) {
            return ISO9660_ERR_IO_ERROR;
        }
        
        if (read_raw_sector(root_directory_extent, root_buffer) != 0) {
            free(root_buffer);
            return ISO9660_ERR_IO_ERROR;
        }
        
        *record = (iso9660_directory_record_t*)root_buffer;
        *buffer = root_buffer;
        return 0;
    }
    
    // Parse the path components
    char component[256];
    const char* path_ptr = path;
    
    while (*path_ptr) {
        // Extract next component
        int i = 0;
        while (*path_ptr && *path_ptr != '/' && i < 255) {
            component[i++] = *path_ptr++;
        }
        component[i] = 0;
        
        // Skip slash
        if (*path_ptr == '/') {
            path_ptr++;
        }
        
        // Find this component in the current directory
        int result = find_file_in_dir(component, current_dir_sector, current_dir_size, record, buffer);
        if (result != 0) {
            return result;
        }
        
        // If we have more path components, ensure this is a directory
        if (*path_ptr && !((*record)->file_flags & ISO9660_ATTR_DIRECTORY)) {
            free(*buffer);
            return ISO9660_ERR_NOT_FOUND;
        }
        
        // If this isn't the last component, update current directory
        if (*path_ptr) {
            current_dir_sector = (*record)->extent_location[0];
            current_dir_size = (*record)->data_length[0];
            free(*buffer);
            *buffer = NULL;
        }
    }
    
    // Found the requested file/directory
    return 0;
}

// Compare a normal filename with an ISO9660 filename
static int iso9660_name_compare(const char* name, const char* iso_name, int iso_name_len) {
    int name_len = strlen(name);
    char iso_name_buf[256];
    int i;
    
    // Copy ISO name to buffer, handling version number
    for (i = 0; i < iso_name_len && i < 255; i++) {
        if (iso_name[i] == ';') {
            // Stop at version separator
            break;
        }
        iso_name_buf[i] = iso_name[i];
    }
    iso_name_buf[i] = 0;
    
    // Compare names
    return (strcasecmp(name, iso_name_buf) == 0);
}

// Convert ISO9660 date format to readable format
static void convert_date(const uint8_t* iso_date, char* output) {
    // ISO9660 date: YY MM DD HH MM SS TZ
    // Output format: YYYY-MM-DD HH:MM:SS
    sprintf(output, "20%02d-%02d-%02d %02d:%02d:%02d",
            iso_date[0], iso_date[1], iso_date[2],
            iso_date[3], iso_date[4], iso_date[5]);
}