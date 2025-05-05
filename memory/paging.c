#include "paging.h"
#include "../kernel/logging/log.h"
#include <stdint.h>
#include <string.h>

// Enhanced page directory and table structures
typedef struct {
    uint32_t entries[PAGE_DIRECTORY_ENTRIES];
} page_directory_t;

typedef struct {
    uint32_t entries[PAGE_TABLE_ENTRIES];
} page_table_t;

// Memory management metadata
typedef struct {
    uint8_t state;          // PAGE_FREE, PAGE_USED, PAGE_RESERVED
    uint16_t references;    // Reference count for shared pages
    uint32_t flags;         // Page flags/attributes
    uint32_t owner_pid;     // Process that owns this page
} page_info_t;

// Shadow page directory for copy-on-write operations
static uint32_t shadow_page_directory[PAGE_DIRECTORY_ENTRIES];

// Memory management variables
static uint32_t* current_page_directory = NULL;
static page_info_t* page_info_array = NULL;
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;

// Memory protection
#define MAX_PROTECTED_REGIONS 16

typedef struct {
    void* start;
    void* end;
    uint32_t flags;
    const char* name;
} protected_region_t;

static protected_region_t protected_regions[MAX_PROTECTED_REGIONS];
static int num_protected_regions = 0;

// Memory statistics
static struct {
    uint32_t pages_allocated;
    uint32_t pages_freed;
    uint32_t address_space_switches;
    uint32_t page_faults;
    uint32_t tlb_flushes;
} memory_stats = {0};

// Forward declarations of helper functions
static void flush_tlb_entry(void* addr);
static void flush_entire_tlb(void);
static uint32_t get_physical_address(void* virtual);
static int handle_page_fault(void* fault_addr, int is_write, int is_user);

/**
 * Initialize the paging subsystem with improved memory management
 */
void paging_init() {
    // Get memory size from the bootloader information
    extern uint32_t _bootinfo_memsize;
    total_pages = _bootinfo_memsize / PAGE_SIZE;
    free_pages = total_pages;
    
    log_info("Paging: Initializing with %u KB total memory (%u pages)", 
             total_pages * (PAGE_SIZE / 1024), total_pages);
    
    // Allocate space for page tracking information
    page_info_array = (page_info_t*)0x100000; // 1MB mark
    memset(page_info_array, 0, total_pages * sizeof(page_info_t));
    
    // Reserve pages for kernel and page tables
    uint32_t kernel_pages = 256; // 1MB kernel size / 4KB page size
    for (uint32_t i = 0; i < kernel_pages; i++) {
        page_info_array[i].state = PAGE_RESERVED;
        page_info_array[i].flags = PAGE_FLAG_PRESENT | PAGE_FLAG_GLOBAL;
        free_pages--;
    }
    
    // Create initial page directory at a fixed physical address
    current_page_directory = (uint32_t*)0x1000; // 4KB mark
    memset(current_page_directory, 0, PAGE_DIRECTORY_ENTRIES * sizeof(uint32_t));
    
    // Allocate and map first few megabytes for kernel space (identity mapping)
    for (uint32_t i = 0; i < 4; i++) { // First 16MB
        page_table_t* table = (page_table_t*)(0x2000 + i * PAGE_SIZE); // Page tables after page directory
        memset(table, 0, sizeof(page_table_t));
        
        // Identity map this portion of memory
        for (uint32_t j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            uint32_t phys_addr = (i * PAGE_TABLE_ENTRIES + j) * PAGE_SIZE;
            table->entries[j] = phys_addr | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE | PAGE_FLAG_GLOBAL;
        }
        
        // Add table to directory
        current_page_directory[i] = (uint32_t)table | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
    }
    
    // Allocate page tables for higher-half kernel mapping (at 0xC0000000)
    uint32_t higher_half_idx = 0xC0000000 / (PAGE_SIZE * PAGE_TABLE_ENTRIES);
    for (uint32_t i = 0; i < 4; i++) { // Map 16MB for kernel
        page_table_t* table = (page_table_t*)(0x6000 + i * PAGE_SIZE); // Higher half page tables
        memset(table, 0, sizeof(page_table_t));
        
        // Map kernel memory to higher half
        for (uint32_t j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            uint32_t phys_addr = (i * PAGE_TABLE_ENTRIES + j) * PAGE_SIZE;
            table->entries[j] = phys_addr | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE | PAGE_FLAG_GLOBAL;
        }
        
        // Add table to directory
        current_page_directory[higher_half_idx + i] = (uint32_t)table | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
    }
    
    // Register protected memory regions
    paging_protect_region((void*)0x0, (void*)0x1000, PAGE_FLAG_PRESENT, "Null guard page");
    paging_protect_region((void*)0x1000, (void*)0xA000, PAGE_FLAG_PRESENT | PAGE_FLAG_GLOBAL, "Kernel page tables");
    paging_protect_region((void*)0x100000, (void*)0x200000, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE | PAGE_FLAG_GLOBAL, "Kernel code & data");
    
    // Setup page fault handler
    extern void register_interrupt_handler(int irq, void (*handler)(void));
    register_interrupt_handler(14, (void*)page_fault_handler);
    
    // Enable paging by updating CR3 and CR0
    asm volatile("movl %0, %%cr3" : : "r"(current_page_directory));
    
    uint32_t cr0;
    asm volatile("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging bit
    asm volatile("movl %0, %%cr0" : : "r"(cr0));
    
    log_info("Paging: Successfully enabled with %u free pages", free_pages);
}

/**
 * Allocate a single page with improved tracking
 */
void* allocate_page() {
    // Find a free page
    for (uint32_t i = 0; i < total_pages; i++) {
        if (page_info_array[i].state == PAGE_FREE) {
            // Mark page as used
            page_info_array[i].state = PAGE_USED;
            page_info_array[i].references = 1;
            page_info_array[i].flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
            
            // Update process owner if we're in a process context
            extern uint32_t current_process_id;
            if (current_process_id != 0) {
                page_info_array[i].owner_pid = current_process_id;
            }
            
            free_pages--;
            memory_stats.pages_allocated++;
            
            // Zero out the page before returning it to prevent data leaks
            void* page = (void*)(i * PAGE_SIZE);
            memset(page, 0, PAGE_SIZE);
            
            return page;
        }
    }
    
    log_error("Paging: Failed to allocate page, out of memory!");
    return NULL; // Out of memory
}

/**
 * Free a previously allocated page with reference counting
 */
void free_page(void* page) {
    uint32_t page_index = (uint32_t)page / PAGE_SIZE;
    
    // Validate page address
    if (page_index >= total_pages) {
        log_error("Paging: Attempt to free invalid page address %p", page);
        return;
    }
    
    // Check if page is already free
    if (page_info_array[page_index].state == PAGE_FREE) {
        log_warning("Paging: Double free detected for page %p", page);
        return;
    }
    
    // Handle reference counting for shared pages
    if (page_info_array[page_index].references > 1) {
        page_info_array[page_index].references--;
        return;
    }
    
    // Actually free the page
    page_info_array[page_index].state = PAGE_FREE;
    page_info_array[page_index].references = 0;
    page_info_array[page_index].flags = 0;
    page_info_array[page_index].owner_pid = 0;
    
    free_pages++;
    memory_stats.pages_freed++;
    
    // Zero out the page to prevent data leaks
    memset(page, 0, PAGE_SIZE);
}

/**
 * Allocate multiple contiguous pages with alignment support
 */
void* allocate_pages(uint32_t num) {
    if (num == 0) return NULL;
    if (num == 1) return allocate_page();
    
    if (num > free_pages) {
        log_error("Paging: Failed to allocate %u pages, only %u available", num, free_pages);
        return NULL;
    }
    
    // Find contiguous free pages
    uint32_t start_page = 0;
    uint32_t found_pages = 0;
    
    for (uint32_t i = 0; i < total_pages; i++) {
        if (page_info_array[i].state == PAGE_FREE) {
            if (found_pages == 0) {
                start_page = i;
            }
            found_pages++;
            
            if (found_pages == num) {
                // Mark all pages as used
                for (uint32_t j = 0; j < num; j++) {
                    uint32_t page_idx = start_page + j;
                    page_info_array[page_idx].state = PAGE_USED;
                    page_info_array[page_idx].references = 1;
                    page_info_array[page_idx].flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
                    
                    // Update process owner if in process context
                    extern uint32_t current_process_id;
                    if (current_process_id != 0) {
                        page_info_array[page_idx].owner_pid = current_process_id;
                    }
                }
                
                free_pages -= num;
                memory_stats.pages_allocated += num;
                
                void* result = (void*)(start_page * PAGE_SIZE);
                memset(result, 0, num * PAGE_SIZE);
                return result;
            }
        } else {
            // Reset counter when we hit a used page
            found_pages = 0;
        }
    }
    
    log_error("Paging: Failed to find %u contiguous pages", num);
    return NULL; // Couldn't find enough contiguous pages
}

/**
 * Free multiple contiguous pages
 */
void free_pages(void* start, uint32_t num) {
    for (uint32_t i = 0; i < num; i++) {
        void* page = (void*)((uint32_t)start + i * PAGE_SIZE);
        free_page(page);
    }
}

/**
 * Map a physical page to a virtual address with specific flags
 */
int paging_map_page(void* physical, void* virtual, uint32_t flags) {
    uint32_t pd_index = (uint32_t)virtual >> 22;
    uint32_t pt_index = ((uint32_t)virtual >> 12) & 0x3FF;
    
    // Check if we need to create a new page table
    if (!(current_page_directory[pd_index] & PAGE_FLAG_PRESENT)) {
        // Allocate page table
        void* pt_physical = allocate_page();
        if (!pt_physical) {
            log_error("Paging: Failed to allocate page table for mapping %p->%p", physical, virtual);
            return -1;
        }
        
        // Zero new page table
        memset(pt_physical, 0, PAGE_SIZE);
        
        // Add to directory
        current_page_directory[pd_index] = (uint32_t)pt_physical | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE | 
                                          ((flags & PAGE_FLAG_USER) ? PAGE_FLAG_USER : 0);
    }
    
    // Get page table
    page_table_t* table = (page_table_t*)(current_page_directory[pd_index] & ~0xFFF);
    
    // Check if there's already a page mapped
    if (table->entries[pt_index] & PAGE_FLAG_PRESENT) {
        // If remapping with different physical address, log a warning
        if ((table->entries[pt_index] & ~0xFFF) != ((uint32_t)physical & ~0xFFF)) {
            log_warning("Paging: Remapping virtual address %p from %p to %p", 
                      virtual, 
                      (void*)(table->entries[pt_index] & ~0xFFF), 
                      physical);
        }
    }
    
    // Update page table entry
    table->entries[pt_index] = ((uint32_t)physical & ~0xFFF) | (flags & 0xFFF);
    
    // Invalidate TLB entry for this virtual address
    flush_tlb_entry(virtual);
    
    return 0;
}

/**
 * Unmap a virtual address
 */
void unmap_page(void* virtual) {
    uint32_t pd_index = (uint32_t)virtual >> 22;
    uint32_t pt_index = ((uint32_t)virtual >> 12) & 0x3FF;
    
    // Make sure page directory entry exists
    if (!(current_page_directory[pd_index] & PAGE_FLAG_PRESENT)) {
        return; // Nothing to unmap
    }
    
    // Get page table
    page_table_t* table = (page_table_t*)(current_page_directory[pd_index] & ~0xFFF);
    
    // Clear page table entry
    table->entries[pt_index] = 0;
    
    // Invalidate TLB entry
    flush_tlb_entry(virtual);
}

/**
 * Get the flags for a mapped page
 */
uint32_t paging_get_page_flags(void* virtual) {
    uint32_t pd_index = (uint32_t)virtual >> 22;
    uint32_t pt_index = ((uint32_t)virtual >> 12) & 0x3FF;
    
    // Make sure page directory entry exists
    if (!(current_page_directory[pd_index] & PAGE_FLAG_PRESENT)) {
        return 0; // Not mapped
    }
    
    // Get page table
    page_table_t* table = (page_table_t*)(current_page_directory[pd_index] & ~0xFFF);
    
    // Return the flags
    return table->entries[pt_index] & 0xFFF;
}

/**
 * Update flags for an existing page mapping
 */
int paging_update_flags(void* virtual, uint32_t flags) {
    uint32_t pd_index = (uint32_t)virtual >> 22;
    uint32_t pt_index = ((uint32_t)virtual >> 12) & 0x3FF;
    
    // Make sure page directory entry exists
    if (!(current_page_directory[pd_index] & PAGE_FLAG_PRESENT)) {
        log_error("Paging: Attempt to update flags for unmapped page %p", virtual);
        return -1;
    }
    
    // Get page table
    page_table_t* table = (page_table_t*)(current_page_directory[pd_index] & ~0xFFF);
    
    // Make sure page table entry exists
    if (!(table->entries[pt_index] & PAGE_FLAG_PRESENT)) {
        log_error("Paging: Attempt to update flags for unmapped page %p", virtual);
        return -1;
    }
    
    // Update flags, keeping the physical address unchanged
    uint32_t phys_addr = table->entries[pt_index] & ~0xFFF;
    table->entries[pt_index] = phys_addr | (flags & 0xFFF);
    
    // Invalidate TLB entry
    flush_tlb_entry(virtual);
    
    return 0;
}

/**
 * Create a new page directory for a process
 */
uint32_t paging_create_address_space(int kernel_accessible) {
    // Allocate page for new directory
    void* new_dir_phys = allocate_page();
    if (!new_dir_phys) {
        log_error("Paging: Failed to allocate page for new address space");
        return 0;
    }
    
    // Clear the directory
    memset(new_dir_phys, 0, PAGE_SIZE);
    
    // If kernel should be accessible, copy kernel mappings
    if (kernel_accessible) {
        // Copy higher-half mappings (kernel space)
        uint32_t* new_dir = (uint32_t*)new_dir_phys;
        for (uint32_t i = 768; i < PAGE_DIRECTORY_ENTRIES; i++) {
            new_dir[i] = current_page_directory[i];
        }
    }
    
    return (uint32_t)new_dir_phys;
}

/**
 * Switch to a different address space
 */
void paging_switch_address_space(uint32_t page_directory) {
    if (page_directory == 0) {
        log_error("Paging: Attempt to switch to null address space");
        return;
    }
    
    // Update CR3 to switch to new page directory
    asm volatile("movl %0, %%cr3" : : "r"(page_directory));
    
    // Update current_page_directory pointer
    current_page_directory = (uint32_t*)page_directory;
    
    memory_stats.address_space_switches++;
}

/**
 * Get the current address space (page directory)
 */
uint32_t paging_get_current_address_space() {
    uint32_t cr3;
    asm volatile("movl %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/**
 * Clone the current address space (for fork operations)
 */
uint32_t paging_clone_address_space(int copy_on_write) {
    // Create new empty page directory
    uint32_t new_dir_phys = paging_create_address_space(1); // Include kernel mappings
    if (new_dir_phys == 0) {
        return 0;
    }
    
    uint32_t* new_dir = (uint32_t*)new_dir_phys;
    
    // Copy or share user space mappings (lower 768 entries)
    for (uint32_t pd_idx = 0; pd_idx < 768; pd_idx++) {
        // Skip unmapped entries
        if (!(current_page_directory[pd_idx] & PAGE_FLAG_PRESENT)) {
            continue;
        }
        
        // Get source page table
        page_table_t* src_table = (page_table_t*)(current_page_directory[pd_idx] & ~0xFFF);
        
        // Create new page table
        void* new_table_phys = allocate_page();
        if (!new_table_phys) {
            // Cleanup on failure - free all allocated tables
            for (uint32_t i = 0; i < pd_idx; i++) {
                if (new_dir[i] & PAGE_FLAG_PRESENT) {
                    free_page((void*)(new_dir[i] & ~0xFFF));
                }
            }
            free_page((void*)new_dir_phys);
            return 0;
        }
        
        // Set up page directory entry
        new_dir[pd_idx] = (uint32_t)new_table_phys | 
                         (current_page_directory[pd_idx] & 0xFFF);
                         
        // Get destination page table
        page_table_t* dst_table = (page_table_t*)new_table_phys;
        
        // Copy page table entries
        for (uint32_t pt_idx = 0; pt_idx < PAGE_TABLE_ENTRIES; pt_idx++) {
            if (!(src_table->entries[pt_idx] & PAGE_FLAG_PRESENT)) {
                dst_table->entries[pt_idx] = 0;
                continue;
            }
            
            if (copy_on_write) {
                // For copy-on-write: Share the same physical memory, but make read-only
                uint32_t phys_addr = src_table->entries[pt_idx] & ~0xFFF;
                uint32_t new_flags = (src_table->entries[pt_idx] & 0xFFF) & ~PAGE_FLAG_WRITABLE;
                
                // Update source to be read-only too
                src_table->entries[pt_idx] &= ~PAGE_FLAG_WRITABLE;
                
                // Mark page in page info array as copy-on-write
                uint32_t page_index = phys_addr / PAGE_SIZE;
                page_info_array[page_index].references++;
                
                // Set destination to same physical page but read-only
                dst_table->entries[pt_idx] = phys_addr | new_flags;
            } else {
                // For full copy: Allocate new physical page and copy data
                void* new_page = allocate_page();
                if (!new_page) {
                    // Handle allocation failure
                    // TODO: Clean up previously allocated resources
                    free_page((void*)new_dir_phys);
                    return 0;
                }
                
                // Copy page contents
                void* src_page = (void*)(src_table->entries[pt_idx] & ~0xFFF);
                memcpy(new_page, src_page, PAGE_SIZE);
                
                // Set up page table entry
                dst_table->entries[pt_idx] = (uint32_t)new_page | 
                                          (src_table->entries[pt_idx] & 0xFFF);
            }
        }
    }
    
    // Store shadow page directory for potential COW operations
    memcpy(shadow_page_directory, new_dir, PAGE_DIRECTORY_ENTRIES * sizeof(uint32_t));
    
    return new_dir_phys;
}

/**
 * Map user memory with appropriate security settings
 */
int paging_map_user_memory(void* virtual, size_t size, int writable, int executable) {
    // Check if address is in user space
    if ((uint32_t)virtual >= 0xC0000000) {
        log_error("Paging: Attempted to map kernel space address %p as user memory", virtual);
        return -1;
    }
    
    // Calculate page aligned address and required pages
    void* aligned_addr = (void*)((uint32_t)virtual & ~0xFFF);
    uint32_t num_pages = ((uint32_t)virtual + size + PAGE_SIZE - 1) / PAGE_SIZE - 
                       (uint32_t)aligned_addr / PAGE_SIZE;
    
    // Create flags
    uint32_t flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
    if (writable) flags |= PAGE_FLAG_WRITABLE;
    if (!executable) flags |= PAGE_FLAG_GUARD; // Custom flag indicating NX (not executable)
    
    // Allocate and map pages
    for (uint32_t i = 0; i < num_pages; i++) {
        void* phys_page = allocate_page();
        if (!phys_page) {
            // Free previously allocated pages on failure
            for (uint32_t j = 0; j < i; j++) {
                void* virt = (void*)((uint32_t)aligned_addr + j * PAGE_SIZE);
                uint32_t phys = get_physical_address(virt);
                unmap_page(virt);
                free_page((void*)phys);
            }
            return -1;
        }
        
        void* virt_page = (void*)((uint32_t)aligned_addr + i * PAGE_SIZE);
        if (paging_map_page(phys_page, virt_page, flags) != 0) {
            free_page(phys_page);
            return -1;
        }
    }
    
    return 0;
}

/**
 * Get free pages count
 */
uint32_t get_free_pages_count() {
    return free_pages;
}

/**
 * Get the physical address for a virtual address
 */
static uint32_t get_physical_address(void* virtual) {
    uint32_t pd_index = (uint32_t)virtual >> 22;
    uint32_t pt_index = ((uint32_t)virtual >> 12) & 0x3FF;
    uint32_t offset = (uint32_t)virtual & 0xFFF;
    
    // Check if page directory entry exists
    if (!(current_page_directory[pd_index] & PAGE_FLAG_PRESENT)) {
        return 0;
    }
    
    // Get page table
    page_table_t* table = (page_table_t*)(current_page_directory[pd_index] & ~0xFFF);
    
    // Check if page table entry exists
    if (!(table->entries[pt_index] & PAGE_FLAG_PRESENT)) {
        return 0;
    }
    
    // Combine physical page address with offset
    return (table->entries[pt_index] & ~0xFFF) | offset;
}

/**
 * Add a protected memory region
 */
int paging_protect_region(void* start, void* end, uint32_t flags, const char* name) {
    if (num_protected_regions >= MAX_PROTECTED_REGIONS) {
        log_error("Paging: Cannot protect region, maximum number already defined");
        return -1;
    }
    
    protected_regions[num_protected_regions].start = start;
    protected_regions[num_protected_regions].end = end;
    protected_regions[num_protected_regions].flags = flags;
    protected_regions[num_protected_regions].name = name;
    num_protected_regions++;
    
    log_info("Paging: Protected region %s: %p-%p with flags 0x%x", 
             name, start, end, flags);
             
    return 0;
}

/**
 * Check if an address is in a protected region
 */
static protected_region_t* get_protected_region(void* addr) {
    for (int i = 0; i < num_protected_regions; i++) {
        if (addr >= protected_regions[i].start && addr < protected_regions[i].end) {
            return &protected_regions[i];
        }
    }
    return NULL;
}

/**
 * Page fault handler
 */
void page_fault_handler() {
    uint32_t fault_addr;
    asm volatile("movl %%cr2, %0" : "=r"(fault_addr));
    
    uint32_t error_code;
    asm volatile("popl %0" : "=r"(error_code));
    
    int present = !(error_code & 1);    // Page not present
    int write = error_code & 2;         // Write operation
    int user = error_code & 4;          // User mode access
    int reserved = error_code & 8;      // Reserved bit violation
    int instruction = error_code & 16;  // Instruction fetch
    
    memory_stats.page_faults++;
    
    // Check if this is a copy-on-write fault we can handle
    if (!present && write && handle_page_fault((void*)fault_addr, write, user) == 0) {
        return; // Successfully handled
    }
    
    // Check if address is in a protected region
    protected_region_t* region = get_protected_region((void*)fault_addr);
    if (region) {
        log_error("Page Fault: Access violation in protected region %s at address %p", 
                 region->name, (void*)fault_addr);
    } else {
        log_error("Page Fault: %s %s %s %s %s at address %p",
                 present ? "protection" : "non-present",
                 write ? "write" : "read",
                 user ? "user" : "kernel",
                 reserved ? "reserved bit" : "",
                 instruction ? "instruction" : "data",
                 (void*)fault_addr);
    }
    
    // Get info about the faulting process
    extern uint32_t current_process_id;
    extern const char* get_process_name(uint32_t pid);
    
    log_error("Process %u (%s) caused the fault", 
             current_process_id, 
             get_process_name(current_process_id));
             
    // Emergency halt for kernel faults, user process termination for user faults
    if (!user) {
        log_emergency("Kernel page fault - system halted");
        asm volatile("cli; hlt");
    } else {
        log_warning("Terminating faulting process");
        extern void terminate_process(uint32_t pid);
        terminate_process(current_process_id);
    }
}

/**
 * Handle copy-on-write page faults
 */
static int handle_page_fault(void* fault_addr, int is_write, int is_user) {
    // Calculate page address
    void* page_addr = (void*)((uint32_t)fault_addr & ~0xFFF);
    
    // First check if it's a COW fault
    uint32_t pd_idx = (uint32_t)page_addr >> 22;
    uint32_t pt_idx = ((uint32_t)page_addr >> 12) & 0x3FF;
    
    // Need page directory and table for current address space
    if (!(current_page_directory[pd_idx] & PAGE_FLAG_PRESENT)) {
        return -1; // Not a valid mapping
    }
    
    page_table_t* pt = (page_table_t*)(current_page_directory[pd_idx] & ~0xFFF);
    
    // Check if page is present but write-protected (COW)
    if ((pt->entries[pt_idx] & PAGE_FLAG_PRESENT) && 
        !(pt->entries[pt_idx] & PAGE_FLAG_WRITABLE) &&
        is_write) {
        
        // Get physical address of the shared page
        uint32_t old_phys = pt->entries[pt_idx] & ~0xFFF;
        uint32_t page_idx = old_phys / PAGE_SIZE;
        
        // Verify it's a COW page with multiple references
        if (page_info_array[page_idx].references > 1) {
            // Allocate new physical page for this process
            void* new_phys = allocate_page();
            if (!new_phys) {
                log_error("Page Fault: Failed to allocate page for COW");
                return -1;
            }
            
            // Copy content from shared page
            memcpy(new_phys, (void*)old_phys, PAGE_SIZE);
            
            // Reduce reference count on old page
            page_info_array[page_idx].references--;
            
            // Update page table to use new writable page
            pt->entries[pt_idx] = (uint32_t)new_phys | 
                                 (pt->entries[pt_idx] & 0xFFF) | 
                                 PAGE_FLAG_WRITABLE;
                                 
            // Invalidate TLB entry
            flush_tlb_entry(page_addr);
            
            return 0; // Successfully handled COW fault
        }
        
        // If this is the only reference, just make it writable
        if (page_info_array[page_idx].references == 1) {
            pt->entries[pt_idx] |= PAGE_FLAG_WRITABLE;
            flush_tlb_entry(page_addr);
            return 0;
        }
    }
    
    // Handle potential demand paging
    if (!(pt->entries[pt_idx] & PAGE_FLAG_PRESENT) && 
        shadow_page_directory[pd_idx] != 0) {
        
        // Check if this page exists in the shadow directory (for demand paging)
        page_table_t* shadow_pt = (page_table_t*)(shadow_page_directory[pd_idx] & ~0xFFF);
        if (shadow_pt && (shadow_pt->entries[pt_idx] & PAGE_FLAG_PRESENT)) {
            // Allocate new physical page
            void* new_phys = allocate_page();
            if (!new_phys) {
                log_error("Page Fault: Failed to allocate page for demand paging");
                return -1;
            }
            
            // Copy content from shadow page
            uint32_t shadow_phys = shadow_pt->entries[pt_idx] & ~0xFFF;
            memcpy(new_phys, (void*)shadow_phys, PAGE_SIZE);
            
            // Map new page with appropriate permissions
            uint32_t flags = (shadow_pt->entries[pt_idx] & 0xFFF) | 
                           (is_write ? PAGE_FLAG_WRITABLE : 0);
            
            pt->entries[pt_idx] = (uint32_t)new_phys | flags;
            flush_tlb_entry(page_addr);
            
            return 0; // Successfully handled demand paging
        }
    }
    
    return -1; // Could not handle this page fault
}

/**
 * Flush a single TLB entry
 */
static void flush_tlb_entry(void* addr) {
    asm volatile("invlpg (%0)" : : "r" (addr) : "memory");
}

/**
 * Flush the entire TLB
 */
static void flush_entire_tlb() {
    uint32_t cr3;
    asm volatile("movl %%cr3, %0" : "=r"(cr3));
    asm volatile("movl %0, %%cr3" : : "r"(cr3));
    memory_stats.tlb_flushes++;
}

/**
 * Get memory statistics
 */
void paging_get_stats(uint32_t* stats, uint32_t size) {
    if (size >= sizeof(memory_stats)) {
        memcpy(stats, &memory_stats, sizeof(memory_stats));
    }
}

/**
 * Reset memory statistics counters
 */
void paging_reset_stats() {
    memset(&memory_stats, 0, sizeof(memory_stats));
}