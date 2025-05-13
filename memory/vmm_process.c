/**
 * @file vmm_process.c
 * @brief Virtual Memory Manager - Process-specific functions
 *
 * This file implements process-related virtual memory management
 * including creating and destroying process address spaces.
 */

#include "vmm.h"
#include "heap.h"
#include "paging.h"
#include "aslr.h"
#include "../kernel/logging/log.h"
#include <string.h>

// External VMM structures and functions
extern int vmm_initialized;
extern address_space_t* vmm_kernel_space;
extern address_space_t* vmm_current_space;
extern address_space_t* vmm_create_address_space(void);
extern void vmm_destroy_address_space(address_space_t* space);
extern void vmm_switch_address_space(address_space_t* space);

// Process address space tracking
#define MAX_PROCESSES 256
static address_space_t* process_spaces[MAX_PROCESSES];

/**
 * Create address space for a new process
 * 
 * @param process_id ID of the process
 * @return 0 on success, -1 on failure
 */
int vmm_create_process_space(int process_id) {
    if (process_id < 0 || process_id >= MAX_PROCESSES) {
        log_error("VMM", "Invalid process ID: %d", process_id);
        return -1;
    }
    
    if (process_spaces[process_id]) {
        log_warning("VMM", "Address space for process %d already exists", process_id);
        return -1;
    }
    
    log_info("VMM", "Creating address space for process %d", process_id);
    
    // Create a new address space (ASLR happens in vmm_create_address_space)
    address_space_t* space = vmm_create_address_space();
    if (!space) {
        log_error("VMM", "Failed to create address space for process %d", process_id);
        return -1;
    }
    
    // Set the process ID in the address space
    space->id = process_id;
    
    // Create default memory regions with ASLR randomization

    // 1. Program code segment (not randomized)
    uint32_t code_start = 0x08048000; // Traditional ELF load address
    uint32_t code_end = 0x08400000;   // ~4MB for code

    // 2. Heap segment (randomized)
    uint32_t heap_base = 0x08400000;  // Default start after code
    heap_base = (uint32_t)aslr_randomize_address((void*)heap_base, ASLR_HEAP_OFFSET);
    uint32_t heap_start = heap_base;
    uint32_t heap_end = heap_start + (4*1024*1024); // 4MB initial heap

    // 3. Shared memory region (randomized)
    uint32_t shmem_base = 0x30000000; // Default location
    shmem_base = (uint32_t)aslr_randomize_address((void*)shmem_base, ASLR_MMAP_OFFSET);
    
    // 4. Libraries region (randomized)
    uint32_t lib_base = 0x40000000;   // Default location
    lib_base = (uint32_t)aslr_randomize_address((void*)lib_base, ASLR_LIB_OFFSET);
    
    // 5. Stack region (randomized)
    uint32_t stack_top = 0xBFFFFFFF;  // Default user stack top
    stack_top = (uint32_t)aslr_randomize_address((void*)stack_top, ASLR_STACK_OFFSET);
    uint32_t stack_bottom = stack_top - (1024*1024); // 1MB stack

    // Add the memory regions to the address space
    // Code segment (read + execute)
    vmm_add_region(space, code_start, code_end, 
                  VM_PERM_READ | VM_PERM_EXEC | VM_PERM_USER, 
                  VM_TYPE_USER, "code");
                  
    // Heap segment (read + write)
    vmm_add_region(space, heap_start, heap_end, 
                  VM_PERM_READ | VM_PERM_WRITE | VM_PERM_USER, 
                  VM_TYPE_HEAP, "heap");
                  
    // Shared memory area (empty initially)
    vmm_add_region(space, shmem_base, shmem_base + (16*1024*1024), 
                  VM_PERM_READ | VM_PERM_WRITE | VM_PERM_USER, 
                  VM_TYPE_SHARED, "shared");
                  
    // Libraries area (read + execute)
    vmm_add_region(space, lib_base, lib_base + (64*1024*1024), 
                  VM_PERM_READ | VM_PERM_EXEC | VM_PERM_USER, 
                  VM_TYPE_MODULE, "libraries");
                  
    // Stack area (read + write)
    vmm_add_region(space, stack_bottom, stack_top, 
                  VM_PERM_READ | VM_PERM_WRITE | VM_PERM_USER | VM_FLAG_STACK, 
                  VM_TYPE_STACK, "stack");

    // Log ASLR status
    if (aslr_is_enabled()) {
        log_info("VMM", "ASLR applied to process %d: heap=0x%08X, stack=0x%08X, libs=0x%08X",
                process_id, heap_start, stack_top, lib_base);
    }
    
    // Store the address space
    process_spaces[process_id] = space;
    
    return 0;
}

/**
 * Destroy address space for a process
 * 
 * @param process_id ID of the process
 */
void vmm_destroy_process_space(int process_id) {
    if (process_id < 0 || process_id >= MAX_PROCESSES) {
        log_warning("VMM", "Invalid process ID: %d", process_id);
        return;
    }
    
    address_space_t* space = process_spaces[process_id];
    if (!space) {
        log_warning("VMM", "No address space for process %d", process_id);
        return;
    }
    
    log_info("VMM", "Destroying address space for process %d", process_id);
    
    // If this is the current space, switch to kernel space first
    if (space == vmm_current_space) {
        vmm_switch_address_space(vmm_kernel_space);
    }
    
    // Destroy the address space
    vmm_destroy_address_space(space);
    
    // Clear the entry
    process_spaces[process_id] = NULL;
}

/**
 * Switch to a process's address space
 * 
 * @param process_id ID of the process
 * @return 0 on success, -1 on failure
 */
int vmm_switch_to_process(int process_id) {
    if (process_id < 0 || process_id >= MAX_PROCESSES) {
        log_error("VMM", "Invalid process ID: %d", process_id);
        return -1;
    }
    
    address_space_t* space = process_spaces[process_id];
    if (!space) {
        log_error("VMM", "No address space for process %d", process_id);
        return -1;
    }
    
    // Switch to the process's address space
    vmm_switch_address_space(space);
    
    return 0;
}

/**
 * Map a shared memory region between processes
 * 
 * @param source_proc ID of the source process
 * @param source_addr Start address in source process
 * @param target_proc ID of the target process
 * @param target_addr Start address in target process
 * @param size Size of the region in bytes
 * @param flags Protection flags
 * @return 0 on success, -1 on failure
 */
int vmm_share_memory(int source_proc, void* source_addr,
                    int target_proc, void* target_addr,
                    size_t size, uint32_t flags) {
    if (source_proc < 0 || source_proc >= MAX_PROCESSES ||
        target_proc < 0 || target_proc >= MAX_PROCESSES) {
        log_error("VMM", "Invalid process ID");
        return -1;
    }
    
    address_space_t* source_space = process_spaces[source_proc];
    address_space_t* target_space = process_spaces[target_proc];
    
    if (!source_space || !target_space) {
        log_error("VMM", "Process address space not found");
        return -1;
    }
    
    // Round addresses to page boundaries
    uint32_t src_start = ((uint32_t)source_addr / PAGE_SIZE) * PAGE_SIZE;
    uint32_t tgt_start = ((uint32_t)target_addr / PAGE_SIZE) * PAGE_SIZE;
    
    // Round size up to page boundary
    size_t page_size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // Create memory regions
    vm_region_t* src_region = vmm_add_region(source_space, src_start, src_start + page_size,
                                           flags | VM_FLAG_SHARED, VM_TYPE_SHARED, 
                                           "shared_source");
    
    vm_region_t* tgt_region = vmm_add_region(target_space, tgt_start, tgt_start + page_size,
                                           flags | VM_FLAG_SHARED, VM_TYPE_SHARED, 
                                           "shared_target");
    
    if (!src_region || !tgt_region) {
        log_error("VMM", "Failed to create shared memory regions");
        return -1;
    }
    
    // Create page mappings
    // Note: We need to actually implement the shared mapping logic here
    // This is just a placeholder implementation
    
    return 0;
}
