#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "task.h"

// ELF header constants
#define ELF_MAGIC                 0x464C457F  // "\x7FELF" in little endian

// ELF file types
#define ET_NONE                   0           // No file type
#define ET_REL                    1           // Relocatable file
#define ET_EXEC                   2           // Executable file
#define ET_DYN                    3           // Shared object file
#define ET_CORE                   4           // Core file

// ELF machine types (architecture)
#define EM_NONE                   0           // No machine
#define EM_386                    3           // Intel 80386
#define EM_X86_64                 62          // AMD x86-64

// ELF version
#define EV_CURRENT                1           // Current version

// ELF identification indexes
#define EI_MAG0                   0           // File identification
#define EI_MAG1                   1           // File identification
#define EI_MAG2                   2           // File identification
#define EI_MAG3                   3           // File identification
#define EI_CLASS                  4           // File class
#define EI_DATA                   5           // Data encoding
#define EI_VERSION                6           // File version
#define EI_OSABI                  7           // Operating system/ABI identification
#define EI_ABIVERSION             8           // ABI version
#define EI_PAD                    9           // Start of padding bytes
#define EI_NIDENT                 16          // Size of e_ident[]

// ELF class types
#define ELFCLASSNONE              0           // Invalid class
#define ELFCLASS32                1           // 32-bit objects
#define ELFCLASS64                2           // 64-bit objects

// ELF data encoding
#define ELFDATANONE               0           // Invalid data encoding
#define ELFDATA2LSB               1           // 2's complement, little endian
#define ELFDATA2MSB               2           // 2's complement, big endian

// ELF OS ABI types
#define ELFOSABI_SYSV             0           // UNIX System V ABI
#define ELFOSABI_HPUX             1           // HP-UX
#define ELFOSABI_STANDALONE       255         // Standalone (embedded) application

// ELF segment types
#define PT_NULL                   0           // Program header table entry unused
#define PT_LOAD                   1           // Loadable program segment
#define PT_DYNAMIC                2           // Dynamic linking information
#define PT_INTERP                 3           // Program interpreter
#define PT_NOTE                   4           // Auxiliary information
#define PT_SHLIB                  5           // Reserved
#define PT_PHDR                   6           // Entry for header table itself

// ELF segment flags
#define PF_X                      0x1         // Executable segment
#define PF_W                      0x2         // Writable segment
#define PF_R                      0x4         // Readable segment

// ELF section types
#define SHT_NULL                  0           // Section header table entry unused
#define SHT_PROGBITS              1           // Program data
#define SHT_SYMTAB                2           // Symbol table
#define SHT_STRTAB                3           // String table
#define SHT_RELA                  4           // Relocation entries with addends
#define SHT_HASH                  5           // Symbol hash table
#define SHT_DYNAMIC               6           // Dynamic linking information
#define SHT_NOTE                  7           // Notes
#define SHT_NOBITS                8           // Program space with no data (bss)
#define SHT_REL                   9           // Relocation entries, no addends
#define SHT_DYNSYM                11          // Dynamic linker symbol table

// ELF section flags
#define SHF_WRITE                 0x1         // Writable section
#define SHF_ALLOC                 0x2         // Occupies memory during execution
#define SHF_EXECINSTR             0x4         // Executable section
#define SHF_MERGE                 0x10        // Might be merged
#define SHF_STRINGS               0x20        // Contains null-terminated strings

// Special section indexes
#define SHN_UNDEF                 0           // Undefined section
#define SHN_ABS                   0xfff1      // Associated symbol is absolute
#define SHN_COMMON                0xfff2      // Associated symbol is common

// 64-bit ELF data types
typedef uint64_t Elf64_Addr;      // Unsigned program address
typedef uint64_t Elf64_Off;       // Unsigned file offset
typedef uint16_t Elf64_Half;      // Unsigned medium integer
typedef uint32_t Elf64_Word;      // Unsigned integer
typedef int32_t  Elf64_Sword;     // Signed integer
typedef uint64_t Elf64_Xword;     // Unsigned long integer
typedef int64_t  Elf64_Sxword;    // Signed long integer

// ELF header (64-bit)
typedef struct {
    unsigned char   e_ident[EI_NIDENT];    // ELF identification bytes
    Elf64_Half      e_type;                // Object file type
    Elf64_Half      e_machine;             // Architecture
    Elf64_Word      e_version;             // Object file version
    Elf64_Addr      e_entry;               // Entry point virtual address
    Elf64_Off       e_phoff;               // Program header table file offset
    Elf64_Off       e_shoff;               // Section header table file offset
    Elf64_Word      e_flags;               // Processor-specific flags
    Elf64_Half      e_ehsize;              // ELF header size in bytes
    Elf64_Half      e_phentsize;           // Program header table entry size
    Elf64_Half      e_phnum;               // Program header table entry count
    Elf64_Half      e_shentsize;           // Section header table entry size
    Elf64_Half      e_shnum;               // Section header table entry count
    Elf64_Half      e_shstrndx;            // Section header string table index
} Elf64_Ehdr;

// Program header (64-bit)
typedef struct {
    Elf64_Word      p_type;                // Segment type
    Elf64_Word      p_flags;               // Segment flags
    Elf64_Off       p_offset;              // Segment file offset
    Elf64_Addr      p_vaddr;               // Segment virtual address
    Elf64_Addr      p_paddr;               // Segment physical address
    Elf64_Xword     p_filesz;              // Segment size in file
    Elf64_Xword     p_memsz;               // Segment size in memory
    Elf64_Xword     p_align;               // Segment alignment
} Elf64_Phdr;

// Section header (64-bit)
typedef struct {
    Elf64_Word      sh_name;               // Section name (string tbl index)
    Elf64_Word      sh_type;               // Section type
    Elf64_Xword     sh_flags;              // Section flags
    Elf64_Addr      sh_addr;               // Section virtual addr at execution
    Elf64_Off       sh_offset;             // Section file offset
    Elf64_Xword     sh_size;               // Section size in bytes
    Elf64_Word      sh_link;               // Link to another section
    Elf64_Word      sh_info;               // Additional section information
    Elf64_Xword     sh_addralign;          // Section alignment
    Elf64_Xword     sh_entsize;            // Entry size if section holds table
} Elf64_Shdr;

// Symbol table entry (64-bit)
typedef struct {
    Elf64_Word      st_name;               // Symbol name (string tbl index)
    unsigned char   st_info;               // Symbol type and binding
    unsigned char   st_other;              // Symbol visibility
    Elf64_Half      st_shndx;              // Section index
    Elf64_Addr      st_value;              // Symbol value
    Elf64_Xword     st_size;               // Symbol size
} Elf64_Sym;

// Relocation entry with addend (64-bit)
typedef struct {
    Elf64_Addr      r_offset;              // Address
    Elf64_Xword     r_info;                // Relocation type and symbol index
    Elf64_Sxword    r_addend;              // Addend
} Elf64_Rela;

// Dynamic structure (64-bit)
typedef struct {
    Elf64_Sxword    d_tag;                 // Dynamic entry type
    union {
        Elf64_Xword d_val;                 // Integer value
        Elf64_Addr  d_ptr;                 // Address value
    } d_un;
} Elf64_Dyn;

// ELF process loading results
typedef enum {
    ELF_LOAD_SUCCESS = 0,              // Successful load
    ELF_LOAD_INVALID_MAGIC = -1,       // Invalid ELF magic number
    ELF_LOAD_INVALID_CLASS = -2,       // Invalid ELF class
    ELF_LOAD_INVALID_DATA = -3,        // Invalid ELF data encoding
    ELF_LOAD_INVALID_VERSION = -4,     // Invalid ELF version
    ELF_LOAD_INVALID_MACHINE = -5,     // Unsupported machine type
    ELF_LOAD_INVALID_TYPE = -6,        // Not an executable file
    ELF_LOAD_NO_ENTRY = -7,            // No entry point
    ELF_LOAD_NO_PHEADER = -8,          // No program headers
    ELF_LOAD_MEMORY_ERROR = -9,        // Memory allocation error
    ELF_LOAD_IO_ERROR = -10,           // File read error
    ELF_LOAD_CORRUPTED = -11,          // Corrupted ELF file
    ELF_LOAD_UNSUPPORTED = -12         // Unsupported ELF feature
} elf_load_status_t;

// ELF process information
typedef struct {
    void* entry_point;                 // Process entry point
    void* program_break;               // Program break (heap start)
    void* stack_bottom;                // Bottom of stack (high address)
    void* text_start;                  // Start of text segment
    void* text_end;                    // End of text segment
    void* data_start;                  // Start of data segment
    void* data_end;                    // End of data segment
    void* bss_start;                   // Start of BSS segment
    void* bss_end;                     // End of BSS segment
    size_t total_memory;               // Total memory used by process
    char* interpreter_path;            // Dynamic interpreter path (if any)
} elf_process_info_t;

// Function to validate an ELF header
bool elf_validate_header(Elf64_Ehdr* header);

// Function to load an ELF file into memory and create a process
elf_load_status_t elf_load_executable(const char* path, elf_process_info_t* process_info);

// Function to create a process from loaded ELF executable
int elf_create_process(elf_process_info_t* process_info, const char* process_name, 
                     int parent_id, task_t** new_task);

// Function to perform memory mapping for ELF segments
elf_load_status_t elf_map_segments(Elf64_Ehdr* header, void* file_data, size_t file_size, 
                                 elf_process_info_t* process_info);

// Function to free resources associated with an ELF process
void elf_free_process(elf_process_info_t* process_info);

// Function to find a symbol in an ELF executable
void* elf_find_symbol(Elf64_Ehdr* header, const char* symbol_name);

// Function to execute an ELF binary (fork + execve combination)
int elf_execute(const char* path, char* const argv[], char* const envp[]);

// Function to handle execve syscall
int sys_execve(const char* path, char* const argv[], char* const envp[]);

// Function to handle fork syscall
int sys_fork(void);

// Function to handle exit syscall
void sys_exit(int status);

#endif /* ELF_LOADER_H */