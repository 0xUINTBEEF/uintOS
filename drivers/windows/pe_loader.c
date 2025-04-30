/**
 * PE (Portable Executable) File Format Loader for Windows Drivers
 * 
 * Implementation of the PE loader functionality defined in pe_loader.h.
 *
 * Version: 1.0
 * Date: May 1, 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pe_loader.h"
#include "../../memory/heap.h"
#include "../../kernel/logging/log.h"
#include "../../filesystem/fat12.h"

#define PE_LOG_TAG "PE_LOADER"

// Section access protection mapping
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

// Memory page size (assumed to be 4KB)
#define PAGE_SIZE 4096

// Inline helper function to align address up to page boundary
static inline uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Helper function to check memory allocation
static void* pe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        log_error(PE_LOG_TAG, "Memory allocation failed: %zu bytes", size);
    }
    return ptr;
}

// Helper function to read a file into memory
static void* pe_read_file(const char* filename, uint64_t* file_size) {
    // Check if file exists and get size
    int size = fat12_get_file_size(filename);
    if (size <= 0) {
        log_error(PE_LOG_TAG, "Failed to get file size for %s: %d", filename, size);
        return NULL;
    }

    // Allocate memory for the file
    void* buffer = pe_malloc(size);
    if (!buffer) {
        return NULL;
    }

    // Read the file
    int bytes_read = fat12_read_file(filename, buffer, size);
    if (bytes_read != size) {
        log_error(PE_LOG_TAG, "Failed to read file %s: %d bytes read, expected %d", 
                  filename, bytes_read, size);
        free(buffer);
        return NULL;
    }

    *file_size = size;
    return buffer;
}

// Function to validate PE file format
pe_error_t pe_validate(const void* file_data, uint64_t file_size) {
    if (!file_data || file_size < sizeof(dos_header_t)) {
        log_error(PE_LOG_TAG, "Invalid file data or size too small for DOS header");
        return PE_ERROR_NOT_PE_FILE;
    }

    // Check DOS header
    const dos_header_t* dos_header = (const dos_header_t*)file_data;
    if (dos_header->e_magic != DOS_SIGNATURE) {
        log_error(PE_LOG_TAG, "Invalid DOS signature: 0x%04X, expected 0x%04X", 
                  dos_header->e_magic, DOS_SIGNATURE);
        return PE_ERROR_NOT_PE_FILE;
    }

    // Check if the file is large enough to contain the PE header
    if (file_size < dos_header->e_lfanew + sizeof(pe_header_t)) {
        log_error(PE_LOG_TAG, "File too small for PE header at offset 0x%X", 
                  dos_header->e_lfanew);
        return PE_ERROR_NOT_PE_FILE;
    }

    // Check PE signature
    const pe_header_t* pe_header = (const pe_header_t*)((const uint8_t*)file_data + dos_header->e_lfanew);
    if (pe_header->Signature != PE_SIGNATURE) {
        log_error(PE_LOG_TAG, "Invalid PE signature: 0x%08X, expected 0x%08X", 
                  pe_header->Signature, PE_SIGNATURE);
        return PE_ERROR_NOT_PE_FILE;
    }

    // Check machine type
    if (pe_header->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 && 
        pe_header->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        log_error(PE_LOG_TAG, "Unsupported machine type: 0x%04X", 
                  pe_header->FileHeader.Machine);
        return PE_ERROR_UNSUPPORTED_MACHINE;
    }

    // All basic validations passed
    return PE_SUCCESS;
}

// Function to convert an RVA to an actual memory address
void* pe_rva_to_ptr(pe_image_t* image, uint32_t rva) {
    if (!image || !image->base_address) {
        return NULL;
    }
    
    return (void*)((uint8_t*)image->base_address + rva);
}

// Find a section containing the given RVA
static section_header_t* pe_find_section(const void* file_data, const pe_header_t* pe_header, uint32_t rva) {
    bool is_64bit = (pe_header->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    uint16_t section_count = pe_header->FileHeader.NumberOfSections;
    
    // Calculate the start of the section headers
    const section_header_t* sections = NULL;
    if (is_64bit) {
        sections = (const section_header_t*)((const uint8_t*)pe_header + 
                   sizeof(uint32_t) + sizeof(file_header_t) + pe_header->FileHeader.SizeOfOptionalHeader);
    } else {
        sections = (const section_header_t*)((const uint8_t*)pe_header + 
                   sizeof(uint32_t) + sizeof(file_header_t) + pe_header->FileHeader.SizeOfOptionalHeader);
    }
    
    // Iterate through sections to find the one containing the RVA
    for (uint16_t i = 0; i < section_count; i++) {
        const section_header_t* section = &sections[i];
        if (rva >= section->VirtualAddress && 
            rva < section->VirtualAddress + section->VirtualSize) {
            return (section_header_t*)section;
        }
    }
    
    return NULL;
}

// Map protection flags from section characteristics
static uint32_t pe_map_section_protection(uint32_t characteristics) {
    uint32_t protection = PROT_NONE;
    
    if (characteristics & IMAGE_SCN_MEM_READ) {
        protection |= PROT_READ;
    }
    if (characteristics & IMAGE_SCN_MEM_WRITE) {
        protection |= PROT_WRITE;
    }
    if (characteristics & IMAGE_SCN_MEM_EXECUTE) {
        protection |= PROT_EXEC;
    }
    
    return protection;
}

// Apply relocations to a PE image
static pe_error_t pe_apply_relocations(pe_image_t* image, const pe_header_t* pe_header, 
                                      const void* file_data) {
    bool is_64bit = image->is_64bit;
    uint64_t image_base_diff = 0;
    
    // Calculate the difference between the actual and preferred base address
    if (is_64bit) {
        image_base_diff = (uint64_t)image->base_address - pe_header->OptionalHeader64.ImageBase;
    } else {
        image_base_diff = (uint64_t)image->base_address - pe_header->OptionalHeader32.ImageBase;
    }
    
    // If the image is loaded at its preferred base address, no relocations needed
    if (image_base_diff == 0) {
        log_debug(PE_LOG_TAG, "No relocations needed, image loaded at preferred base 0x%llX", 
                 (unsigned long long)image->base_address);
        return PE_SUCCESS;
    }
    
    // Get the base relocation directory
    const data_directory_t* reloc_dir = NULL;
    if (is_64bit) {
        reloc_dir = &pe_header->OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    } else {
        reloc_dir = &pe_header->OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    }
    
    // If there's no relocation information, we can't relocate
    if (reloc_dir->VirtualAddress == 0 || reloc_dir->Size == 0) {
        log_error(PE_LOG_TAG, "No relocation information available, but needed");
        return PE_ERROR_RELOCATION;
    }
    
    log_debug(PE_LOG_TAG, "Applying relocations, image base diff: 0x%llX", 
             (unsigned long long)image_base_diff);
    
    // Process relocation blocks
    uint8_t* reloc_base = (uint8_t*)pe_rva_to_ptr(image, reloc_dir->VirtualAddress);
    const base_relocation_block_t* block = (const base_relocation_block_t*)reloc_base;
    uint32_t remaining_size = reloc_dir->Size;
    
    while (remaining_size > 0 && block->SizeOfBlock > 0) {
        uint32_t va = block->VirtualAddress;
        uint32_t count = (block->SizeOfBlock - sizeof(base_relocation_block_t)) / sizeof(uint16_t);
        
        // Get the relocation entries
        const uint16_t* entries = (const uint16_t*)(block + 1);
        
        // Process each relocation entry
        for (uint32_t i = 0; i < count; i++) {
            uint16_t entry = entries[i];
            uint16_t type = (entry >> 12) & 0xF;
            uint16_t offset = entry & 0xFFF;
            
            // Calculate the address to relocate
            uint8_t* relocate_addr = (uint8_t*)image->base_address + va + offset;
            
            // Apply the relocation based on type
            switch (type) {
                case 0: // IMAGE_REL_BASED_ABSOLUTE
                    // Do nothing, this is a padding entry
                    break;
                
                case 3: // IMAGE_REL_BASED_HIGHLOW (32-bit)
                    {
                        uint32_t* addr = (uint32_t*)relocate_addr;
                        *addr = (uint32_t)(*addr + image_base_diff);
                    }
                    break;
                
                case 10: // IMAGE_REL_BASED_DIR64 (64-bit)
                    if (is_64bit) {
                        uint64_t* addr = (uint64_t*)relocate_addr;
                        *addr = *addr + image_base_diff;
                    } else {
                        log_error(PE_LOG_TAG, "64-bit relocation in 32-bit image");
                        return PE_ERROR_RELOCATION;
                    }
                    break;
                
                default:
                    log_warning(PE_LOG_TAG, "Unsupported relocation type: %u", type);
                    break;
            }
        }
        
        // Move to the next block
        remaining_size -= block->SizeOfBlock;
        block = (const base_relocation_block_t*)((const uint8_t*)block + block->SizeOfBlock);
    }
    
    log_info(PE_LOG_TAG, "Relocations applied successfully");
    return PE_SUCCESS;
}

// Resolve imports for a PE image
static pe_error_t pe_resolve_imports(pe_image_t* image, const pe_header_t* pe_header,
                                    const void* file_data, pe_loader_config_t* config) {
    if (!config->resolve_imports || !config->import_resolver) {
        log_debug(PE_LOG_TAG, "Import resolution disabled or no resolver provided");
        return PE_SUCCESS;
    }
    
    bool is_64bit = image->is_64bit;
    
    // Get the import directory
    const data_directory_t* import_dir = NULL;
    if (is_64bit) {
        import_dir = &pe_header->OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    } else {
        import_dir = &pe_header->OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    }
    
    // If there's no import information, nothing to do
    if (import_dir->VirtualAddress == 0 || import_dir->Size == 0) {
        log_debug(PE_LOG_TAG, "No imports to resolve");
        return PE_SUCCESS;
    }
    
    // Process import descriptors
    import_descriptor_t* import_desc = (import_descriptor_t*)pe_rva_to_ptr(image, import_dir->VirtualAddress);
    
    while (import_desc->Name != 0) {
        // Get the DLL name
        char* dll_name = (char*)pe_rva_to_ptr(image, import_desc->Name);
        log_debug(PE_LOG_TAG, "Resolving imports from: %s", dll_name);
        
        // Process import thunks
        uint32_t thunk_rva = import_desc->OriginalFirstThunk ? 
                            import_desc->OriginalFirstThunk : 
                            import_desc->FirstThunk;
        
        if (thunk_rva == 0) {
            log_warning(PE_LOG_TAG, "No thunks found for %s", dll_name);
            import_desc++;
            continue;
        }
        
        if (is_64bit) {
            import_thunk64_t* thunk = (import_thunk64_t*)pe_rva_to_ptr(image, thunk_rva);
            import_thunk64_t* iat = (import_thunk64_t*)pe_rva_to_ptr(image, import_desc->FirstThunk);
            
            while (thunk->u1.Function != 0) {
                // Check if it's an ordinal or a named import
                if (thunk->u1.Ordinal & 0x8000000000000000ULL) {
                    // Ordinal import
                    uint16_t ordinal = (uint16_t)(thunk->u1.Ordinal & 0xFFFF);
                    log_debug(PE_LOG_TAG, "  Ordinal import #%u", ordinal);
                    
                    // Resolve the function
                    void* func_addr = config->import_resolver(dll_name, (const char*)(uintptr_t)ordinal);
                    if (!func_addr) {
                        log_error(PE_LOG_TAG, "Failed to resolve ordinal import %u from %s", 
                                 ordinal, dll_name);
                        return PE_ERROR_IMPORT_RESOLUTION;
                    }
                    
                    // Update the IAT
                    iat->u1.Function = (uint64_t)func_addr;
                } else {
                    // Named import
                    import_thunk64_t* name_data = (import_thunk64_t*)pe_rva_to_ptr(
                                                 image, (uint32_t)thunk->u1.AddressOfData);
                    char* func_name = (char*)((uint8_t*)name_data + 2); // Skip hint
                    
                    log_debug(PE_LOG_TAG, "  Named import: %s", func_name);
                    
                    // Resolve the function
                    void* func_addr = config->import_resolver(dll_name, func_name);
                    if (!func_addr) {
                        log_error(PE_LOG_TAG, "Failed to resolve import %s from %s", 
                                 func_name, dll_name);
                        return PE_ERROR_IMPORT_RESOLUTION;
                    }
                    
                    // Update the IAT
                    iat->u1.Function = (uint64_t)func_addr;
                }
                
                // Move to the next thunk
                thunk++;
                iat++;
            }
        } else {
            // 32-bit equivalent logic
            import_thunk32_t* thunk = (import_thunk32_t*)pe_rva_to_ptr(image, thunk_rva);
            import_thunk32_t* iat = (import_thunk32_t*)pe_rva_to_ptr(image, import_desc->FirstThunk);
            
            while (thunk->u1.AddressOfData != 0) {
                // Check if it's an ordinal or a named import
                if (thunk->u1.Ordinal & 0x80000000) {
                    // Ordinal import
                    uint16_t ordinal = (uint16_t)(thunk->u1.Ordinal & 0xFFFF);
                    log_debug(PE_LOG_TAG, "  Ordinal import #%u", ordinal);
                    
                    // Resolve the function
                    void* func_addr = config->import_resolver(dll_name, (const char*)(uintptr_t)ordinal);
                    if (!func_addr) {
                        log_error(PE_LOG_TAG, "Failed to resolve ordinal import %u from %s", 
                                 ordinal, dll_name);
                        return PE_ERROR_IMPORT_RESOLUTION;
                    }
                    
                    // Update the IAT
                    iat->u1.Function = (uint32_t)func_addr;
                } else {
                    // Named import
                    uint32_t name_rva = thunk->u1.AddressOfData;
                    uint8_t* name_data = (uint8_t*)pe_rva_to_ptr(image, name_rva);
                    char* func_name = (char*)(name_data + 2); // Skip hint
                    
                    log_debug(PE_LOG_TAG, "  Named import: %s", func_name);
                    
                    // Resolve the function
                    void* func_addr = config->import_resolver(dll_name, func_name);
                    if (!func_addr) {
                        log_error(PE_LOG_TAG, "Failed to resolve import %s from %s", 
                                 func_name, dll_name);
                        return PE_ERROR_IMPORT_RESOLUTION;
                    }
                    
                    // Update the IAT
                    iat->u1.Function = (uint32_t)func_addr;
                }
                
                // Move to the next thunk
                thunk++;
                iat++;
            }
        }
        
        // Move to the next import descriptor
        import_desc++;
    }
    
    log_info(PE_LOG_TAG, "Imports resolved successfully");
    return PE_SUCCESS;
}

// Get a function from the export directory
void* pe_get_export(pe_image_t* image, const char* function_name) {
    if (!image || !image->base_address || !function_name) {
        return NULL;
    }
    
    // If export directory hasn't been located yet, we need to find it
    if (!image->export_directory) {
        pe_header_t* pe_header = (pe_header_t*)((uint8_t*)image->base_address + 
                                ((dos_header_t*)image->base_address)->e_lfanew);
        
        const data_directory_t* export_dir = NULL;
        if (image->is_64bit) {
            export_dir = &pe_header->OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        } else {
            export_dir = &pe_header->OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        }
        
        if (export_dir->VirtualAddress == 0 || export_dir->Size == 0) {
            log_error(PE_LOG_TAG, "Image has no export directory");
            return NULL;
        }
        
        image->export_directory = pe_rva_to_ptr(image, export_dir->VirtualAddress);
    }
    
    export_directory_t* exports = (export_directory_t*)image->export_directory;
    
    // Get arrays of names, functions, and ordinals
    uint32_t* names = (uint32_t*)pe_rva_to_ptr(image, exports->AddressOfNames);
    uint16_t* ordinals = (uint16_t*)pe_rva_to_ptr(image, exports->AddressOfNameOrdinals);
    uint32_t* functions = (uint32_t*)pe_rva_to_ptr(image, exports->AddressOfFunctions);
    
    // Search for the function by name
    for (uint32_t i = 0; i < exports->NumberOfNames; i++) {
        char* name = (char*)pe_rva_to_ptr(image, names[i]);
        
        if (strcmp(name, function_name) == 0) {
            uint16_t ordinal = ordinals[i];
            
            if (ordinal >= exports->NumberOfFunctions) {
                log_error(PE_LOG_TAG, "Export ordinal %u out of range (max %u)", 
                         ordinal, exports->NumberOfFunctions);
                return NULL;
            }
            
            uint32_t func_rva = functions[ordinal];
            return pe_rva_to_ptr(image, func_rva);
        }
    }
    
    log_error(PE_LOG_TAG, "Export function '%s' not found", function_name);
    return NULL;
}

// Get a function from the export directory by ordinal
void* pe_get_export_by_ordinal(pe_image_t* image, uint16_t ordinal) {
    if (!image || !image->base_address) {
        return NULL;
    }
    
    // If export directory hasn't been located yet, we need to find it
    if (!image->export_directory) {
        pe_header_t* pe_header = (pe_header_t*)((uint8_t*)image->base_address + 
                                ((dos_header_t*)image->base_address)->e_lfanew);
        
        const data_directory_t* export_dir = NULL;
        if (image->is_64bit) {
            export_dir = &pe_header->OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        } else {
            export_dir = &pe_header->OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        }
        
        if (export_dir->VirtualAddress == 0 || export_dir->Size == 0) {
            log_error(PE_LOG_TAG, "Image has no export directory");
            return NULL;
        }
        
        image->export_directory = pe_rva_to_ptr(image, export_dir->VirtualAddress);
    }
    
    export_directory_t* exports = (export_directory_t*)image->export_directory;
    
    // Check if the ordinal is in range
    ordinal -= exports->Base;  // Adjust by export base
    
    if (ordinal >= exports->NumberOfFunctions) {
        log_error(PE_LOG_TAG, "Export ordinal %u out of range (max %u, base %u)", 
                 ordinal + exports->Base, exports->NumberOfFunctions, exports->Base);
        return NULL;
    }
    
    // Get function address
    uint32_t* functions = (uint32_t*)pe_rva_to_ptr(image, exports->AddressOfFunctions);
    uint32_t func_rva = functions[ordinal];
    
    return pe_rva_to_ptr(image, func_rva);
}

// Load a PE file from memory
pe_error_t pe_load_from_memory(const void* file_data, uint64_t file_size, 
                               pe_loader_config_t* config, pe_image_t* image) {
    if (!file_data || !config || !image) {
        log_error(PE_LOG_TAG, "Invalid parameters");
        return PE_ERROR_INVALID_PARAMETER;
    }
    
    // Validate the PE file
    pe_error_t result = pe_validate(file_data, file_size);
    if (result != PE_SUCCESS) {
        return result;
    }
    
    // Get headers
    const dos_header_t* dos_header = (const dos_header_t*)file_data;
    const pe_header_t* pe_header = (const pe_header_t*)((const uint8_t*)file_data + dos_header->e_lfanew);
    
    // Determine if it's a 64-bit PE file
    bool is_64bit = (pe_header->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    
    // Check subsystem (we only support native drivers)
    uint16_t subsystem = is_64bit ? pe_header->OptionalHeader64.Subsystem : 
                                   pe_header->OptionalHeader32.Subsystem;
    
    if (subsystem != IMAGE_SUBSYSTEM_NATIVE) {
        log_error(PE_LOG_TAG, "Unsupported subsystem: %u, expected %u (NATIVE)", 
                 subsystem, IMAGE_SUBSYSTEM_NATIVE);
        return PE_ERROR_UNSUPPORTED_SUBSYSTEM;
    }
    
    // Get image information
    uint64_t image_size = is_64bit ? pe_header->OptionalHeader64.SizeOfImage : 
                                    pe_header->OptionalHeader32.SizeOfImage;
    uint64_t image_base = is_64bit ? pe_header->OptionalHeader64.ImageBase : 
                                    pe_header->OptionalHeader32.ImageBase;
    uint32_t entry_point_rva = is_64bit ? pe_header->OptionalHeader64.AddressOfEntryPoint : 
                                         pe_header->OptionalHeader32.AddressOfEntryPoint;
    
    log_debug(PE_LOG_TAG, "PE Image: Size=%llu, Base=0x%llX, EntryPoint=+0x%X", 
             (unsigned long long)image_size, (unsigned long long)image_base, entry_point_rva);
    
    // Determine actual base address
    void* base_address = NULL;
    
    if (config->preferred_base_address != 0) {
        base_address = (void*)config->preferred_base_address;
    } else {
        base_address = (void*)image_base;
    }
    
    // Allocate memory for the image (aligned to page size)
    image_size = align_up(image_size, PAGE_SIZE);
    void* allocated_memory = pe_malloc(image_size);
    
    if (!allocated_memory) {
        return PE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Zero out the memory
    memset(allocated_memory, 0, image_size);
    base_address = allocated_memory;
    
    log_debug(PE_LOG_TAG, "Allocated memory at 0x%llX for image", (unsigned long long)base_address);
    
    // Load headers
    uint32_t headers_size = is_64bit ? pe_header->OptionalHeader64.SizeOfHeaders : 
                                      pe_header->OptionalHeader32.SizeOfHeaders;
    memcpy(base_address, file_data, headers_size);
    
    // Load sections
    uint16_t section_count = pe_header->FileHeader.NumberOfSections;
    const section_header_t* sections = (const section_header_t*)(
        (const uint8_t*)pe_header + sizeof(uint32_t) + 
        sizeof(file_header_t) + pe_header->FileHeader.SizeOfOptionalHeader
    );
    
    for (uint16_t i = 0; i < section_count; i++) {
        const section_header_t* section = &sections[i];
        
        // Calculate destination address
        void* dest = (uint8_t*)base_address + section->VirtualAddress;
        
        // Copy section data
        if (section->SizeOfRawData > 0) {
            const void* src = (const uint8_t*)file_data + section->PointerToRawData;
            uint32_t copy_size = section->SizeOfRawData;
            
            // Ensure we don't copy more than the virtual size
            if (copy_size > section->VirtualSize) {
                copy_size = section->VirtualSize;
            }
            
            log_debug(PE_LOG_TAG, "Loading section '%8.8s' to 0x%llX (size %u bytes)",
                     section->Name, (unsigned long long)dest, copy_size);
            
            memcpy(dest, src, copy_size);
        }
        
        // If this is a code section, determine the memory protection
        if (config->map_sections) {
            uint32_t protection = pe_map_section_protection(section->Characteristics);
            
            // In a real implementation, you'd set memory protection here
            // For example: mprotect(dest, section->VirtualSize, protection);
            
            log_debug(PE_LOG_TAG, "  Protection: %s%s%s", 
                     (protection & PROT_READ) ? "R" : "-",
                     (protection & PROT_WRITE) ? "W" : "-",
                     (protection & PROT_EXEC) ? "X" : "-");
        }
    }
    
    // Initialize the image information
    image->base_address = base_address;
    image->image_size = image_size;
    image->is_64bit = is_64bit;
    image->machine_type = pe_header->FileHeader.Machine;
    image->original_image_base = image_base;
    image->timestamp = pe_header->FileHeader.TimeDateStamp;
    image->entry_point = (uint8_t*)base_address + entry_point_rva;
    image->export_directory = NULL;  // Will be populated later if needed
    
    // Extract name if available
    memset(image->name, 0, sizeof(image->name));
    
    // Extract name from export directory if available
    const data_directory_t* export_dir = NULL;
    if (is_64bit) {
        export_dir = &pe_header->OptionalHeader64.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else {
        export_dir = &pe_header->OptionalHeader32.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    }
    
    if (export_dir->VirtualAddress != 0 && export_dir->Size != 0) {
        export_directory_t* exports = (export_directory_t*)((uint8_t*)base_address + export_dir->VirtualAddress);
        if (exports->Name != 0) {
            const char* module_name = (const char*)((uint8_t*)base_address + exports->Name);
            strncpy(image->name, module_name, sizeof(image->name) - 1);
        }
        
        image->export_directory = exports;
    }
    
    // If no name was extracted, use a generic one
    if (image->name[0] == '\0') {
        strcpy(image->name, "unknown");
    }
    
    // If needed, apply relocations
    if (config->relocate && base_address != (void*)image_base) {
        result = pe_apply_relocations(image, pe_header, file_data);
        if (result != PE_SUCCESS) {
            pe_unload(image);
            return result;
        }
    }
    
    // If needed, resolve imports
    if (config->resolve_imports) {
        result = pe_resolve_imports(image, pe_header, file_data, config);
        if (result != PE_SUCCESS) {
            pe_unload(image);
            return result;
        }
    }
    
    log_info(PE_LOG_TAG, "PE image '%s' loaded successfully at 0x%llX", 
             image->name, (unsigned long long)base_address);
    
    return PE_SUCCESS;
}

// Load a PE file from disk
pe_error_t pe_load_from_file(const char* filename, pe_loader_config_t* config, pe_image_t* image) {
    if (!filename || !config || !image) {
        log_error(PE_LOG_TAG, "Invalid parameters");
        return PE_ERROR_INVALID_PARAMETER;
    }
    
    // Read the file into memory
    uint64_t file_size = 0;
    void* file_data = pe_read_file(filename, &file_size);
    
    if (!file_data) {
        return PE_ERROR_FILE_READ;
    }
    
    // Load the PE from memory
    pe_error_t result = pe_load_from_memory(file_data, file_size, config, image);
    
    // Free the file data
    free(file_data);
    
    return result;
}

// Unload a PE image
pe_error_t pe_unload(pe_image_t* image) {
    if (!image || !image->base_address) {
        return PE_ERROR_INVALID_PARAMETER;
    }
    
    // In a real implementation with memory mapping, you would need to:
    // 1. Unmap sections with proper memory protection
    // 2. Call any necessary cleanup routines
    
    log_info(PE_LOG_TAG, "Unloading PE image '%s' from 0x%llX", 
             image->name, (unsigned long long)image->base_address);
    
    // Free the allocated memory
    free(image->base_address);
    
    // Clear the image structure
    memset(image, 0, sizeof(pe_image_t));
    
    return PE_SUCCESS;
}