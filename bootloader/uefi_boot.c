#include "uefi_boot.h"
#include <stddef.h>

// Define protocol GUIDs
const efi_guid_t EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

const efi_guid_t EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

const efi_guid_t EFI_FILE_INFO_GUID = {
    0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

const efi_guid_t EFI_GLOBAL_VARIABLE_GUID = {
    0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}
};

const efi_guid_t ACPI_TABLE_GUID = {
    0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};

const efi_guid_t ACPI_20_TABLE_GUID = {
    0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};

// Global EFI variables
static efi_system_table_t* g_st = NULL;
static efi_handle_t g_image_handle = NULL;
static void* g_acpi_rsdp = NULL;

// UEFI utility functions
static void uefi_reset_console(void) {
    // Reset console input
    g_st->con_in->_reset(g_st->con_in, 0);
    
    // Reset console output
    g_st->con_out->_reset(g_st->con_out, 0);
}

static void uefi_wait_for_key(void) {
    efi_input_key_t key;
    efi_event_t events[] = { g_st->con_in->wait_for_key };
    uint64_t index;
    
    // Wait for a key press event
    g_st->boot_services->_wait_for_event(1, events, &index);
    
    // Read the key to clear the event
    g_st->con_in->read_key_stroke(g_st->con_in, &key);
}

static efi_status_t uefi_puts(const char* str) {
    // Convert ASCII to UCS-2 and print
    efi_char16_t buf[2];
    
    while (*str) {
        buf[0] = (efi_char16_t)*str++;
        buf[1] = 0;
        g_st->con_out->output_string(g_st->con_out, buf);
    }
    
    return EFI_SUCCESS;
}

static void uefi_clear_screen(void) {
    // Call clear screen function
    ((efi_status_t(*)(efi_simple_text_output_protocol_t*))g_st->con_out->_clear_screen)(g_st->con_out);
}

static void uefi_put_char(char c) {
    efi_char16_t buf[2];
    buf[0] = (efi_char16_t)c;
    buf[1] = 0;
    g_st->con_out->output_string(g_st->con_out, buf);
}

static void uefi_print_hex(uint64_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    int i;
    uint8_t nibble;
    
    // Print "0x" prefix
    uefi_put_char('0');
    uefi_put_char('x');
    
    // Print each hex digit, starting from the most significant
    for (i = 60; i >= 0; i -= 4) {
        nibble = (uint8_t)((value >> i) & 0xF);
        uefi_put_char(hex_chars[nibble]);
    }
}

static void uefi_print_decimal(uint64_t value) {
    char buffer[21]; // Maximum 20 digits for 64-bit integer
    int pos = 0;
    
    // Handle special case of 0
    if (value == 0) {
        uefi_put_char('0');
        return;
    }
    
    // Convert to decimal
    while (value > 0) {
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Print in reverse order
    while (pos > 0) {
        uefi_put_char(buffer[--pos]);
    }
}

static void* uefi_allocate_pages(uint64_t pages) {
    efi_physical_address_t addr = 0;
    efi_status_t status;
    
    // Allocate contiguous physical memory
    status = ((efi_status_t(*)(uint32_t, uint32_t, uint64_t, efi_physical_address_t*))
              g_st->boot_services->_allocate_pages)(
                  0, // AllocateAnyPages
                  EFI_LOADER_DATA,
                  pages,
                  &addr
              );
    
    if (status != EFI_SUCCESS) {
        return NULL;
    }
    
    return (void*)(uintptr_t)addr;
}

static void* uefi_allocate_pool(uint64_t size) {
    void* buffer = NULL;
    efi_status_t status;
    
    // Allocate memory from pool
    status = ((efi_status_t(*)(uint32_t, uint64_t, void**))
              g_st->boot_services->_allocate_pool)(
                  EFI_LOADER_DATA,
                  size,
                  &buffer
              );
    
    if (status != EFI_SUCCESS) {
        return NULL;
    }
    
    return buffer;
}

static void uefi_free_pool(void* buffer) {
    if (buffer) {
        ((efi_status_t(*)(void*))g_st->boot_services->_free_pool)(buffer);
    }
}

static efi_status_t uefi_handle_protocol(efi_handle_t handle, efi_guid_t* protocol, void** interface) {
    return ((efi_status_t(*)(efi_handle_t, efi_guid_t*, void**))
            g_st->boot_services->_handle_protocol)(
                handle,
                protocol,
                interface
            );
}

// Find the ACPI RSDP
static void* uefi_find_acpi_rsdp(void) {
    efi_configuration_table_t* config_table = g_st->configuration_table;
    void* rsdp = NULL;
    
    // Search configuration tables for ACPI RSDP
    for (uint64_t i = 0; i < g_st->number_of_table_entries; i++) {
        // Check for ACPI 2.0 table first
        if (memcmp(&config_table[i].vendor_guid, &ACPI_20_TABLE_GUID, sizeof(efi_guid_t)) == 0) {
            rsdp = config_table[i].vendor_table;
            uefi_puts("Found ACPI 2.0 RSDP table\r\n");
            return rsdp;
        }
        
        // Check for ACPI 1.0 table
        if (memcmp(&config_table[i].vendor_guid, &ACPI_TABLE_GUID, sizeof(efi_guid_t)) == 0) {
            rsdp = config_table[i].vendor_table;
            uefi_puts("Found ACPI 1.0 RSDP table\r\n");
            return rsdp;
        }
    }
    
    uefi_puts("WARNING: ACPI RSDP not found\r\n");
    return NULL;
}

// Load kernel file from the EFI System Partition
static efi_status_t uefi_load_kernel(efi_handle_t image_handle, void** kernel_base, uint64_t* kernel_size) {
    efi_loaded_image_protocol_t* loaded_image = NULL;
    efi_simple_file_system_protocol_t* fs = NULL;
    efi_file_protocol_t* root = NULL;
    efi_file_protocol_t* kernel_file = NULL;
    efi_status_t status;
    efi_guid_t fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    efi_guid_t loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_guid_t file_info_guid = EFI_FILE_INFO_GUID;
    
    // Get loaded image protocol
    status = uefi_handle_protocol(image_handle, &loaded_image_guid, (void**)&loaded_image);
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to get loaded image protocol\r\n");
        return status;
    }
    
    // Get file system protocol from loaded image device
    status = uefi_handle_protocol(loaded_image->device_handle, &fs_guid, (void**)&fs);
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to get file system protocol\r\n");
        return status;
    }
    
    // Open the volume
    status = fs->open_volume(fs, &root);
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to open volume\r\n");
        return status;
    }
    
    // Convert kernel filename to UCS-2
    efi_char16_t kernel_filename[32];
    const char* kernel_name = "KERNEL";
    int i;
    for (i = 0; kernel_name[i] != '\0'; i++) {
        kernel_filename[i] = (efi_char16_t)kernel_name[i];
    }
    kernel_filename[i] = 0;
    
    // Open kernel file
    status = root->open(
        root,
        &kernel_file,
        kernel_filename,
        0x01, // EFI_FILE_MODE_READ
        0
    );
    
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to open kernel file\r\n");
        root->close(root);
        return status;
    }
    
    // Get kernel file info to determine size
    efi_file_info_t* file_info = NULL;
    uint64_t file_info_size = sizeof(efi_file_info_t) + 128;
    
    file_info = uefi_allocate_pool(file_info_size);
    if (!file_info) {
        uefi_puts("Failed to allocate memory for file info\r\n");
        kernel_file->close(kernel_file);
        root->close(root);
        return EFI_OUT_OF_RESOURCES;
    }
    
    status = kernel_file->get_info(
        kernel_file,
        &file_info_guid,
        &file_info_size,
        file_info
    );
    
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to get kernel file info\r\n");
        uefi_free_pool(file_info);
        kernel_file->close(kernel_file);
        root->close(root);
        return status;
    }
    
    // Allocate pages for kernel
    uint64_t kernel_file_size = file_info->file_size;
    uint64_t pages_needed = (kernel_file_size + 4095) / 4096;
    void* kernel_addr = uefi_allocate_pages(pages_needed);
    
    if (!kernel_addr) {
        uefi_puts("Failed to allocate memory for kernel\r\n");
        uefi_free_pool(file_info);
        kernel_file->close(kernel_file);
        root->close(root);
        return EFI_OUT_OF_RESOURCES;
    }
    
    // Read kernel file
    status = kernel_file->read(
        kernel_file,
        &kernel_file_size,
        kernel_addr
    );
    
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to read kernel file\r\n");
        uefi_free_pool(file_info);
        kernel_file->close(kernel_file);
        root->close(root);
        return status;
    }
    
    // Clean up
    uefi_free_pool(file_info);
    kernel_file->close(kernel_file);
    root->close(root);
    
    *kernel_base = kernel_addr;
    *kernel_size = kernel_file_size;
    
    return EFI_SUCCESS;
}

// Convert UEFI memory map to uintOS format
void uefi_convert_memory_map(
    efi_memory_descriptor_t* uefi_memory_map,
    uint64_t memory_map_size,
    uint64_t descriptor_size,
    void* uintos_memory_map
) {
    // This is a simplified version - in a real OS, you'd do proper conversion
    // based on your OS's memory map format
    uint8_t* source = (uint8_t*)uefi_memory_map;
    uint8_t* dest = (uint8_t*)uintos_memory_map;
    uint64_t entries = memory_map_size / descriptor_size;
    
    for (uint64_t i = 0; i < entries; i++) {
        efi_memory_descriptor_t* descriptor = (efi_memory_descriptor_t*)(source + i * descriptor_size);
        
        // Example of mapping to uintOS memory type
        uint32_t uintos_type;
        switch (descriptor->type) {
            case EFI_CONVENTIONAL_MEMORY:
                uintos_type = 1; // Available memory
                break;
            case EFI_ACPI_RECLAIM_MEMORY:
                uintos_type = 3; // ACPI reclaimable
                break;
            case EFI_RUNTIME_SERVICES_CODE:
            case EFI_RUNTIME_SERVICES_DATA:
                uintos_type = 4; // Runtime services
                break;
            default:
                uintos_type = 2; // Reserved
                break;
        }
        
        // Example of populating uintOS memory map entries
        // NOTE: This is a simplified example - adapt to your OS's memory map format
        *(uint64_t*)(dest + 0) = descriptor->physical_start;
        *(uint64_t*)(dest + 8) = descriptor->number_of_pages * 4096;
        *(uint32_t*)(dest + 16) = uintos_type;
        *(uint32_t*)(dest + 20) = 0; // Reserved
        
        dest += 24; // Size of uintOS memory map entry
    }
}

// Main UEFI entry point
efi_status_t efi_main(efi_handle_t image_handle, efi_system_table_t* system_table) {
    // Initialize global variables
    g_st = system_table;
    g_image_handle = image_handle;
    
    // Reset console
    uefi_reset_console();
    
    // Clear screen and print welcome message
    uefi_clear_screen();
    g_st->con_out->set_attribute(g_st->con_out, 0x0F); // White on black
    uefi_puts(" \r\n");
    uefi_puts("uintOS UEFI Bootloader\r\n");
    uefi_puts("---------------------\r\n");
    uefi_puts(" \r\n");
    
    // Find ACPI RSDP
    g_acpi_rsdp = uefi_find_acpi_rsdp();
    
    // Load kernel file
    void* kernel_base = NULL;
    uint64_t kernel_size = 0;
    efi_status_t status = uefi_load_kernel(image_handle, &kernel_base, &kernel_size);
    
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to load kernel. Press any key to reboot.\r\n");
        uefi_wait_for_key();
        
        // Reboot the system
        ((void(*)(uint32_t, efi_status_t, uint64_t, void*))
         g_st->runtime_services->_reset_system)(
             0, // EfiResetCold
             EFI_SUCCESS,
             0,
             NULL
         );
        
        return status;
    }
    
    // Print kernel info
    uefi_puts("Kernel loaded at: ");
    uefi_print_hex((uint64_t)kernel_base);
    uefi_puts(" Size: ");
    uefi_print_decimal(kernel_size);
    uefi_puts(" bytes\r\n");
    
    // Get memory map
    uint64_t memory_map_size = 0;
    efi_memory_descriptor_t* memory_map = NULL;
    uint64_t map_key = 0;
    uint64_t descriptor_size = 0;
    uint32_t descriptor_version = 0;
    
    // Get memory map size
    status = g_st->boot_services->get_memory_map(
        &memory_map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );
    
    if (status != EFI_BUFFER_TOO_SMALL) {
        uefi_puts("Failed to get memory map size\r\n");
        return status;
    }
    
    // Add extra space for changes made by ExitBootServices
    memory_map_size += 2 * descriptor_size;
    
    // Allocate memory for the map
    memory_map = uefi_allocate_pool(memory_map_size);
    if (!memory_map) {
        uefi_puts("Failed to allocate memory for memory map\r\n");
        return EFI_OUT_OF_RESOURCES;
    }
    
    // Get the memory map
    status = g_st->boot_services->get_memory_map(
        &memory_map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );
    
    if (status != EFI_SUCCESS) {
        uefi_puts("Failed to get memory map\r\n");
        uefi_free_pool(memory_map);
        return status;
    }
    
    // Exit boot services
    status = g_st->boot_services->exit_boot_services(image_handle, map_key);
    if (status != EFI_SUCCESS) {
        // If this fails, we need to get a new memory map and try again
        uefi_puts("Failed to exit boot services, updating memory map and retrying...\r\n");
        
        status = g_st->boot_services->get_memory_map(
            &memory_map_size,
            memory_map,
            &map_key,
            &descriptor_size,
            &descriptor_version
        );
        
        if (status != EFI_SUCCESS) {
            uefi_puts("Failed to get memory map on retry\r\n");
            return status;
        }
        
        // Try to exit boot services again
        status = g_st->boot_services->exit_boot_services(image_handle, map_key);
        if (status != EFI_SUCCESS) {
            uefi_puts("Failed to exit boot services after retry\r\n");
            return status;
        }
    }
    
    // Boot services are no longer available after this point
    
    // Prepare boot info structure for kernel
    uefi_boot_info_t* boot_info = (uefi_boot_info_t*)memory_map; // Reuse memory map space
    
    // Fill in boot info
    boot_info->memory_map = (uint64_t)(memory_map + sizeof(uefi_boot_info_t));
    boot_info->memory_map_size = memory_map_size - sizeof(uefi_boot_info_t);
    boot_info->memory_map_descriptor_size = descriptor_size;
    boot_info->memory_map_descriptor_version = descriptor_version;
    boot_info->acpi_rsdp = g_acpi_rsdp;
    boot_info->kernel_physical_base = (uint64_t)kernel_base;
    boot_info->kernel_virtual_base = 0xFFFFFFFF80000000ULL; // Higher half kernel
    boot_info->kernel_size = kernel_size;
    
    // Jump to kernel entry point
    // Assumes kernel is at address 1MB with entry point at the beginning
    typedef void (*kernel_entry_t)(uefi_boot_info_t*);
    kernel_entry_t kernel_entry = (kernel_entry_t)((uintptr_t)kernel_base);
    
    // Call kernel
    kernel_entry(boot_info);
    
    // Should never get here
    return EFI_LOAD_ERROR;
}