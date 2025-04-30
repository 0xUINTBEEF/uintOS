/**
 * PE (Portable Executable) File Format Loader for Windows Drivers
 * 
 * This module handles loading and parsing PE format files for Windows drivers.
 * It supports proper section loading, import resolution, and relocation.
 *
 * Version: 1.0
 * Date: May 1, 2025
 */

#ifndef UINTOS_PE_LOADER_H
#define UINTOS_PE_LOADER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * PE file format constants
 */
#define PE_SIGNATURE             0x00004550  // "PE\0\0"
#define DOS_SIGNATURE            0x5A4D      // "MZ"

#define IMAGE_FILE_MACHINE_I386  0x014c
#define IMAGE_FILE_MACHINE_AMD64 0x8664

#define IMAGE_SUBSYSTEM_NATIVE   1

#define IMAGE_SCN_MEM_EXECUTE    0x20000000
#define IMAGE_SCN_MEM_READ       0x40000000
#define IMAGE_SCN_MEM_WRITE      0x80000000

#define IMAGE_DIRECTORY_ENTRY_EXPORT         0
#define IMAGE_DIRECTORY_ENTRY_IMPORT         1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE       2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION      3
#define IMAGE_DIRECTORY_ENTRY_SECURITY       4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC      5
#define IMAGE_DIRECTORY_ENTRY_DEBUG          6
#define IMAGE_DIRECTORY_ENTRY_COPYRIGHT      7
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR      8
#define IMAGE_DIRECTORY_ENTRY_TLS            9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG    10
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT   11
#define IMAGE_DIRECTORY_ENTRY_IAT            12

/**
 * PE file format structures
 */
#pragma pack(push, 1)

// DOS header
typedef struct {
    uint16_t e_magic;           // Magic number (MZ)
    uint16_t e_cblp;            // Bytes on last page of file
    uint16_t e_cp;              // Pages in file
    uint16_t e_crlc;            // Relocations
    uint16_t e_cparhdr;         // Size of header in paragraphs
    uint16_t e_minalloc;        // Minimum extra paragraphs needed
    uint16_t e_maxalloc;        // Maximum extra paragraphs needed
    uint16_t e_ss;              // Initial (relative) SS value
    uint16_t e_sp;              // Initial SP value
    uint16_t e_csum;            // Checksum
    uint16_t e_ip;              // Initial IP value
    uint16_t e_cs;              // Initial (relative) CS value
    uint16_t e_lfarlc;          // File address of relocation table
    uint16_t e_ovno;            // Overlay number
    uint16_t e_res[4];          // Reserved words
    uint16_t e_oemid;           // OEM identifier
    uint16_t e_oeminfo;         // OEM information
    uint16_t e_res2[10];        // Reserved words
    uint32_t e_lfanew;          // File address of new exe header
} dos_header_t;

// PE file header
typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} file_header_t;

// PE data directory
typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} data_directory_t;

// PE optional header (32-bit)
typedef struct {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    data_directory_t DataDirectory[16];
} optional_header32_t;

// PE optional header (64-bit)
typedef struct {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    data_directory_t DataDirectory[16];
} optional_header64_t;

// PE header
typedef struct {
    uint32_t Signature;
    file_header_t FileHeader;
    union {
        optional_header32_t OptionalHeader32;
        optional_header64_t OptionalHeader64;
    };
} pe_header_t;

// Section header
typedef struct {
    char Name[8];
    union {
        uint32_t PhysicalAddress;
        uint32_t VirtualSize;
    };
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} section_header_t;

// Import descriptor
typedef struct {
    union {
        uint32_t Characteristics;
        uint32_t OriginalFirstThunk;
    };
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
} import_descriptor_t;

// Import thunk
typedef struct {
    union {
        uint32_t ForwarderString;
        uint32_t Function;
        uint32_t Ordinal;
        uint32_t AddressOfData;
    } u1;
} import_thunk32_t;

typedef struct {
    union {
        uint64_t ForwarderString;
        uint64_t Function;
        uint64_t Ordinal;
        uint64_t AddressOfData;
    } u1;
} import_thunk64_t;

// Export directory
typedef struct {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Name;
    uint32_t Base;
    uint32_t NumberOfFunctions;
    uint32_t NumberOfNames;
    uint32_t AddressOfFunctions;
    uint32_t AddressOfNames;
    uint32_t AddressOfNameOrdinals;
} export_directory_t;

// Base relocation block
typedef struct {
    uint32_t VirtualAddress;
    uint32_t SizeOfBlock;
    // Followed by relocation entries
} base_relocation_block_t;

#pragma pack(pop)

/**
 * PE loader error codes
 */
typedef enum {
    PE_SUCCESS = 0,
    PE_ERROR_NOT_PE_FILE = -1,
    PE_ERROR_UNSUPPORTED_MACHINE = -2,
    PE_ERROR_MEMORY_ALLOCATION = -3,
    PE_ERROR_FILE_READ = -4,
    PE_ERROR_IMPORT_RESOLUTION = -5,
    PE_ERROR_RELOCATION = -6,
    PE_ERROR_INVALID_SECTION = -7,
    PE_ERROR_EXPORT_NOT_FOUND = -8,
    PE_ERROR_UNSUPPORTED_SUBSYSTEM = -9,
    PE_ERROR_ENTRY_POINT = -10
} pe_error_t;

/**
 * PE loader configuration
 */
typedef struct {
    uint64_t preferred_base_address;  // Preferred base address for loading the image
    bool relocate;                    // Whether to apply relocations if needed
    bool resolve_imports;             // Whether to resolve imports
    void* (*import_resolver)(const char*, const char*);  // Function to resolve imports
    uint32_t memory_protection;       // Memory protection flags for sections
    bool map_sections;                // Whether to map sections with appropriate protection
    bool debug_info;                  // Whether to load debug information
} pe_loader_config_t;

/**
 * Loaded PE image information
 */
typedef struct {
    void*    base_address;            // Base address of the loaded image
    uint64_t image_size;              // Size of the loaded image
    void*    entry_point;             // Image entry point
    void*    export_directory;        // Export directory of the image
    uint32_t timestamp;               // Image timestamp
    char     name[64];                // Image name (from export directory if available)
    uint16_t machine_type;            // Machine type the image is built for
    bool     is_64bit;                // Whether the image is 64-bit
    uint64_t original_image_base;     // Image base address from the PE header
} pe_image_t;

/**
 * Load a PE file from memory
 * 
 * @param file_data Pointer to file data in memory
 * @param file_size Size of the file in memory
 * @param config Loader configuration
 * @param image Output image information
 * @return PE_SUCCESS on success, error code on failure
 */
pe_error_t pe_load_from_memory(const void* file_data, uint64_t file_size, 
                               pe_loader_config_t* config, pe_image_t* image);

/**
 * Load a PE file from disk
 * 
 * @param filename Path to the PE file
 * @param config Loader configuration
 * @param image Output image information
 * @return PE_SUCCESS on success, error code on failure
 */
pe_error_t pe_load_from_file(const char* filename, pe_loader_config_t* config, pe_image_t* image);

/**
 * Unload a PE image
 * 
 * @param image Image to unload
 * @return PE_SUCCESS on success, error code on failure
 */
pe_error_t pe_unload(pe_image_t* image);

/**
 * Get an exported function from a loaded PE image
 * 
 * @param image Loaded PE image
 * @param function_name Name of the function to find
 * @return Pointer to the function, NULL if not found
 */
void* pe_get_export(pe_image_t* image, const char* function_name);

/**
 * Get an exported function by ordinal from a loaded PE image
 * 
 * @param image Loaded PE image
 * @param ordinal Ordinal of the function to find
 * @return Pointer to the function, NULL if not found
 */
void* pe_get_export_by_ordinal(pe_image_t* image, uint16_t ordinal);

/**
 * Validate a PE file
 * 
 * @param file_data Pointer to file data in memory
 * @param file_size Size of the file in memory
 * @return PE_SUCCESS if the file is a valid PE file, error code otherwise
 */
pe_error_t pe_validate(const void* file_data, uint64_t file_size);

/**
 * Convert a relative virtual address (RVA) to an actual memory address
 * 
 * @param image Loaded PE image
 * @param rva Relative virtual address
 * @return Actual memory address
 */
void* pe_rva_to_ptr(pe_image_t* image, uint32_t rva);

#endif /* UINTOS_PE_LOADER_H */