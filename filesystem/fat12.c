#include "fat12.h"
#include <stdint.h>
#include <stddef.h>
#include "../kernel/io.h"

#define SECTOR_SIZE 512

// FAT12 Boot Sector structure
struct fat12_boot_sector {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_dir_entries;
    uint16_t total_sectors;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t large_sector_count;
} __attribute__((packed));

// FAT12 Directory Entry structure
struct fat12_dir_entry {
    char name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t create_time_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

static struct fat12_boot_sector boot_sector;

// Function to read a sector from disk
int read_sector(uint32_t sector, void* buffer) {
    // This would normally use a disk driver to read from an actual disk
    // For a simple implementation, we'll create a basic in-memory disk image
    
    static int initialized = 0;
    static uint8_t disk_image[1024 * 1024];  // 1MB disk image
    
    // Initialize disk image with a basic FAT12 structure on first use
    if (!initialized) {
        initialized = 1;
        
        // Create boot sector
        struct fat12_boot_sector* bs = (struct fat12_boot_sector*)&disk_image[0];
        bs->jump[0] = 0xEB;          // JMP instruction
        bs->jump[1] = 0x3C;
        bs->jump[2] = 0x90;
        memcpy(bs->oem, "UINTOS  ", 8);
        bs->bytes_per_sector = 512;
        bs->sectors_per_cluster = 1;
        bs->reserved_sectors = 1;
        bs->num_fats = 2;
        bs->root_dir_entries = 224;
        bs->total_sectors = 2880;    // 1.44MB floppy
        bs->media_descriptor = 0xF0; // 3.5" floppy
        bs->sectors_per_fat = 9;
        bs->sectors_per_track = 18;
        bs->num_heads = 2;
        bs->hidden_sectors = 0;
        bs->large_sector_count = 0;
        
        // Calculate some key offsets
        uint16_t fat_start = bs->reserved_sectors;
        uint16_t root_dir_start = fat_start + bs->num_fats * bs->sectors_per_fat;
        uint16_t data_start = root_dir_start + ((bs->root_dir_entries * 32) + (bs->bytes_per_sector - 1)) / bs->bytes_per_sector;
        
        // Initialize the first FAT
        disk_image[fat_start * 512] = bs->media_descriptor;
        disk_image[fat_start * 512 + 1] = 0xFF;
        disk_image[fat_start * 512 + 2] = 0xFF;
        
        // Initialize the second FAT (identical to the first)
        memcpy(&disk_image[(fat_start + bs->sectors_per_fat) * 512], 
               &disk_image[fat_start * 512], 
               bs->sectors_per_fat * 512);
        
        // Create some sample files in the root directory
        struct fat12_dir_entry* dir = (struct fat12_dir_entry*)&disk_image[root_dir_start * 512];
        
        // File 1: README.TXT
        memcpy(dir->name, "README  TXT", 11);
        dir->attr = 0x20;  // Archive attribute
        dir->first_cluster_low = 2;
        dir->file_size = 37;
        dir->create_date = 0x5345;  // Some date value
        dir->create_time = 0x6123;  // Some time value
        dir->write_date = 0x5345;
        dir->write_time = 0x6123;
        
        // Content for README.TXT
        char* readme_content = "uintOS - A simple educational OS\r\n";
        memcpy(&disk_image[data_start * 512], readme_content, strlen(readme_content));
        
        // File 2: KERNEL.BIN
        dir++;
        memcpy(dir->name, "KERNEL  BIN", 11);
        dir->attr = 0x20;  // Archive attribute
        dir->first_cluster_low = 3;
        dir->file_size = 512;  // 512 byte kernel
        dir->create_date = 0x5345;  // Some date value
        dir->create_time = 0x6123;  // Some time value
        dir->write_date = 0x5345;
        dir->write_time = 0x6123;
        
        // Simple content for KERNEL.BIN
        memset(&disk_image[(data_start + 1) * 512], 0xAA, 512);
        
        // Directory: SYSTEM
        dir++;
        memcpy(dir->name, "SYSTEM     ", 11);
        dir->attr = 0x10;  // Directory attribute
        dir->first_cluster_low = 4;
        dir->file_size = 0;  // Directories have size 0
        dir->create_date = 0x5345;
        dir->create_time = 0x6123;
        dir->write_date = 0x5345;
        dir->write_time = 0x6123;
        
        // File 3: LOG.TXT
        dir++;
        memcpy(dir->name, "LOG     TXT", 11);
        dir->attr = 0x20;  // Archive attribute
        dir->first_cluster_low = 5;
        dir->file_size = 24;
        dir->create_date = 0x5345;
        dir->create_time = 0x6123;
        dir->write_date = 0x5345;
        dir->write_time = 0x6123;
        
        // Content for LOG.TXT
        char* log_content = "System started up OK\r\n";
        memcpy(&disk_image[(data_start + 3) * 512], log_content, strlen(log_content));
    }
    
    // Read the requested sector from our in-memory disk image
    if (sector * 512 >= sizeof(disk_image)) {
        return 0;  // Out of bounds
    }
    
    memcpy(buffer, &disk_image[sector * 512], 512);
    return 512;  // Return number of bytes read
}

void fat12_init() {
    // Read the boot sector from the disk
    read_sector(0, (uint8_t*)&boot_sector);

    // Validate the boot sector
    if (boot_sector.bytes_per_sector != SECTOR_SIZE) {
        // Handle error: invalid sector size
        return;
    }

    // Additional initialization logic can go here
}

int fat12_read_file(const char* filename, char* buffer, int size) {
    // Locate the file in the root directory
    uint16_t root_dir_sectors = (boot_sector.root_dir_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint16_t root_dir_start = boot_sector.reserved_sectors + boot_sector.num_fats * boot_sector.sectors_per_fat;

    for (uint16_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t sector_data[SECTOR_SIZE];
        read_sector(root_dir_start + sector, sector_data);

        struct fat12_dir_entry* entries = (struct fat12_dir_entry*)sector_data;
        for (int i = 0; i < SECTOR_SIZE / sizeof(struct fat12_dir_entry); i++) {
            if (entries[i].name[0] == 0x00) {
                // End of directory
                return -1;
            }

            if (entries[i].name[0] != 0xE5 && strncmp(entries[i].name, filename, 11) == 0) {
                // File found, read its contents
                uint16_t cluster = entries[i].first_cluster_low;
                int bytes_read = 0;
                int file_size = entries[i].file_size;
                
                // Ensure we don't read more than the file size or buffer size
                int max_bytes_to_read = (file_size < size) ? file_size : size;

                while (cluster < 0xFF8 && bytes_read < max_bytes_to_read) {
                    uint16_t cluster_sector = root_dir_start + root_dir_sectors + (cluster - 2) * boot_sector.sectors_per_cluster;
                    
                    // Calculate how many bytes to read from this sector
                    int bytes_to_read = SECTOR_SIZE;
                    if (bytes_read + bytes_to_read > max_bytes_to_read) {
                        bytes_to_read = max_bytes_to_read - bytes_read;
                    }
                    
                    // Read the sector data into a temporary buffer first
                    uint8_t sector_data[SECTOR_SIZE];
                    read_sector(cluster_sector, sector_data);
                    
                    // Copy only the needed bytes to the output buffer
                    for (int j = 0; j < bytes_to_read; j++) {
                        buffer[bytes_read + j] = sector_data[j];
                    }
                    
                    bytes_read += bytes_to_read;

                    // Read the next cluster from the FAT
                    uint16_t fat_offset = cluster * 3 / 2;
                    uint16_t fat_sector = boot_sector.reserved_sectors + fat_offset / SECTOR_SIZE;
                    uint8_t fat_data[SECTOR_SIZE];
                    read_sector(fat_sector, fat_data);

                    if (cluster & 1) {
                        cluster = (fat_data[fat_offset % SECTOR_SIZE] >> 4) | (fat_data[fat_offset % SECTOR_SIZE + 1] << 4);
                    } else {
                        cluster = fat_data[fat_offset % SECTOR_SIZE] | ((fat_data[fat_offset % SECTOR_SIZE + 1] & 0x0F) << 8);
                    }
                }

                return bytes_read;
            }
        }
    }

    // File not found
    return -1;
}

// Implements directory listing functionality
int fat12_list_directory(const char* path, fat12_file_entry_t* entries, int max_entries) {
    // For the root directory listing
    uint16_t root_dir_sectors = (boot_sector.root_dir_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint16_t root_dir_start = boot_sector.reserved_sectors + boot_sector.num_fats * boot_sector.sectors_per_fat;
    
    int entries_found = 0;
    
    // Currently we only support root directory listing (path = "/" or "")
    if (path != NULL && path[0] != 0 && path[0] != '/' && path[1] != 0) {
        return FAT12_ERR_INVALID_ARG;
    }

    for (uint16_t sector = 0; sector < root_dir_sectors && entries_found < max_entries; sector++) {
        uint8_t sector_data[SECTOR_SIZE];
        if (read_sector(root_dir_start + sector, sector_data) != SECTOR_SIZE) {
            return FAT12_ERR_IO_ERROR;
        }

        struct fat12_dir_entry* dir_entries = (struct fat12_dir_entry*)sector_data;
        
        for (int i = 0; i < SECTOR_SIZE / sizeof(struct fat12_dir_entry) && entries_found < max_entries; i++) {
            // Skip deleted or empty entries
            if (dir_entries[i].name[0] == 0x00) {
                // End of directory
                break;
            }
            
            if (dir_entries[i].name[0] == 0xE5) {
                // Deleted entry
                continue;
            }
            
            // Convert 8.3 format to filename.ext format
            int name_len = 0;
            for (int j = 0; j < 8 && dir_entries[i].name[j] != ' '; j++) {
                entries[entries_found].name[name_len++] = dir_entries[i].name[j];
            }
            
            // Add extension if present
            if (dir_entries[i].name[8] != ' ') {
                entries[entries_found].name[name_len++] = '.';
                for (int j = 8; j < 11 && dir_entries[i].name[j] != ' '; j++) {
                    entries[entries_found].name[name_len++] = dir_entries[i].name[j];
                }
            }
            
            // Null terminate the filename
            entries[entries_found].name[name_len] = '\0';
            
            // Copy other file information
            entries[entries_found].attributes = dir_entries[i].attr;
            entries[entries_found].size = dir_entries[i].file_size;
            entries[entries_found].cluster = dir_entries[i].first_cluster_low;
            entries[entries_found].create_date = dir_entries[i].create_date;
            entries[entries_found].create_time = dir_entries[i].create_time;
            entries[entries_found].last_access_date = dir_entries[i].last_access_date;
            entries[entries_found].last_modified_date = dir_entries[i].write_date;
            entries[entries_found].last_modified_time = dir_entries[i].write_time;
            
            entries_found++;
        }
    }
    
    return entries_found;
}

// Check if a file exists in the filesystem
int fat12_file_exists(const char* filename) {
    // Convert filename to FAT12 8.3 format
    char fat_filename[11];
    for (int i = 0; i < 11; i++) {
        fat_filename[i] = ' ';  // Fill with spaces (padding)
    }
    
    // Parse name component
    int i = 0;
    while (filename[i] != '\0' && filename[i] != '.' && i < 8) {
        fat_filename[i] = filename[i];
        i++;
    }
    
    // Skip to extension if there is one
    int j = 0;
    while (filename[i] != '\0' && filename[i] != '.') {
        i++;
    }
    
    // Parse extension component if it exists
    if (filename[i] == '.') {
        i++; // Skip the dot
        while (filename[i] != '\0' && j < 3) {
            fat_filename[8 + j] = filename[i];
            i++;
            j++;
        }
    }
    
    // Now search for the file in the directory
    uint16_t root_dir_sectors = (boot_sector.root_dir_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint16_t root_dir_start = boot_sector.reserved_sectors + boot_sector.num_fats * boot_sector.sectors_per_fat;

    for (uint16_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t sector_data[SECTOR_SIZE];
        if (read_sector(root_dir_start + sector, sector_data) != SECTOR_SIZE) {
            return FAT12_ERR_IO_ERROR;
        }

        struct fat12_dir_entry* entries = (struct fat12_dir_entry*)sector_data;
        for (int i = 0; i < SECTOR_SIZE / sizeof(struct fat12_dir_entry); i++) {
            if (entries[i].name[0] == 0x00) {
                // End of directory, file not found
                return 0;
            }

            if (entries[i].name[0] != 0xE5) {  // Not a deleted entry
                // Compare the filename (all 11 characters must match)
                int match = 1;
                for (int j = 0; j < 11; j++) {
                    if (entries[i].name[j] != fat_filename[j]) {
                        match = 0;
                        break;
                    }
                }
                
                if (match) {
                    // File found
                    return 1;
                }
            }
        }
    }
    
    // File not found
    return 0;
}

// Get the size of a file in the filesystem
int fat12_get_file_size(const char* filename) {
    // Convert filename to FAT12 8.3 format (same as in file_exists)
    char fat_filename[11];
    for (int i = 0; i < 11; i++) {
        fat_filename[i] = ' ';  // Fill with spaces (padding)
    }
    
    // Parse name component
    int i = 0;
    while (filename[i] != '\0' && filename[i] != '.' && i < 8) {
        fat_filename[i] = filename[i];
        i++;
    }
    
    // Skip to extension if there is one
    int j = 0;
    while (filename[i] != '\0' && filename[i] != '.') {
        i++;
    }
    
    // Parse extension component if it exists
    if (filename[i] == '.') {
        i++; // Skip the dot
        while (filename[i] != '\0' && j < 3) {
            fat_filename[8 + j] = filename[i];
            i++;
            j++;
        }
    }
    
    // Now search for the file in the directory
    uint16_t root_dir_sectors = (boot_sector.root_dir_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint16_t root_dir_start = boot_sector.reserved_sectors + boot_sector.num_fats * boot_sector.sectors_per_fat;

    for (uint16_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t sector_data[SECTOR_SIZE];
        if (read_sector(root_dir_start + sector, sector_data) != SECTOR_SIZE) {
            return FAT12_ERR_IO_ERROR;
        }

        struct fat12_dir_entry* entries = (struct fat12_dir_entry*)sector_data;
        for (int i = 0; i < SECTOR_SIZE / sizeof(struct fat12_dir_entry); i++) {
            if (entries[i].name[0] == 0x00) {
                // End of directory, file not found
                return FAT12_ERR_NOT_FOUND;
            }

            if (entries[i].name[0] != 0xE5) {  // Not a deleted entry
                // Compare the filename (all 11 characters must match)
                int match = 1;
                for (int j = 0; j < 11; j++) {
                    if (entries[i].name[j] != fat_filename[j]) {
                        match = 0;
                        break;
                    }
                }
                
                if (match) {
                    // File found, return its size
                    return entries[i].file_size;
                }
            }
        }
    }
    
    // File not found
    return FAT12_ERR_NOT_FOUND;
}