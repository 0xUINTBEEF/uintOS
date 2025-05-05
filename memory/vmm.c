#include "vmm.h"
#include "heap.h"
#include "paging.h"
#include "../kernel/logging/log.h"
#include <string.h>

/**
 * Virtual Memory Manager (VMM) implementation for uintOS
 */

// Page size in bytes
#define PAGE_SIZE 4096

// Number of bits in a page frame number
#define PFN_BITS 20  // For 32-bit system with 4KB pages (gives 4GB address space)

// Page table entry flags
#define PTE_PRESENT     0x001   // Page is present in memory
#define PTE_WRITE       0x002   // Page is writable
#define PTE_USER        0x004   // Page is accessible to user mode
#define PTE_WRITETHROUGH 0x008  // Write-through caching enabled
#define PTE_NOCACHE     0x010   // Disable caching for this page
#define PTE_ACCESSED    0x020   // Page has been accessed
#define PTE_DIRTY       0x040   // Page has been written to
#define PTE_PAT         0x080   // Page attribute table
#define PTE_GLOBAL      0x100   // Global page (not flushed from TLB)
#define PTE_FRAMEBITS   0xFFFFF000  // Bits that represent the physical frame (assuming 4KB pages)

// Page fault error code flags
#define PF_PRESENT      0x01    // Fault was due to page not present
#define PF_WRITE        0x02    // Fault was caused by a write
#define PF_USER         0x04    // Fault occurred in user mode
#define PF_RSVD         0x08    // Fault was caused by reserved bit violation
#define PF_INST         0x10    // Fault was caused by an instruction fetch

// System-wide memory statistics
typedef struct {
    size_t total_physical_memory;     // Total physical memory in bytes
    size_t free_physical_memory;      // Free physical memory in bytes
    size_t total_virtual_memory;      // Total virtual memory in bytes
    size_t free_virtual_memory;       // Free virtual memory in bytes
    size_t page_faults;               // Number of page faults handled
    size_t page_ins;                  // Number of pages read from disk
    size_t page_outs;                 // Number of pages written to disk
} vm_stats_t;

// Memory region descriptor (for tracking memory regions)
typedef struct vm_region {
    uint32_t start;                   // Start address of the region
    uint32_t end;                     // End address of the region
    uint32_t flags;                   // Protection flags (read/write/execute)
    uint32_t type;                    // Type of region (code, data, stack, etc.)
    const char* name;                 // Name of the region (for debugging)
    struct vm_region* next;           // Next region in the list
} vm_region_t;

// Address space structure (one per process)
typedef struct {
    uint32_t* page_directory;         // Physical address of page directory
    vm_region_t* regions;             // Memory regions in this address space
    int id;                           // ID of owner process
} address_space_t;

// Page frame structure (for physical memory management)
typedef struct {
    uint32_t flags;                   // Flags for the page frame
    uint16_t ref_count;               // Reference count
    uint16_t reserved;                // Padding/reserved
} page_frame_t;

// Page frame flags
#define PF_FREE         0x00    // Page frame is free
#define PF_ALLOCATED    0x01    // Page frame is allocated
#define PF_LOCKED       0x02    // Page frame is locked (can't be paged out)
#define PF_KERNEL       0x04    // Page frame is used by kernel
#define PF_SHARED       0x08    // Page frame is shared between processes
#define PF_RESERVED     0x10    // Page frame is reserved by hardware

// Global VMM state
static struct {
    int initialized;                  // Whether VMM is initialized
    address_space_t* kernel_space;    // Kernel address space
    address_space_t* current_space;   // Current active address space
    
    // Physical memory management
    page_frame_t* page_frames;        // Array of page frame structures
    uint32_t num_frames;              // Number of physical page frames
    uint32_t free_frames;             // Number of free page frames
    uint32_t next_free_frame;         // Next free frame to check (optimization)
    
    // Memory statistics
    vm_stats_t stats;                 // Memory statistics
    
    // Page fault handler
    void (*page_fault_handler)(uint32_t address, uint32_t error_code);
} vmm;

// Forward declarations for internal functions
static uint32_t vmm_alloc_frame(void);
static void vmm_free_frame(uint32_t frame);
static int vmm_map_page(address_space_t* space, uint32_t virt, uint32_t phys, uint32_t flags);
static int vmm_unmap_page(address_space_t* space, uint32_t virt);
static void vmm_flush_tlb_page(uint32_t addr);
static void vmm_flush_tlb_full(void);
static address_space_t* vmm_create_address_space(void);
static void vmm_destroy_address_space(address_space_t* space);
static void vmm_switch_address_space(address_space_t* space);
static vm_region_t* vmm_add_region(address_space_t* space, uint32_t start, uint32_t end,
                                  uint32_t flags, uint32_t type, const char* name);
static void vmm_remove_region(address_space_t* space, uint32_t start);
static vm_region_t* vmm_find_region(address_space_t* space, uint32_t addr);
static void vmm_default_page_fault_handler(uint32_t address, uint32_t error_code);

/**
 * Initialize the Virtual Memory Manager
 */
int vmm_init(uint32_t mem_size_kb) {
    log_info("Initializing Virtual Memory Manager");
    
    // Initialize VMM state
    memset(&vmm, 0, sizeof(vmm));
    
    // Calculate number of page frames
    vmm.num_frames = mem_size_kb / (PAGE_SIZE / 1024);
    vmm.free_frames = vmm.num_frames;
    
    log_debug("Total physical memory: %u KB (%u frames)", mem_size_kb, vmm.num_frames);
    
    // Allocate page frame array
    vmm.page_frames = (page_frame_t*)heap_alloc(sizeof(page_frame_t) * vmm.num_frames);
    if (!vmm.page_frames) {
        log_error("Failed to allocate page frame array");
        return -1;
    }
    
    // Initialize page frame array
    memset(vmm.page_frames, 0, sizeof(page_frame_t) * vmm.num_frames);
    
    // Reserve frames for kernel (0-1MB)
    for (uint32_t i = 0; i < 256; i++) {  // 256 frames = 1MB
        vmm.page_frames[i].flags = PF_ALLOCATED | PF_LOCKED | PF_KERNEL;
        vmm.page_frames[i].ref_count = 1;
        vmm.free_frames--;
    }
    
    vmm.next_free_frame = 256;  // Start scanning for free frames after kernel area
    
    // Create kernel address space
    vmm.kernel_space = vmm_create_address_space();
    if (!vmm.kernel_space) {
        log_error("Failed to create kernel address space");
        heap_free(vmm.page_frames);
        return -1;
    }
    
    // Identity map the kernel area (0-4MB)
    for (uint32_t i = 0; i < 1024; i++) {  // 1024 pages = 4MB
        uint32_t addr = i * PAGE_SIZE;
        vmm_map_page(vmm.kernel_space, addr, addr, PTE_PRESENT | PTE_WRITE);
    }
    
    // Add kernel memory region
    vmm_add_region(vmm.kernel_space, 0, 4*1024*1024, 
                  VM_PERM_READ | VM_PERM_WRITE | VM_PERM_EXEC, 
                  VM_TYPE_KERNEL, "kernel");
    
    // Set up the default page fault handler
    vmm.page_fault_handler = vmm_default_page_fault_handler;
    
    // Switch to kernel address space
    vmm.current_space = vmm.kernel_space;
    vmm_switch_address_space(vmm.kernel_space);
    
    // Initialize memory statistics
    vmm.stats.total_physical_memory = mem_size_kb * 1024;
    vmm.stats.free_physical_memory = vmm.free_frames * PAGE_SIZE;
    vmm.stats.total_virtual_memory = 4UL * 1024 * 1024 * 1024;  // 4GB for 32-bit
    vmm.stats.free_virtual_memory = vmm.stats.total_virtual_memory - (4*1024*1024);  // Minus kernel area
    
    vmm.initialized = 1;
    
    log_info("VMM initialized successfully");
    log_debug("Free physical memory: %u KB", vmm.stats.free_physical_memory / 1024);
    
    return 0;
}

/**
 * Allocate a physical page frame
 * 
 * @return Physical address of the frame, or 0 on failure
 */
static uint32_t vmm_alloc_frame(void) {
    if (vmm.free_frames == 0) {
        log_error("No free page frames available");
        return 0;
    }
    
    // Find a free frame
    uint32_t i = vmm.next_free_frame;
    for (uint32_t count = 0; count < vmm.num_frames; count++) {
        if ((vmm.page_frames[i].flags & PF_ALLOCATED) == 0) {
            // Found a free frame
            vmm.page_frames[i].flags = PF_ALLOCATED;
            vmm.page_frames[i].ref_count = 1;
            vmm.free_frames--;
            
            // Update next free frame hint
            vmm.next_free_frame = (i + 1) % vmm.num_frames;
            
            // Update statistics
            vmm.stats.free_physical_memory -= PAGE_SIZE;
            
            return i * PAGE_SIZE;
        }
        
        i = (i + 1) % vmm.num_frames;
    }
    
    log_error("Failed to allocate page frame");
    return 0;
}

/**
 * Free a physical page frame
 * 
 * @param frame Physical address of the frame
 */
static void vmm_free_frame(uint32_t frame) {
    uint32_t frame_idx = frame / PAGE_SIZE;
    
    if (frame_idx >= vmm.num_frames) {
        log_error("Invalid page frame address: 0x%08x", frame);
        return;
    }
    
    // Check if the frame is allocated
    if ((vmm.page_frames[frame_idx].flags & PF_ALLOCATED) == 0) {
        log_warning("Attempting to free already free frame: 0x%08x", frame);
        return;
    }
    
    // Decrement reference count
    if (vmm.page_frames[frame_idx].ref_count > 0) {
        vmm.page_frames[frame_idx].ref_count--;
    }
    
    // If reference count reaches 0, free the frame
    if (vmm.page_frames[frame_idx].ref_count == 0) {
        vmm.page_frames[frame_idx].flags = 0;
        vmm.free_frames++;
        
        // Update statistics
        vmm.stats.free_physical_memory += PAGE_SIZE;
        
        // Update next free frame hint if this is lower
        if (frame_idx < vmm.next_free_frame) {
            vmm.next_free_frame = frame_idx;
        }
    }
}

/**
 * Map a virtual page to a physical frame
 * 
 * @param space Address space
 * @param virt Virtual address
 * @param phys Physical address
 * @param flags Page table entry flags
 * @return 0 on success, -1 on failure
 */
static int vmm_map_page(address_space_t* space, uint32_t virt, uint32_t phys, uint32_t flags) {
    if (!space) {
        log_error("Invalid address space");
        return -1;
    }
    
    // Get indices in page directory and page table
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    
    // Get page directory entry
    uint32_t* pd = space->page_directory;
    uint32_t pde = pd[pd_index];
    uint32_t* pt = NULL;
    
    // If page table doesn't exist, create it
    if ((pde & PTE_PRESENT) == 0) {
        // Allocate a new page table
        uint32_t pt_phys = vmm_alloc_frame();
        if (pt_phys == 0) {
            log_error("Failed to allocate page table");
            return -1;
        }
        
        // Map the page table into our virtual address space
        pt = (uint32_t*)(pt_phys + 0xC0000000);  // Map at kernel offset
        
        // Clear the page table
        memset(pt, 0, PAGE_SIZE);
        
        // Add the entry to the page directory
        pd[pd_index] = pt_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
    } else {
        // Page table exists, get its address
        uint32_t pt_phys = pde & PTE_FRAMEBITS;
        pt = (uint32_t*)(pt_phys + 0xC0000000);  // Map at kernel offset
    }
    
    // Map the page
    pt[pt_index] = (phys & PTE_FRAMEBITS) | flags;
    
    // Flush the TLB for this page
    vmm_flush_tlb_page(virt);
    
    return 0;
}

/**
 * Unmap a virtual page
 * 
 * @param space Address space
 * @param virt Virtual address
 * @return 0 on success, -1 on failure
 */
static int vmm_unmap_page(address_space_t* space, uint32_t virt) {
    if (!space) {
        log_error("Invalid address space");
        return -1;
    }
    
    // Get indices in page directory and page table
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    
    // Get page directory entry
    uint32_t* pd = space->page_directory;
    uint32_t pde = pd[pd_index];
    
    // If page table doesn't exist, nothing to do
    if ((pde & PTE_PRESENT) == 0) {
        return 0;
    }
    
    // Get page table
    uint32_t pt_phys = pde & PTE_FRAMEBITS;
    uint32_t* pt = (uint32_t*)(pt_phys + 0xC0000000);  // Map at kernel offset
    
    // Get page table entry
    uint32_t pte = pt[pt_index];
    
    // If page is not present, nothing to do
    if ((pte & PTE_PRESENT) == 0) {
        return 0;
    }
    
    // Get physical frame address
    uint32_t frame = pte & PTE_FRAMEBITS;
    
    // Free the physical frame
    vmm_free_frame(frame);
    
    // Clear the page table entry
    pt[pt_index] = 0;
    
    // Flush the TLB for this page
    vmm_flush_tlb_page(virt);
    
    return 0;
}

/**
 * Flush a single page from the TLB
 * 
 * @param addr Virtual address of the page
 */
static void vmm_flush_tlb_page(uint32_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/**
 * Flush the entire TLB
 */
static void vmm_flush_tlb_full(void) {
    // Read CR3 and write it back to flush the TLB
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" : : "r"(cr3));
}

/**
 * Create a new address space
 * 
 * @return Pointer to new address space, or NULL on failure
 */
static address_space_t* vmm_create_address_space(void) {
    // Allocate address space structure
    address_space_t* space = (address_space_t*)heap_alloc(sizeof(address_space_t));
    if (!space) {
        log_error("Failed to allocate address space structure");
        return NULL;
    }
    
    // Clear the structure
    memset(space, 0, sizeof(address_space_t));
    
    // Allocate page directory
    uint32_t pd_phys = vmm_alloc_frame();
    if (pd_phys == 0) {
        log_error("Failed to allocate page directory");
        heap_free(space);
        return NULL;
    }
    
    // Map page directory at kernel offset
    space->page_directory = (uint32_t*)(pd_phys + 0xC0000000);
    
    // Clear the page directory
    memset(space->page_directory, 0, PAGE_SIZE);
    
    // Map kernel pages into this address space
    // In a real implementation, we would copy or link the kernel entries from the
    // kernel page directory to ensure all process address spaces include the kernel.
    
    return space;
}

/**
 * Destroy an address space
 * 
 * @param space Address space to destroy
 */
static void vmm_destroy_address_space(address_space_t* space) {
    if (!space) {
        return;
    }
    
    // Free all memory regions
    vm_region_t* region = space->regions;
    while (region) {
        vm_region_t* next = region->next;
        heap_free(region);
        region = next;
    }
    
    // Free all page tables
    for (uint32_t pd_index = 0; pd_index < 1024; pd_index++) {
        uint32_t pde = space->page_directory[pd_index];
        if (pde & PTE_PRESENT) {
            uint32_t pt_phys = pde & PTE_FRAMEBITS;
            vmm_free_frame(pt_phys);
        }
    }
    
    // Free page directory
    uint32_t pd_phys = ((uint32_t)space->page_directory) - 0xC0000000;
    vmm_free_frame(pd_phys);
    
    // Free address space structure
    heap_free(space);
}

/**
 * Switch to a different address space
 * 
 * @param space Address space to switch to
 */
static void vmm_switch_address_space(address_space_t* space) {
    if (!space) {
        return;
    }
    
    // Get physical address of page directory
    uint32_t pd_phys = ((uint32_t)space->page_directory) - 0xC0000000;
    
    // Set CR3 to the new page directory
    asm volatile("mov %0, %%cr3" : : "r"(pd_phys));
    
    // Update current address space
    vmm.current_space = space;
}

/**
 * Add a memory region to an address space
 * 
 * @param space Address space
 * @param start Start address of the region
 * @param end End address of the region
 * @param flags Protection flags
 * @param type Type of memory region
 * @param name Name of the region (for debugging)
 * @return Pointer to the new region, or NULL on failure
 */
static vm_region_t* vmm_add_region(address_space_t* space, uint32_t start, uint32_t end,
                                  uint32_t flags, uint32_t type, const char* name) {
    if (!space) {
        return NULL;
    }
    
    // Align start and end addresses to page boundaries
    start = (start / PAGE_SIZE) * PAGE_SIZE;
    end = ((end + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // Check if the region overlaps with an existing region
    vm_region_t* region = space->regions;
    while (region) {
        if ((start < region->end) && (end > region->start)) {
            log_error("Memory region overlap detected");
            return NULL;
        }
        region = region->next;
    }
    
    // Allocate new region descriptor
    vm_region_t* new_region = (vm_region_t*)heap_alloc(sizeof(vm_region_t));
    if (!new_region) {
        log_error("Failed to allocate memory region descriptor");
        return NULL;
    }
    
    // Initialize the region
    new_region->start = start;
    new_region->end = end;
    new_region->flags = flags;
    new_region->type = type;
    new_region->name = name;
    
    // Add to the region list
    new_region->next = space->regions;
    space->regions = new_region;
    
    return new_region;
}

/**
 * Remove a memory region from an address space
 * 
 * @param space Address space
 * @param start Start address of the region to remove
 */
static void vmm_remove_region(address_space_t* space, uint32_t start) {
    if (!space) {
        return;
    }
    
    // Find the region
    vm_region_t** pprev = &space->regions;
    vm_region_t* region = space->regions;
    
    while (region) {
        if (region->start == start) {
            // Found the region, remove it
            *pprev = region->next;
            heap_free(region);
            return;
        }
        
        pprev = &region->next;
        region = region->next;
    }
}

/**
 * Find the memory region containing an address
 * 
 * @param space Address space
 * @param addr Address to find
 * @return Pointer to the region, or NULL if not found
 */
static vm_region_t* vmm_find_region(address_space_t* space, uint32_t addr) {
    if (!space) {
        return NULL;
    }
    
    // Search for the region
    vm_region_t* region = space->regions;
    
    while (region) {
        if ((addr >= region->start) && (addr < region->end)) {
            return region;
        }
        
        region = region->next;
    }
    
    return NULL;
}

/**
 * Default page fault handler
 * 
 * @param address Virtual address that caused the fault
 * @param error_code Error code from the CPU
 */
static void vmm_default_page_fault_handler(uint32_t address, uint32_t error_code) {
    // Update statistics
    vmm.stats.page_faults++;
    
    // Log information about the page fault
    log_error("Page fault at 0x%08x, error code 0x%08x", address, error_code);
    
    if (!(error_code & PF_PRESENT)) {
        log_error("  Page not present");
    }
    if (error_code & PF_WRITE) {
        log_error("  Caused by write access");
    } else {
        log_error("  Caused by read access");
    }
    if (error_code & PF_USER) {
        log_error("  Occurred in user mode");
    } else {
        log_error("  Occurred in kernel mode");
    }
    if (error_code & PF_RSVD) {
        log_error("  Reserved bits set in page entry");
    }
    if (error_code & PF_INST) {
        log_error("  Caused by instruction fetch");
    }
    
    // Try to handle the fault
    vm_region_t* region = vmm_find_region(vmm.current_space, address);
    
    if (region) {
        // Check if the access is allowed
        uint32_t required_flags = 0;
        
        if (error_code & PF_WRITE) {
            required_flags |= VM_PERM_WRITE;
        } else if (error_code & PF_INST) {
            required_flags |= VM_PERM_EXEC;
        } else {
            required_flags |= VM_PERM_READ;
        }
        
        if ((region->flags & required_flags) == required_flags) {
            // Access is allowed, try to map a new page
            uint32_t page_addr = (address / PAGE_SIZE) * PAGE_SIZE;
            uint32_t frame = vmm_alloc_frame();
            
            if (frame) {
                // Map the page
                uint32_t pte_flags = PTE_PRESENT | PTE_USER;
                if (region->flags & VM_PERM_WRITE) {
                    pte_flags |= PTE_WRITE;
                }
                
                vmm_map_page(vmm.current_space, page_addr, frame, pte_flags);
                
                // Clear the newly allocated page
                memset((void*)page_addr, 0, PAGE_SIZE);
                
                log_debug("Mapped new page at 0x%08x -> 0x%08x", page_addr, frame);
                
                // Handled successfully
                return;
            }
        }
    }
    
    log_error("Unhandled page fault at 0x%08x", address);
    
    // Proper process termination for unhandled page faults
    if (process_is_running()) {
        // Get current process information
        process_t* current = process_current();
        log_error("Terminating process %d (%s) due to unhandled page fault", 
                 current->pid, current->name);
        
        // Create a core dump file for debugging (if filesystem is available)
        if (fs_is_available()) {
            char dump_filename[64];
            snprintf(dump_filename, sizeof(dump_filename), "/var/crash/core.%d", current->pid);
            process_create_core_dump(current, dump_filename);
            log_info("Core dump created at %s", dump_filename);
        }
        
        // Terminate the process
        process_exit(PROCESS_EXIT_SEGFAULT);
        
        // Return to scheduler to run next process
        scheduler_yield();
    } else {
        // If no process context, this is a kernel fault - panic
        panic("Unhandled kernel page fault at 0x%08x", address);
    }
}

/**
 * Handle a page fault
 * 
 * @param address Virtual address that caused the fault
 * @param error_code Error code from the CPU
 */
void vmm_handle_page_fault(uint32_t address, uint32_t error_code) {
    if (vmm.page_fault_handler) {
        vmm.page_fault_handler(address, error_code);
    } else {
        vmm_default_page_fault_handler(address, error_code);
    }
}

/**
 * Register a custom page fault handler
 * 
 * @param handler Pointer to handler function
 */
void vmm_register_page_fault_handler(void (*handler)(uint32_t, uint32_t)) {
    vmm.page_fault_handler = handler;
}

/**
 * Allocate a region of virtual memory
 * 
 * @param size Size in bytes
 * @param flags Protection flags
 * @param type Type of memory
 * @param name Name of the region (for debugging)
 * @return Virtual address of the allocated region, or 0 on failure
 */
void* vmm_alloc(size_t size, uint32_t flags, uint32_t type, const char* name) {
    if (!vmm.initialized || size == 0) {
        return NULL;
    }
    
    // Round up size to page boundary
    size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // TODO: Find a free region of appropriate size
    // For now, use a simple approach:
    
    // Try to allocate at a fixed address (for demo purposes)
    uint32_t start = 0x10000000;  // 256MB
    uint32_t end = start + size;
    
    // Create the memory region
    vm_region_t* region = vmm_add_region(vmm.current_space, start, end, flags, type, name);
    if (!region) {
        log_error("Failed to create memory region");
        return NULL;
    }
    
    // Return the start address of the region
    return (void*)start;
}

/**
 * Free a previously allocated memory region
 * 
 * @param addr Start address of the region
 * @param size Size in bytes
 */
void vmm_free(void* addr, size_t size) {
    if (!vmm.initialized || !addr || size == 0) {
        return;
    }
    
    uint32_t start = (uint32_t)addr;
    uint32_t end = start + size;
    
    // Align to page boundaries
    start = (start / PAGE_SIZE) * PAGE_SIZE;
    end = ((end + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // Find the region
    vm_region_t* region = vmm_find_region(vmm.current_space, start);
    if (!region || region->start != start) {
        log_warning("Trying to free memory not at the start of a region: 0x%08x", start);
        return;
    }
    
    // Unmap all pages in the region
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        vmm_unmap_page(vmm.current_space, addr);
    }
    
    // Remove the region
    vmm_remove_region(vmm.current_space, start);
}

/**
 * Map physical memory into virtual address space
 * 
 * @param phys Physical address
 * @param size Size in bytes
 * @param flags Protection flags
 * @param name Name of the region (for debugging)
 * @return Virtual address of the mapped region, or 0 on failure
 */
void* vmm_map_physical(uint32_t phys, size_t size, uint32_t flags, const char* name) {
    if (!vmm.initialized || size == 0) {
        return NULL;
    }
    
    // Align start and size to page boundaries
    uint32_t page_phys = (phys / PAGE_SIZE) * PAGE_SIZE;
    uint32_t offset = phys - page_phys;
    size += offset;
    size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // Try to allocate at a fixed address (for demo purposes)
    uint32_t virt = 0x20000000;  // 512MB
    uint32_t end = virt + size;
    
    // Create the memory region
    vm_region_t* region = vmm_add_region(vmm.current_space, virt, end, flags, VM_TYPE_MMIO, name);
    if (!region) {
        log_error("Failed to create memory region for physical mapping");
        return NULL;
    }
    
    // Map each page
    for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint32_t pte_flags = PTE_PRESENT | PTE_NOCACHE;  // MMIO regions should not be cached
        if (flags & VM_PERM_WRITE) {
            pte_flags |= PTE_WRITE;
        }
        if (flags & VM_PERM_USER) {
            pte_flags |= PTE_USER;
        }
        
        vmm_map_page(vmm.current_space, virt + offset, page_phys + offset, pte_flags);
    }
    
    // Return the virtual address, adjusted for the offset
    return (void*)(virt + offset);
}

/**
 * Get statistics about memory usage
 * 
 * @param stats Pointer to statistics structure to fill
 */
void vmm_get_stats(vm_stats_t* stats) {
    if (!vmm.initialized || !stats) {
        return;
    }
    
    memcpy(stats, &vmm.stats, sizeof(vm_stats_t));
}

/**
 * Dump memory regions for debugging
 */
void vmm_dump_regions(void) {
    if (!vmm.initialized) {
        return;
    }
    
    address_space_t* space = vmm.current_space;
    if (!space) {
        log_info("No current address space");
        return;
    }
    
    log_info("Memory regions in address space %d:", space->id);
    
    vm_region_t* region = space->regions;
    while (region) {
        log_info("  0x%08x - 0x%08x: %s (flags: 0x%x, type: %d)",
                region->start, region->end,
                region->name ? region->name : "unnamed",
                region->flags, region->type);
        
        region = region->next;
    }
}