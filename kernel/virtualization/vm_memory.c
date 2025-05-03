/**
 * @file vm_memory.c
 * @brief Memory virtualization implementation for VM support
 *
 * This file implements memory virtualization features including address
 * translation, memory allocation, and Extended Page Tables (EPT) support.
 */

#include "vm_memory.h"
#include "vmx.h"
#include "../logging/log.h"
#include "../../memory/paging.h"
#include "../../memory/heap.h"
#include <string.h>

// Define the VM memory log tag
#define VM_MEM_LOG_TAG "VM_MEM"

// Error codes for vm_memory functions
#define VM_MEM_SUCCESS                 0
#define VM_MEM_ERROR_NOT_INITIALIZED   -1
#define VM_MEM_ERROR_VM_NOT_FOUND      -2
#define VM_MEM_ERROR_INSUFFICIENT_MEM  -3
#define VM_MEM_ERROR_INVALID_PARAM     -4
#define VM_MEM_ERROR_MAPPING_FAILED    -5
#define VM_MEM_ERROR_ADDRESS_NOT_FOUND -6
#define VM_MEM_ERROR_EPT_UNSUPPORTED   -7
#define VM_MEM_ERROR_EPT_SETUP_FAILED  -8

// Per-VM memory allocation tracking
typedef struct vm_memory_block {
    uint32_t vm_id;
    void* virtual_address;
    uint32_t physical_address;
    uint32_t size;
    struct vm_memory_block* next;
} vm_memory_block_t;

// Global memory tracking list
static vm_memory_block_t* memory_blocks = NULL;

// EPT (Extended Page Tables) page table structures
typedef struct ept_pml4e {
    uint64_t read:1;
    uint64_t write:1;
    uint64_t execute:1;
    uint64_t reserved_1:5;
    uint64_t accessed:1;
    uint64_t ignored_1:1;
    uint64_t user_mode_execute:1;
    uint64_t ignored_2:1;
    uint64_t pdpt_addr:40;
    uint64_t reserved_2:12;
} __attribute__((packed)) ept_pml4e_t;

typedef struct ept_pdpte {
    uint64_t read:1;
    uint64_t write:1;
    uint64_t execute:1;
    uint64_t reserved_1:5;
    uint64_t accessed:1;
    uint64_t ignored_1:1;
    uint64_t user_mode_execute:1;
    uint64_t ignored_2:1;
    uint64_t pd_addr:40;
    uint64_t reserved_2:12;
} __attribute__((packed)) ept_pdpte_t;

// Initialize the memory virtualization subsystem
int vm_memory_init() {
    log_info(VM_MEM_LOG_TAG, "Initializing VM memory subsystem");
    
    // Nothing special to initialize for now
    memory_blocks = NULL;
    
    log_info(VM_MEM_LOG_TAG, "VM memory subsystem initialized successfully");
    return VM_MEM_SUCCESS;
}

// Allocate physical memory for a virtual machine
void* vm_memory_allocate(uint32_t vm_id, uint32_t size) {
    if (size == 0) {
        log_error(VM_MEM_LOG_TAG, "Invalid memory allocation size");
        return NULL;
    }
    
    // Round up to page size
    uint32_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t actual_size = page_count * PAGE_SIZE;
    
    log_debug(VM_MEM_LOG_TAG, "Allocating %d bytes (%d pages) for VM %d", 
             actual_size, page_count, vm_id);
    
    // Allocate memory
    void* virtual_address = allocate_pages(page_count);
    if (!virtual_address) {
        log_error(VM_MEM_LOG_TAG, "Failed to allocate %d pages for VM %d", page_count, vm_id);
        return NULL;
    }
    
    // Get the physical address
    uint32_t physical_address = 0;
    // In a real OS, you'd use a function to get the physical address from virtual
    // For simplicity, we'll use a placeholder calculation
    physical_address = (uint32_t)virtual_address; // This is a simplification
    
    // Create a tracking block
    vm_memory_block_t* block = (vm_memory_block_t*)malloc(sizeof(vm_memory_block_t));
    if (!block) {
        log_error(VM_MEM_LOG_TAG, "Failed to allocate memory tracking block");
        free_pages(virtual_address, page_count);
        return NULL;
    }
    
    // Initialize the block
    block->vm_id = vm_id;
    block->virtual_address = virtual_address;
    block->physical_address = physical_address;
    block->size = actual_size;
    block->next = memory_blocks;
    memory_blocks = block;
    
    log_debug(VM_MEM_LOG_TAG, "Allocated memory for VM %d: virtual=0x%p, physical=0x%x, size=%d",
             vm_id, virtual_address, physical_address, actual_size);
    
    return virtual_address;
}

// Free previously allocated VM memory
int vm_memory_free(uint32_t vm_id, void* addr, uint32_t size) {
    if (!addr || size == 0) {
        return VM_MEM_ERROR_INVALID_PARAM;
    }
    
    // Find the block to free
    vm_memory_block_t* current = memory_blocks;
    vm_memory_block_t* prev = NULL;
    
    while (current) {
        if (current->vm_id == vm_id && current->virtual_address == addr) {
            // Found the block
            if (prev) {
                prev->next = current->next;
            } else {
                memory_blocks = current->next;
            }
            
            // Calculate page count
            uint32_t page_count = (current->size + PAGE_SIZE - 1) / PAGE_SIZE;
            
            // Free the memory
            free_pages(current->virtual_address, page_count);
            
            log_debug(VM_MEM_LOG_TAG, "Freed memory for VM %d: virtual=0x%p, size=%d",
                     vm_id, addr, current->size);
            
            // Free the tracking block
            free(current);
            
            return VM_MEM_SUCCESS;
        }
        
        prev = current;
        current = current->next;
    }
    
    log_warn(VM_MEM_LOG_TAG, "Attempted to free unknown memory block: VM %d, addr=0x%p",
            vm_id, addr);
    
    return VM_MEM_ERROR_ADDRESS_NOT_FOUND;
}

// Map a physical address into a VM's address space
int vm_memory_map(uint32_t vm_id, uint32_t guest_virtual, uint32_t host_physical, 
                  uint32_t size, int writable, int executable) {
    // Find the VM
    vm_instance_t* vm = NULL;
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].id == vm_id && vm_instances[i].state != VM_STATE_UNINITIALIZED) {
            vm = &vm_instances[i];
            break;
        }
    }
    
    if (!vm) {
        log_error(VM_MEM_LOG_TAG, "VM with ID %d not found", vm_id);
        return VM_MEM_ERROR_VM_NOT_FOUND;
    }
    
    // Calculate the number of pages to map
    uint32_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    log_debug(VM_MEM_LOG_TAG, "Mapping %d bytes (%d pages) for VM %d: guest=0x%x, host=0x%x",
             size, page_count, vm_id, guest_virtual, host_physical);
    
    // Switch to the VM's address space
    uint32_t current_cr3 = paging_get_current_address_space();
    paging_switch_address_space(vm->cr3);
    
    // Map each page
    uint32_t flags = PAGE_FLAG_PRESENT;
    if (writable) flags |= PAGE_FLAG_WRITABLE;
    if (executable) flags |= PAGE_FLAG_USER; // In x86, no execute bit is the inverse of user bit
    
    uint32_t result = VM_MEM_SUCCESS;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t g_addr = guest_virtual + (i * PAGE_SIZE);
        uint32_t h_addr = host_physical + (i * PAGE_SIZE);
        
        // Map the page
        map_page((void*)h_addr, (void*)g_addr, flags);
    }
    
    // Switch back to the original address space
    paging_switch_address_space(current_cr3);
    
    log_debug(VM_MEM_LOG_TAG, "Memory mapping completed for VM %d", vm_id);
    
    return result;
}

// Translate a guest virtual address to host physical address
int vm_memory_translate(uint32_t vm_id, uint32_t guest_virtual, uint32_t* host_physical) {
    if (!host_physical) {
        return VM_MEM_ERROR_INVALID_PARAM;
    }
    
    // Find the VM
    vm_instance_t* vm = NULL;
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].id == vm_id && vm_instances[i].state != VM_STATE_UNINITIALIZED) {
            vm = &vm_instances[i];
            break;
        }
    }
    
    if (!vm) {
        log_error(VM_MEM_LOG_TAG, "VM with ID %d not found", vm_id);
        return VM_MEM_ERROR_VM_NOT_FOUND;
    }
    
    // Switch to the VM's address space
    uint32_t current_cr3 = paging_get_current_address_space();
    paging_switch_address_space(vm->cr3);
    
    // Get the physical address
    // In a real implementation, we would use the page tables to translate
    // For now, we'll use a simplistic approach
    *host_physical = guest_virtual; // This is a simplification
    
    // Switch back to the original address space
    paging_switch_address_space(current_cr3);
    
    return VM_MEM_SUCCESS;
}

// Setup Extended Page Tables (EPT) for a virtual machine
int vm_memory_setup_ept(uint32_t vm_id) {
    // Find the VM
    vm_instance_t* vm = NULL;
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].id == vm_id && vm_instances[i].state != VM_STATE_UNINITIALIZED) {
            vm = &vm_instances[i];
            break;
        }
    }
    
    if (!vm) {
        log_error(VM_MEM_LOG_TAG, "VM with ID %d not found", vm_id);
        return VM_MEM_ERROR_VM_NOT_FOUND;
    }
    
    log_info(VM_MEM_LOG_TAG, "Setting up EPT for VM %d", vm_id);
    
    // Check for EPT support
    // In a real implementation, this would involve checking CPUID and MSRs
    // For now, we'll assume it's supported
    
    // Allocate memory for EPT structures
    // In a real implementation, this would involve setting up a multi-level page table
    void* ept_pml4 = allocate_pages(1);
    if (!ept_pml4) {
        log_error(VM_MEM_LOG_TAG, "Failed to allocate memory for EPT structures");
        return VM_MEM_ERROR_INSUFFICIENT_MEM;
    }
    
    // Initialize EPT structures
    memset(ept_pml4, 0, PAGE_SIZE);
    
    // TODO: Set up EPT entries
    // For a real implementation, this would involve complex page table setup
    
    // For now, we'll just log a message and consider it done
    log_info(VM_MEM_LOG_TAG, "EPT setup completed for VM %d (simulated)", vm_id);
    
    return VM_MEM_SUCCESS;
}

// Allocate memory for the guest VM physical memory
void *vm_memory_allocate_physical(size_t size) {
    // Use HAL memory allocation for aligned physical memory
    return hal_memory_allocate_physical(size, PAGE_SIZE_4K);
}

// Map guest memory to EPT structures
int vm_memory_map_ept(ept_pml4e_t *ept_pml4, uint64_t guest_physical, uint64_t host_physical, size_t size, uint32_t permissions) {
    // Calculate number of pages to map
    size_t pages = (size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
    
    for (size_t i = 0; i < pages; i++) {
        uint64_t gpa = guest_physical + (i * PAGE_SIZE_4K);
        uint64_t hpa = host_physical + (i * PAGE_SIZE_4K);
        
        // Use HAL memory functions to ensure proper physical address translation
        uint64_t real_hpa = hal_memory_virtual_to_physical((void*)hpa);
        
        int result = ept_map_page(ept_pml4, gpa, real_hpa, permissions);
        if (result != 0) {
            return result;
        }
    }
    
    // Flush TLB using HAL CPU function to ensure EPT changes take effect
    hal_cpu_invept_all_contexts();
    
    return 0;
}

// Free guest VM physical memory
void vm_memory_free_physical(void *memory, size_t size) {
    hal_memory_free_physical(memory);
}