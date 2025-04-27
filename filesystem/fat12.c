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

                while (cluster < 0xFF8 && bytes_read < size) {
                    uint16_t cluster_sector = root_dir_start + root_dir_sectors + (cluster - 2) * boot_sector.sectors_per_cluster;
                    read_sector(cluster_sector, buffer + bytes_read);
                    bytes_read += SECTOR_SIZE;

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