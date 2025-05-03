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

// EPT page sizes
#define EPT_PAGE_SIZE_4K  4096
#define EPT_PAGE_SIZE_2M  (2 * 1024 * 1024)
#define EPT_PAGE_SIZE_1G  (1024 * 1024 * 1024)

// Memory type definitions for EPT
#define EPT_MEMORY_TYPE_UC     0   // Uncacheable
#define EPT_MEMORY_TYPE_WC     1   // Write combining
#define EPT_MEMORY_TYPE_WT     4   // Write through
#define EPT_MEMORY_TYPE_WP     5   // Write protected
#define EPT_MEMORY_TYPE_WB     6   // Write back

// Define EPT permission flags
#define EPT_PERM_READ      0x01
#define EPT_PERM_WRITE     0x02
#define EPT_PERM_EXECUTE   0x04

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

// EPT PDE structure for mapping 2MB pages
typedef struct ept_pde {
    uint64_t read:1;
    uint64_t write:1;
    uint64_t execute:1;
    uint64_t memory_type:3;
    uint64_t ignore_pat:1;
    uint64_t large_page:1;
    uint64_t accessed:1;
    uint64_t dirty:1;
    uint64_t user_mode_execute:1;
    uint64_t ignored_2:1;
    uint64_t page_frame_number:40;
    uint64_t reserved:12;
} __attribute__((packed)) ept_pde_t;

// EPT PTE structure for mapping 4KB pages
typedef struct ept_pte {
    uint64_t read:1;
    uint64_t write:1;
    uint64_t execute:1;
    uint64_t memory_type:3;
    uint64_t ignore_pat:1;
    uint64_t ignored_1:1;
    uint64_t accessed:1;
    uint64_t dirty:1;
    uint64_t user_mode_execute:1;
    uint64_t ignored_2:1;
    uint64_t page_frame_number:40;
    uint64_t reserved:12;
} __attribute__((packed)) ept_pte_t;

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
    
    // Check for EPT support using HAL CPU function
    // This would be a real check in a production implementation
    // For now we'll assume EPT is supported
    vm->supports_ept = 1;
    
    if (!vm->supports_ept) {
        log_error(VM_MEM_LOG_TAG, "EPT not supported by CPU");
        return VM_MEM_ERROR_EPT_UNSUPPORTED;
    }
    
    // Allocate memory for EPT PML4 table - must be physically contiguous
    void* ept_pml4_virt = allocate_pages(1);  // 4KB aligned page
    if (!ept_pml4_virt) {
        log_error(VM_MEM_LOG_TAG, "Failed to allocate memory for EPT PML4");
        return VM_MEM_ERROR_INSUFFICIENT_MEM;
    }
    
    // Clear the new EPT PML4 table
    memset(ept_pml4_virt, 0, PAGE_SIZE);
    
    // Get the physical address of the EPT PML4 table (simplified for demo)
    uint64_t ept_pml4_phys = (uint64_t)ept_pml4_virt;
    
    // Store EPT PML4 pointer in VM instance
    vm->ept_pml4 = ept_pml4_virt;
    
    // Create the EPTP (Extended Page Table Pointer) for the VMCS
    // EPTP layout:
    // - Bits 2:0: EPT memory type (6 = write-back)
    // - Bits 5:3: EPT page walk length - 1 (3 = 4 level page walk)
    // - Bit 6: Enable accessed and dirty flags
    // - Bits 11:7: Reserved (must be 0)
    // - Bits N-1:12: Physical address of EPT PML4 table
    // - Bits 63:N: Reserved (must be 0), where N is CPU's physical address width
    vm->eptp = (ept_pml4_phys & 0xFFFFFFFFF000ULL) |
               (0ULL << 3) |  // Page walk length (4 levels)
               (EPT_MEMORY_TYPE_WB);
    
    // Log the EPTP value
    log_debug(VM_MEM_LOG_TAG, "EPTP for VM %d: 0x%llx", vm_id, vm->eptp);
    
    // Map the first 4MB of physical memory as an example
    // In a real implementation, you would map according to VM memory requirements
    log_debug(VM_MEM_LOG_TAG, "Mapping initial 4MB of physical memory for VM %d", vm_id);
    
    // Map with all permissions (R/W/X)
    int result = vm_memory_map_ept((ept_pml4e_t*)ept_pml4_virt, 
                                  0x0,         // Guest physical starts at 0
                                  0x0,         // Host physical starts at 0
                                  4 * 1024 * 1024,  // 4MB
                                  EPT_PERM_READ | EPT_PERM_WRITE | EPT_PERM_EXECUTE);
    
    if (result != 0) {
        log_error(VM_MEM_LOG_TAG, "Failed to map initial memory for VM %d: %d", vm_id, result);
        free_pages(ept_pml4_virt, 1);
        return VM_MEM_ERROR_EPT_SETUP_FAILED;
    }
    
    // Map MMIO regions (example: VGA memory)
    // This is important for device interaction through memory-mapped I/O
    log_debug(VM_MEM_LOG_TAG, "Mapping VGA MMIO region for VM %d", vm_id);
    result = vm_memory_map_ept((ept_pml4e_t*)ept_pml4_virt,
                               0xA0000,       // VGA memory starts at 0xA0000
                               0xA0000,       // Same physical address in host
                               0x20000,       // 128KB size
                               EPT_PERM_READ | EPT_PERM_WRITE);
    
    if (result != 0) {
        log_error(VM_MEM_LOG_TAG, "Failed to map VGA MMIO for VM %d: %d", vm_id, result);
        free_pages(ept_pml4_virt, 1);
        return VM_MEM_ERROR_EPT_SETUP_FAILED;
    }
    
    log_info(VM_MEM_LOG_TAG, "EPT setup completed for VM %d", vm_id);
    
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

/**
 * Map a 4KB page in EPT structures
 * 
 * @param ept_pml4 Pointer to EPT PML4 table
 * @param guest_physical Guest physical address
 * @param host_physical Host physical address
 * @param permissions Access permissions (read/write/execute)
 * @return 0 on success, error code on failure
 */
int ept_map_page(ept_pml4e_t* ept_pml4, uint64_t guest_physical, uint64_t host_physical, uint32_t permissions) {
    // Extract indices for each level
    uint64_t pml4_index = (guest_physical >> 39) & 0x1FF;
    uint64_t pdpt_index = (guest_physical >> 30) & 0x1FF;
    uint64_t pd_index = (guest_physical >> 21) & 0x1FF;
    uint64_t pt_index = (guest_physical >> 12) & 0x1FF;
    
    // Check if the PML4 entry exists
    if (!(ept_pml4[pml4_index].read)) {
        // Need to allocate a new PDPT
        ept_pdpte_t* pdpt = (ept_pdpte_t*)allocate_pages(1);
        if (!pdpt) {
            log_error(VM_MEM_LOG_TAG, "Failed to allocate PDPT");
            return VM_MEM_ERROR_INSUFFICIENT_MEM;
        }
        
        // Clear the new table
        memset(pdpt, 0, PAGE_SIZE);
        
        // Setup the PML4 entry
        ept_pml4[pml4_index].read = 1;
        ept_pml4[pml4_index].write = 1;
        ept_pml4[pml4_index].execute = 1;
        ept_pml4[pml4_index].pdpt_addr = (uint64_t)pdpt >> 12;
    }
    
    // Get PDPT
    ept_pdpte_t* pdpt = (ept_pdpte_t*)(ept_pml4[pml4_index].pdpt_addr << 12);
    
    // Check if the PDPT entry exists
    if (!(pdpt[pdpt_index].read)) {
        // Need to allocate a new PD
        ept_pde_t* pd = (ept_pde_t*)allocate_pages(1);
        if (!pd) {
            log_error(VM_MEM_LOG_TAG, "Failed to allocate PD");
            return VM_MEM_ERROR_INSUFFICIENT_MEM;
        }
        
        // Clear the new table
        memset(pd, 0, PAGE_SIZE);
        
        // Setup the PDPT entry
        pdpt[pdpt_index].read = 1;
        pdpt[pdpt_index].write = 1;
        pdpt[pdpt_index].execute = 1;
        pdpt[pdpt_index].pd_addr = (uint64_t)pd >> 12;
    }
    
    // Get PD
    ept_pde_t* pd = (ept_pde_t*)(pdpt[pdpt_index].pd_addr << 12);
    
    // Check if we want to map a 2MB large page
    if (0) { // Set to 0 for now, as we'll focus on 4KB pages
        // Map a 2MB page directly here
        pd[pd_index].read = !!(permissions & EPT_PERM_READ);
        pd[pd_index].write = !!(permissions & EPT_PERM_WRITE);
        pd[pd_index].execute = !!(permissions & EPT_PERM_EXECUTE);
        pd[pd_index].memory_type = EPT_MEMORY_TYPE_WB; // Use write-back caching
        pd[pd_index].large_page = 1;
        pd[pd_index].page_frame_number = (host_physical & 0xFFFFFFFE00000) >> 21;
    } else {
        // We need a page table for 4KB pages
        if (!(pd[pd_index].read)) {
            // Need to allocate a new PT
            ept_pte_t* pt = (ept_pte_t*)allocate_pages(1);
            if (!pt) {
                log_error(VM_MEM_LOG_TAG, "Failed to allocate PT");
                return VM_MEM_ERROR_INSUFFICIENT_MEM;
            }
            
            // Clear the new table
            memset(pt, 0, PAGE_SIZE);
            
            // Setup the PD entry
            pd[pd_index].read = 1;
            pd[pd_index].write = 1;
            pd[pd_index].execute = 1;
            pd[pd_index].large_page = 0;
            pd[pd_index].page_frame_number = (uint64_t)pt >> 12;
        }
        
        // Get PT
        ept_pte_t* pt = (ept_pte_t*)(pd[pd_index].page_frame_number << 12);
        
        // Set up the PTE
        pt[pt_index].read = !!(permissions & EPT_PERM_READ);
        pt[pt_index].write = !!(permissions & EPT_PERM_WRITE);
        pt[pt_index].execute = !!(permissions & EPT_PERM_EXECUTE);
        pt[pt_index].memory_type = EPT_MEMORY_TYPE_WB;
        pt[pt_index].page_frame_number = host_physical >> 12;
    }
    
    return VM_MEM_SUCCESS;
}