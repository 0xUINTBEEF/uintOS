#include "elf_loader.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "memory/aslr.h"
#include "logging/log.h"
#include "task.h"
#include "scheduler.h"
#include "syscall.h"
#include "vfs/vfs.h"
#include <string.h>

// Size of user stack (16 MB)
#define USER_STACK_SIZE (16 * 1024 * 1024)
// Base virtual address for loading user programs
#define USER_BASE_ADDR 0x400000

// Validate an ELF header
bool elf_validate_header(Elf64_Ehdr* header) {
    if (!header) {
        return false;
    }
    
    // Check ELF magic number
    if (header->e_ident[EI_MAG0] != 0x7F || 
        header->e_ident[EI_MAG1] != 'E' ||
        header->e_ident[EI_MAG2] != 'L' ||
        header->e_ident[EI_MAG3] != 'F') {
        log_error("Invalid ELF magic number");
        return false;
    }
    
    // Check ELF class (64-bit)
    if (header->e_ident[EI_CLASS] != ELFCLASS64) {
        log_error("Only 64-bit ELF files are supported");
        return false;
    }
    
    // Check data encoding (little endian)
    if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
        log_error("Only little-endian ELF files are supported");
        return false;
    }
    
    // Check ELF version
    if (header->e_ident[EI_VERSION] != EV_CURRENT || header->e_version != EV_CURRENT) {
        log_error("Unsupported ELF version");
        return false;
    }
    
    // Check machine type (x86-64)
    if (header->e_machine != EM_X86_64) {
        log_error("Unsupported machine type: %d", header->e_machine);
        return false;
    }
    
    // Check file type (executable)
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        log_error("File is not an executable or shared object");
        return false;
    }
    
    // Must have an entry point
    if (header->e_entry == 0) {
        log_error("ELF file has no entry point");
        return false;
    }
    
    // Must have program headers
    if (header->e_phnum == 0 || header->e_phentsize == 0) {
        log_error("ELF file has no program headers");
        return false;
    }
    
    return true;
}

// Load an ELF file into memory
elf_load_status_t elf_load_executable(const char* path, elf_process_info_t* process_info) {
    if (!path || !process_info) {
        return ELF_LOAD_INVALID_MAGIC;
    }
    
    // Initialize process info
    memset(process_info, 0, sizeof(elf_process_info_t));
    
    // Open the file using VFS
    file_handle_t file;
    int result = vfs_open(path, VFS_O_RDONLY, &file);
    if (result != 0) {
        log_error("Failed to open ELF file: %s", path);
        return ELF_LOAD_IO_ERROR;
    }
    
    // Get file size
    size_t file_size;
    result = vfs_size(&file, &file_size);
    if (result != 0 || file_size == 0) {
        log_error("Failed to get ELF file size or file is empty");
        vfs_close(&file);
        return ELF_LOAD_IO_ERROR;
    }
    
    // Allocate memory for file data
    void* file_data = heap_alloc(file_size);
    if (!file_data) {
        log_error("Failed to allocate memory for ELF file");
        vfs_close(&file);
        return ELF_LOAD_MEMORY_ERROR;
    }
    
    // Read the file into memory
    size_t bytes_read;
    result = vfs_read(&file, file_data, file_size, &bytes_read);
    vfs_close(&file);
    
    if (result != 0 || bytes_read != file_size) {
        log_error("Failed to read ELF file data");
        heap_free(file_data);
        return ELF_LOAD_IO_ERROR;
    }
    
    // Validate ELF header
    Elf64_Ehdr* header = (Elf64_Ehdr*)file_data;
    if (!elf_validate_header(header)) {
        heap_free(file_data);
        return ELF_LOAD_INVALID_MAGIC;
    }
    
    // Map ELF segments into memory
    elf_load_status_t status = elf_map_segments(header, file_data, file_size, process_info);
    if (status != ELF_LOAD_SUCCESS) {
        heap_free(file_data);
        return status;
    }
      // Set entry point, applying load bias for PIE
    if (process_info->is_pie) {
        process_info->entry_point = (void*)(header->e_entry + process_info->load_bias);
    } else {
        process_info->entry_point = (void*)header->e_entry;
    }
    
    // Check if this is a dynamically linked executable
    Elf64_Phdr* phdr = (Elf64_Phdr*)((uintptr_t)file_data + header->e_phoff);
    for (int i = 0; i < header->e_phnum; i++, phdr++) {
        if (phdr->p_type == PT_INTERP) {
            // This is a dynamically linked executable with an interpreter
            char* interp = (char*)((uintptr_t)file_data + phdr->p_offset);
            
            // Allocate memory for interpreter path and copy it
            process_info->interpreter_path = heap_alloc(phdr->p_filesz);
            if (process_info->interpreter_path) {
                memcpy(process_info->interpreter_path, interp, phdr->p_filesz);
                log_info("Dynamic executable with interpreter: %s", process_info->interpreter_path);
                // In a full implementation, we would load and use the dynamic linker here
            }
            break;
        }
    }
    
    // Free the original file data, as segments have been mapped
    heap_free(file_data);
    
    return ELF_LOAD_SUCCESS;
}

// Map ELF segments into memory
elf_load_status_t elf_map_segments(Elf64_Ehdr* header, void* file_data, size_t file_size, 
                                 elf_process_info_t* process_info) {
    if (!header || !file_data || !process_info) {
        return ELF_LOAD_INVALID_MAGIC;
    }
    
    // Get program headers
    Elf64_Phdr* phdr = (Elf64_Phdr*)((uintptr_t)file_data + header->e_phoff);
      // First pass: identify memory boundaries
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    
    for (int i = 0; i < header->e_phnum; i++, phdr++) {
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        
        if (phdr->p_vaddr < min_vaddr) {
            min_vaddr = phdr->p_vaddr;
        }
        
        uint64_t segment_end = phdr->p_vaddr + phdr->p_memsz;
        if (segment_end > max_vaddr) {
            max_vaddr = segment_end;
        }
    }
    
    // Round down min_vaddr to page boundary
    min_vaddr &= ~(PAGE_SIZE - 1);
    
    // Round up max_vaddr to page boundary
    max_vaddr = (max_vaddr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Calculate total memory size
    size_t total_size = max_vaddr - min_vaddr;
    process_info->total_memory = total_size;
    
    // Check if this is a PIE (Position Independent Executable)
    bool is_pie = (header->e_type == ET_DYN);
    process_info->is_pie = is_pie;
    
    // For PIE executables, apply ASLR if enabled
    uint64_t load_bias = 0;
    if (is_pie && aslr_is_enabled()) {
        // Apply ASLR to executables - generate a random base address
        load_bias = (uint64_t)aslr_get_random_offset(ASLR_EXEC_OFFSET);
        // Ensure proper alignment for the load bias
        load_bias = (load_bias & ~(PAGE_SIZE - 1));
        process_info->load_bias = load_bias;
        
        log_info("PIE executable detected, applying ASLR with bias: 0x%llx", load_bias);
    }
    
    log_info("ELF memory range: 0x%llx - 0x%llx (size: %llu bytes)", 
             min_vaddr, max_vaddr, total_size);
    
    // Second pass: load segments into memory
    phdr = (Elf64_Phdr*)((uintptr_t)file_data + header->e_phoff);
    
    for (int i = 0; i < header->e_phnum; i++, phdr++) {
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
          // Calculate segment addresses, aligned to page boundaries
        uint64_t seg_vaddr = phdr->p_vaddr;
        uint64_t seg_size = phdr->p_memsz;
        uint64_t file_offset = phdr->p_offset;
        uint64_t file_size_seg = phdr->p_filesz;
        
        // Apply load bias for PIE executables
        if (process_info->is_pie) {
            seg_vaddr += process_info->load_bias;
        }
        
        // Flags for memory mapping
        uint32_t prot = 0;
        if (phdr->p_flags & PF_R) prot |= VMM_PAGE_READ;
        if (phdr->p_flags & PF_W) prot |= VMM_PAGE_WRITE;
        if (phdr->p_flags & PF_X) prot |= VMM_PAGE_EXECUTE;
        
        // Map memory for this segment
        void* segment_vaddr = (void*)seg_vaddr;
        
        // Create memory mapping for the segment with user permissions
        vmm_map_range(
            (vmm_context_t*)vmm_get_current_context(),
            segment_vaddr,
            seg_size,
            prot | VMM_PAGE_USER);
        
        // Copy segment data from file
        if (file_size_seg > 0) {
            memcpy(segment_vaddr, (void*)((uintptr_t)file_data + file_offset), file_size_seg);
        }
        
        // Initialize BSS (if any)
        if (file_size_seg < seg_size) {
            memset(
                (void*)((uintptr_t)segment_vaddr + file_size_seg),
                0,
                seg_size - file_size_seg);
        }
        
        // Store segment information
        if (phdr->p_flags & PF_X) {
            // Text segment
            if (!process_info->text_start || seg_vaddr < (uint64_t)process_info->text_start) {
                process_info->text_start = (void*)seg_vaddr;
            }
            if (!process_info->text_end || seg_vaddr + seg_size > (uint64_t)process_info->text_end) {
                process_info->text_end = (void*)(seg_vaddr + seg_size);
            }
        } else if (phdr->p_flags & PF_W) {
            // Data segment (including BSS)
            if (file_size_seg < seg_size) {
                // Contains BSS
                if (!process_info->bss_start || 
                    seg_vaddr + file_size_seg < (uint64_t)process_info->bss_start) {
                    process_info->bss_start = (void*)(seg_vaddr + file_size_seg);
                }
                if (!process_info->bss_end || 
                    seg_vaddr + seg_size > (uint64_t)process_info->bss_end) {
                    process_info->bss_end = (void*)(seg_vaddr + seg_size);
                }
            }
            
            // Data segment proper
            if (!process_info->data_start || seg_vaddr < (uint64_t)process_info->data_start) {
                process_info->data_start = (void*)seg_vaddr;
            }
            if (!process_info->data_end || 
                seg_vaddr + (file_size_seg ? file_size_seg : seg_size) > (uint64_t)process_info->data_end) {
                process_info->data_end = (void*)(seg_vaddr + (file_size_seg ? file_size_seg : seg_size));
            }
        }
        
        log_debug("Mapped segment at 0x%llx - 0x%llx (flags: %x)", 
                 seg_vaddr, seg_vaddr + seg_size, phdr->p_flags);
    }
    
    // Set up program break (heap start)
    process_info->program_break = process_info->bss_end;
      // Allocate and map user stack
    uint64_t stack_top = 0x7FFFFFFFF000ULL;  // Near top of user virtual address space
    
    // Apply ASLR to stack if enabled
    if (aslr_is_enabled()) {
        // Randomize stack location
        stack_top = (uint64_t)aslr_randomize_address((void*)stack_top, ASLR_STACK_OFFSET);
        // Ensure proper alignment
        stack_top = (stack_top & ~0xFULL);
        log_info("ASLR applied to stack: 0x%llx", stack_top);
    }
    
    uint64_t stack_bottom = stack_top - USER_STACK_SIZE;
    
    // Map stack memory
    vmm_map_range(
        (vmm_context_t*)vmm_get_current_context(),
        (void*)stack_bottom,
        USER_STACK_SIZE,
        VMM_PAGE_READ | VMM_PAGE_WRITE | VMM_PAGE_USER);
    
    // Store stack information
    process_info->stack_bottom = (void*)stack_top;
    
    return ELF_LOAD_SUCCESS;
}

// Create a process from a loaded ELF executable
int elf_create_process(elf_process_info_t* process_info, const char* process_name, 
                       int parent_id, task_t** new_task) {
    if (!process_info || !process_info->entry_point) {
        return -1;
    }
    
    // Create a new task
    task_t* task = (task_t*)heap_alloc(sizeof(task_t));
    if (!task) {
        return -1;
    }
    
    // Initialize task structure
    memset(task, 0, sizeof(task_t));
    
    // Set task name
    if (process_name) {
        strncpy(task->name, process_name, sizeof(task->name) - 1);
    } else {
        strcpy(task->name, "elf_process");
    }
    
    // Set task properties
    task->parent_id = parent_id;
    task->priority = TASK_PRIORITY_NORMAL;
    task->state = TASK_STATE_READY;
    task->flags = TASK_FLAG_USER;
    task->entry_point = process_info->entry_point;
    
    // Create stack for the task
    task->stack_size = KERNEL_STACK_SIZE;
    task->stack = heap_alloc(task->stack_size);
    if (!task->stack) {
        heap_free(task);
        return -1;
    }    // Set up task context for ELF process
    task_setup_context(task);
    
    // Configure task context for user mode
    task_setup_user_context(task, process_info->entry_point, (void*)process_info->stack_bottom);
    
    // Register task in scheduler
    int task_id = scheduler_create_task_from_state(task);
    if (task_id < 0) {
        heap_free(task->stack);
        heap_free(task);
        return -1;
    }
    
    // Create memory space for the process (with ASLR)
    int vm_result = vmm_create_process_space(task_id);
    if (vm_result != 0) {
        log_error("ELF", "Failed to create memory space for process %d", task_id);
        scheduler_remove_task(task_id);
        heap_free(task->stack);
        heap_free(task);
        return -1;
    }
    
    if (new_task) {
        *new_task = task;
    }
    
    return task_id;
}

// Free resources associated with an ELF process
void elf_free_process(elf_process_info_t* process_info) {
    if (!process_info) {
        return;
    }
    
    // Free interpreter path if it was allocated
    if (process_info->interpreter_path) {
        heap_free(process_info->interpreter_path);
        process_info->interpreter_path = NULL;
    }
    
    // Note: We don't free the memory mappings here as they are part of the task's
    // memory space and will be freed when the task is terminated
}

// Execute an ELF binary (fork + execve combination)
int elf_execute(const char* path, char* const argv[], char* const envp[]) {
    // In a full implementation, this would create a new process and replace its
    // image with the loaded ELF executable. For simplicity, we'll just load the
    // ELF file and create a new process directly.
    
    elf_process_info_t process_info;
    elf_load_status_t status = elf_load_executable(path, &process_info);
    
    if (status != ELF_LOAD_SUCCESS) {
        log_error("Failed to load ELF executable: %d", status);
        return -1;
    }
    
    // Create a new process from the loaded ELF executable
    task_t* new_task = NULL;
    int task_id = elf_create_process(
        &process_info, 
        path, 
        scheduler_get_current_task() ? scheduler_get_current_task()->id : 0,
        &new_task);
    
    if (task_id < 0) {
        log_error("Failed to create process for ELF executable");
        elf_free_process(&process_info);
        return -1;
    }
    
    // In a full implementation, we would pass argv and envp to the new process
    
    // Free process info resources
    elf_free_process(&process_info);
    
    return task_id;
}

// Find a symbol in an ELF executable
void* elf_find_symbol(Elf64_Ehdr* header, const char* symbol_name) {
    if (!header || !symbol_name) {
        return NULL;
    }
    
    // First, find the symbol table section
    Elf64_Shdr* shdr = (Elf64_Shdr*)((uintptr_t)header + header->e_shoff);
    Elf64_Shdr* symtab = NULL;
    Elf64_Shdr* strtab = NULL;
    
    // Find the section header string table
    if (header->e_shstrndx == SHN_UNDEF) {
        return NULL; // No section header string table
    }
    
    char* shstrtab = (char*)((uintptr_t)header + shdr[header->e_shstrndx].sh_offset);
    
    // Find symbol table and associated string table
    for (int i = 0; i < header->e_shnum; i++) {
        char* name = shstrtab + shdr[i].sh_name;
        
        if (shdr[i].sh_type == SHT_SYMTAB && strcmp(name, ".symtab") == 0) {
            symtab = &shdr[i];
            strtab = &shdr[shdr[i].sh_link];
            break;
        }
    }
    
    if (!symtab || !strtab) {
        return NULL; // No symbol table found
    }
    
    // Get pointers to symbol table and string table
    Elf64_Sym* symbols = (Elf64_Sym*)((uintptr_t)header + symtab->sh_offset);
    char* strtable = (char*)((uintptr_t)header + strtab->sh_offset);
    
    // Calculate the number of symbols
    int num_symbols = symtab->sh_size / symtab->sh_entsize;
    
    // Search for the symbol by name
    for (int i = 0; i < num_symbols; i++) {
        char* name = strtable + symbols[i].st_name;
        if (name && strcmp(name, symbol_name) == 0) {
            return (void*)symbols[i].st_value;
        }
    }
    
    return NULL; // Symbol not found
}

// System call handlers

// Handle execve syscall
int sys_execve(const char* path, char* const argv[], char* const envp[]) {
    if (!path) {
        return -1;
    }
    
    task_t* current_task = scheduler_get_current_task();
    if (!current_task) {
        return -1;
    }
    
    // Load the new executable
    elf_process_info_t process_info;
    elf_load_status_t status = elf_load_executable(path, &process_info);
    
    if (status != ELF_LOAD_SUCCESS) {
        log_error("execve: Failed to load ELF executable: %d", status);
        return -1;
    }
    
    // Replace the current task's memory space with the new one
    // In a real implementation, this would require cleaning up the current
    // memory mappings and replacing them with the new ones
    
    // Update task information
    current_task->entry_point = process_info.entry_point;
    
    // Set up new context with the loaded executable
    task_setup_user_context(
        current_task, 
        process_info.entry_point, 
        (void*)process_info.stack_bottom);
    
    // In a full implementation, we would set up argv and envp
    
    // Free process info resources
    elf_free_process(&process_info);
    
    // Force a context switch to start the new executable
    scheduler_yield();
    
    // Should never get here
    return 0;
}

// Handle fork syscall
int sys_fork(void) {
    task_t* parent_task = scheduler_get_current_task();
    if (!parent_task) {
        return -1;
    }
    
    // Create a new task structure
    task_t* child_task = (task_t*)heap_alloc(sizeof(task_t));
    if (!child_task) {
        return -1;
    }
    
    // Copy task information from parent
    memcpy(child_task, parent_task, sizeof(task_t));
    
    // Update child task information
    child_task->id = 0; // Will be assigned by scheduler
    child_task->parent_id = parent_task->id;
    
    // Set child task name
    snprintf(child_task->name, sizeof(child_task->name), "%s_child", parent_task->name);
    
    // Allocate stack for the child
    child_task->stack = heap_alloc(child_task->stack_size);
    if (!child_task->stack) {
        heap_free(child_task);
        return -1;
    }
    
    // Copy parent's stack to child
    memcpy(child_task->stack, parent_task->stack, child_task->stack_size);
    
    // Set up new context for the child (copy parent's context)
    task_copy_context(child_task, parent_task);
    
    // In a full implementation, we would also clone the parent's memory space
    // by creating a new page directory and copying all mappings
    
    // Register child in scheduler
    int child_id = scheduler_create_task_from_state(child_task);
    if (child_id < 0) {
        heap_free(child_task->stack);
        heap_free(child_task);
        return -1;
    }
    
    // Set up different return values for parent and child
    // For parent: return child's ID
    // For child: return 0
    task_set_syscall_return(parent_task, child_id);
    task_set_syscall_return(child_task, 0);
    
    return child_id;
}

// Handle exit syscall
void sys_exit(int status) {
    task_t* current_task = scheduler_get_current_task();
    if (!current_task) {
        return;
    }
    
    // Set task status to zombie and store exit code
    log_info("Process %d (%s) exited with status %d", 
             current_task->id, current_task->name, status);
    
    // Terminate the task
    scheduler_terminate_task(current_task->id, status);
    
    // This should never be reached, as scheduler_terminate_task()
    // will switch to another task if terminating the current one
    while (1) {
        scheduler_yield();
    }
}