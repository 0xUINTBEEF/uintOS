#ifndef UEFI_BOOT_H
#define UEFI_BOOT_H

/**
 * UEFI Bootloader for uintOS
 * 
 * This file defines the UEFI boot protocol and structures needed for
 * booting the uintOS kernel using UEFI firmware instead of legacy BIOS.
 */

#include <stdint.h>

// UEFI uses wide chars for strings
typedef uint16_t efi_char16_t;

// UEFI status codes
typedef uint64_t efi_status_t;

// UEFI handles and pointers
typedef void* efi_handle_t;
typedef void* efi_event_t;
typedef uint64_t efi_tpl_t;
typedef uint64_t efi_lba_t;
typedef uint64_t efi_physical_address_t;
typedef uint64_t efi_virtual_address_t;

// UEFI common status codes
#define EFI_SUCCESS                     0
#define EFI_LOAD_ERROR                  0x8000000000000001
#define EFI_INVALID_PARAMETER           0x8000000000000002
#define EFI_UNSUPPORTED                 0x8000000000000003
#define EFI_BAD_BUFFER_SIZE             0x8000000000000004
#define EFI_BUFFER_TOO_SMALL            0x8000000000000005
#define EFI_NOT_READY                   0x8000000000000006
#define EFI_DEVICE_ERROR                0x8000000000000007
#define EFI_NOT_FOUND                   0x8000000000000014
#define EFI_OUT_OF_RESOURCES            0x8000000000000015

// UEFI GUID structure
typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} efi_guid_t;

// UEFI Memory types
#define EFI_RESERVED_MEMORY_TYPE        0
#define EFI_LOADER_CODE                 1
#define EFI_LOADER_DATA                 2
#define EFI_BOOT_SERVICES_CODE          3
#define EFI_BOOT_SERVICES_DATA          4
#define EFI_RUNTIME_SERVICES_CODE       5
#define EFI_RUNTIME_SERVICES_DATA       6
#define EFI_CONVENTIONAL_MEMORY         7
#define EFI_UNUSABLE_MEMORY             8
#define EFI_ACPI_RECLAIM_MEMORY         9
#define EFI_ACPI_MEMORY_NVS             10
#define EFI_MEMORY_MAPPED_IO            11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                    13
#define EFI_PERSISTENT_MEMORY           14
#define EFI_MAX_MEMORY_TYPE             15

// Memory descriptor
typedef struct {
    uint32_t type;
    efi_physical_address_t physical_start;
    efi_virtual_address_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

// UEFI time structure
typedef struct {
    uint16_t year;        // 1900 – 9999
    uint8_t  month;       // 1 – 12
    uint8_t  day;         // 1 – 31
    uint8_t  hour;        // 0 – 23
    uint8_t  minute;      // 0 – 59
    uint8_t  second;      // 0 – 59
    uint8_t  pad1;
    uint32_t nanosecond;  // 0 – 999,999,999
    int16_t  time_zone;   // -1440 to 1440 or 2047
    uint8_t  daylight;
    uint8_t  pad2;
} efi_time_t;

// UEFI table header
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

// Forward declarations for UEFI protocols
struct efi_simple_text_input_protocol;
struct efi_simple_text_output_protocol;
struct efi_boot_services;
struct efi_runtime_services;

// UEFI configuration table entry
typedef struct {
    efi_guid_t vendor_guid;
    void* vendor_table;
} efi_configuration_table_t;

// UEFI System Table
typedef struct {
    efi_table_header_t hdr;
    efi_char16_t* firmware_vendor;
    uint32_t firmware_revision;
    efi_handle_t console_in_handle;
    struct efi_simple_text_input_protocol* con_in;
    efi_handle_t console_out_handle;
    struct efi_simple_text_output_protocol* con_out;
    efi_handle_t standard_error_handle;
    struct efi_simple_text_output_protocol* std_err;
    struct efi_runtime_services* runtime_services;
    struct efi_boot_services* boot_services;
    uint64_t number_of_table_entries;
    efi_configuration_table_t* configuration_table;
} efi_system_table_t;

// Simple Text Input Protocol
typedef struct {
    uint16_t scan_code;
    efi_char16_t unicode_char;
} efi_input_key_t;

typedef struct efi_simple_text_input_protocol {
    uint64_t _reset;
    efi_status_t (*read_key_stroke)(
        struct efi_simple_text_input_protocol* this,
        efi_input_key_t* key
    );
    efi_event_t wait_for_key;
} efi_simple_text_input_protocol_t;

// Simple Text Output Protocol
typedef struct efi_simple_text_output_protocol {
    uint64_t _reset;
    
    efi_status_t (*output_string)(
        struct efi_simple_text_output_protocol* this,
        efi_char16_t* string
    );
    
    uint64_t _test_string;
    
    efi_status_t (*query_mode)(
        struct efi_simple_text_output_protocol* this,
        uint64_t mode_number,
        uint64_t* columns,
        uint64_t* rows
    );
    
    efi_status_t (*set_mode)(
        struct efi_simple_text_output_protocol* this,
        uint64_t mode_number
    );
    
    efi_status_t (*set_attribute)(
        struct efi_simple_text_output_protocol* this,
        uint64_t attribute
    );
    
    uint64_t _clear_screen;
    uint64_t _set_cursor_position;
    uint64_t _enable_cursor;
    void* mode;
} efi_simple_text_output_protocol_t;

// Boot services
typedef struct efi_boot_services {
    efi_table_header_t hdr;
    
    // Task Priority Services
    uint64_t _raise_tpl;
    uint64_t _restore_tpl;
    
    // Memory Services
    uint64_t _allocate_pages;
    uint64_t _free_pages;
    
    efi_status_t (*get_memory_map)(
        uint64_t* memory_map_size,
        efi_memory_descriptor_t* memory_map,
        uint64_t* map_key,
        uint64_t* descriptor_size,
        uint32_t* descriptor_version
    );
    
    uint64_t _allocate_pool;
    uint64_t _free_pool;
    
    // Event & Timer Services
    uint64_t _create_event;
    uint64_t _set_timer;
    uint64_t _wait_for_event;
    uint64_t _signal_event;
    uint64_t _close_event;
    uint64_t _check_event;
    
    // Protocol Handler Services
    uint64_t _install_protocol_interface;
    uint64_t _reinstall_protocol_interface;
    uint64_t _uninstall_protocol_interface;
    uint64_t _handle_protocol;
    uint64_t _reserved;
    uint64_t _register_protocol_notify;
    uint64_t _locate_handle;
    uint64_t _locate_device_path;
    uint64_t _install_configuration_table;
    
    // Image Services
    uint64_t _load_image;
    uint64_t _start_image;
    uint64_t _exit;
    uint64_t _unload_image;
    
    efi_status_t (*exit_boot_services)(
        efi_handle_t image_handle,
        uint64_t map_key
    );
    
    // Miscellaneous Services
    uint64_t _get_next_monotonic_count;
    uint64_t _stall;
    uint64_t _set_watchdog_timer;
    
    // DriverSupport Services
    uint64_t _connect_controller;
    uint64_t _disconnect_controller;
    
    // Open and Close Protocol Services
    uint64_t _open_protocol;
    uint64_t _close_protocol;
    uint64_t _open_protocol_information;
    
    // Library Services
    uint64_t _protocols_per_handle;
    uint64_t _locate_handle_buffer;
    
    efi_status_t (*locate_protocol)(
        efi_guid_t* protocol,
        void* registration,
        void** interface
    );
    
    uint64_t _install_multiple_protocol_interfaces;
    uint64_t _uninstall_multiple_protocol_interfaces;
    
    // 32-bit CRC Services
    uint64_t _calculate_crc32;
    
    // Miscellaneous Services
    uint64_t _copy_mem;
    uint64_t _set_mem;
    uint64_t _create_event_ex;
} efi_boot_services_t;

// Runtime services
typedef struct efi_runtime_services {
    efi_table_header_t hdr;
    
    // Time Services
    uint64_t _get_time;
    uint64_t _set_time;
    uint64_t _get_wakeup_time;
    uint64_t _set_wakeup_time;
    
    // Virtual Memory Services
    uint64_t _set_virtual_address_map;
    uint64_t _convert_pointer;
    
    // Variable Services
    uint64_t _get_variable;
    uint64_t _get_next_variable_name;
    uint64_t _set_variable;
    
    // Miscellaneous Services
    uint64_t _get_next_high_monotonic_count;
    uint64_t _reset_system;
    
    // UEFI 2.0 Capsule Services
    uint64_t _update_capsule;
    uint64_t _query_capsule_capabilities;
    
    // Miscellaneous UEFI 2.0 Service
    uint64_t _query_variable_info;
} efi_runtime_services_t;

// File system protocol GUIDs
extern const efi_guid_t EFI_LOADED_IMAGE_PROTOCOL_GUID;
extern const efi_guid_t EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
extern const efi_guid_t EFI_FILE_INFO_GUID;
extern const efi_guid_t EFI_GLOBAL_VARIABLE_GUID;

// ACPI table GUIDs
extern const efi_guid_t ACPI_TABLE_GUID;
extern const efi_guid_t ACPI_20_TABLE_GUID;

// Loaded image protocol
typedef struct {
    uint32_t revision;
    efi_handle_t parent_handle;
    efi_system_table_t* system_table;
    efi_handle_t device_handle;
    void* file_path;
    void* reserved;
    uint32_t load_options_size;
    void* load_options;
    void* image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    uint64_t unload;
} efi_loaded_image_protocol_t;

// File IO
typedef struct efi_file_protocol efi_file_protocol_t;

struct efi_file_protocol {
    uint64_t revision;
    
    efi_status_t (*open)(
        efi_file_protocol_t* this,
        efi_file_protocol_t** new_handle,
        efi_char16_t* file_name,
        uint64_t open_mode,
        uint64_t attributes
    );
    
    efi_status_t (*close)(
        efi_file_protocol_t* this
    );
    
    uint64_t _delete;
    
    efi_status_t (*read)(
        efi_file_protocol_t* this,
        uint64_t* buffer_size,
        void* buffer
    );
    
    efi_status_t (*write)(
        efi_file_protocol_t* this,
        uint64_t* buffer_size,
        void* buffer
    );
    
    uint64_t _get_position;
    uint64_t _set_position;
    
    efi_status_t (*get_info)(
        efi_file_protocol_t* this,
        efi_guid_t* information_type,
        uint64_t* buffer_size,
        void* buffer
    );
    
    uint64_t _set_info;
    uint64_t _flush;
    
    // EFI 1.1+
    uint64_t _open_ex;
    uint64_t _read_ex;
    uint64_t _write_ex;
    uint64_t _flush_ex;
};

// Simple File System Protocol
typedef struct efi_simple_file_system_protocol {
    uint64_t revision;
    
    efi_status_t (*open_volume)(
        struct efi_simple_file_system_protocol* this,
        efi_file_protocol_t** root
    );
} efi_simple_file_system_protocol_t;

// File info
typedef struct {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    efi_time_t create_time;
    efi_time_t last_access_time;
    efi_time_t modification_time;
    uint64_t attribute;
    efi_char16_t file_name[1];  // Variable length array
} efi_file_info_t;

// Boot information passed to the kernel
typedef struct {
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_descriptor_size;
    uint32_t memory_map_descriptor_version;
    void* acpi_rsdp;
    uint64_t kernel_physical_base;
    uint64_t kernel_virtual_base;
    uint64_t kernel_size;
} uefi_boot_info_t;

// Function to convert UEFI memory map to uintOS format
void uefi_convert_memory_map(
    efi_memory_descriptor_t* uefi_memory_map,
    uint64_t memory_map_size,
    uint64_t descriptor_size,
    void* uintos_memory_map
);

// Main UEFI entry point
efi_status_t efi_main(efi_handle_t image_handle, efi_system_table_t* system_table);

#endif // UEFI_BOOT_H