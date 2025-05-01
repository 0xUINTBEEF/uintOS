#include "paging.h"
#include <stdint.h>
#include "../kernel/io.h"
#include "../kernel/security.h"
#include "../kernel/logging/log.h"

#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIRECTORY_ENTRIES 1024
#define TOTAL_MEMORY_PAGES (PAGE_DIRECTORY_ENTRIES * PAGE_TABLE_ENTRIES)

static uint32_t page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_table[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

// Bitmap to track page allocation status
static uint8_t page_bitmap[TOTAL_MEMORY_PAGES / 8];

// Initialize the paging system
void paging_init() {
    // Clear page bitmap
    for (int i = 0; i < TOTAL_MEMORY_PAGES / 8; i++) {
        page_bitmap[i] = 0;
    }

    // Mark the first few pages as reserved (for kernel use)
    for (int i = 0; i < 16; i++) {
        page_bitmap[i / 8] |= (1 << (i % 8));
    }

    // Map the first 4MB of memory
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        page_table[i] = (i * PAGE_SIZE) | 3; // Present and writable
    }

    // Point the first entry of the page directory to the page table
    page_directory[0] = ((uint32_t)page_table) | 3; // Present and writable

    // Set all other entries in the page directory to not present
    for (int i = 1; i < PAGE_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // Load the page directory into CR3
    asm volatile("mov %0, %%cr3" : : "r"(page_directory));

    // Enable paging by setting the PG bit in CR0
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Set the PG bit
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

// Find a free page in the bitmap
static int find_free_page() {
    for (int i = 0; i < TOTAL_MEMORY_PAGES / 8; i++) {
        if (page_bitmap[i] != 0xFF) {
            // Found a byte with at least one free bit
            for (int j = 0; j < 8; j++) {
                if (!(page_bitmap[i] & (1 << j))) {
                    return i * 8 + j;
                }
            }
        }
    }
    return -1; // No free pages
}

// Allocate a single page
void* allocate_page() {
    int page_index = find_free_page();
    if (page_index == -1) {
        return 0; // Out of memory
    }
    
    // Mark the page as used
    page_bitmap[page_index / 8] |= (1 << (page_index % 8));
    
    // Return the physical address
    return (void*)(page_index * PAGE_SIZE);
}

// Free a previously allocated page
void free_page(void* page) {
    uint32_t page_index = (uint32_t)page / PAGE_SIZE;
    
    if (page_index >= TOTAL_MEMORY_PAGES) {
        return; // Invalid page address
    }
    
    // Mark the page as free
    page_bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

// Allocate multiple contiguous pages
void* allocate_pages(uint32_t num) {
    if (num == 0) return 0;
    
    // Find contiguous free pages
    for (int start = 0; start <= TOTAL_MEMORY_PAGES - num; start++) {
        int i;
        for (i = 0; i < num; i++) {
            int page_index = start + i;
            if (page_bitmap[page_index / 8] & (1 << (page_index % 8))) {
                break; // This page is already allocated
            }
        }
        
        if (i == num) {
            // Found enough contiguous free pages
            for (i = 0; i < num; i++) {
                int page_index = start + i;
                page_bitmap[page_index / 8] |= (1 << (page_index % 8));
            }
            return (void*)(start * PAGE_SIZE);
        }
    }
    
    return 0; // Not enough contiguous free pages
}

// Free multiple contiguous pages
void free_pages(void* start, uint32_t num) {
    uint32_t page_start = (uint32_t)start / PAGE_SIZE;
    
    for (uint32_t i = 0; i < num; i++) {
        uint32_t page_index = page_start + i;
        if (page_index < TOTAL_MEMORY_PAGES) {
            page_bitmap[page_index / 8] &= ~(1 << (page_index % 8));
        }
    }
}

// Get the count of free pages
uint32_t get_free_pages_count() {
    uint32_t count = 0;
    for (int i = 0; i < TOTAL_MEMORY_PAGES; i++) {
        if (!(page_bitmap[i / 8] & (1 << (i % 8)))) {
            count++;
        }
    }
    return count;
}

// Map a physical page to a virtual address
void map_page(void* physical, void* virtual, uint32_t flags) {
    uint32_t pd_index = (uint32_t)virtual / (PAGE_SIZE * PAGE_TABLE_ENTRIES);
    uint32_t pt_index = ((uint32_t)virtual / PAGE_SIZE) % PAGE_TABLE_ENTRIES;
    
    // Create a new page table if necessary
    if (!(page_directory[pd_index] & 1)) {
        uint32_t* new_page_table = (uint32_t*)allocate_page();
        if (!new_page_table) return;
        
        // Clear the new page table
        for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            new_page_table[i] = 0;
        }
        
        page_directory[pd_index] = ((uint32_t)new_page_table) | 3;
    }
    
    // Get the page table
    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    
    // Set the entry
    page_table[pt_index] = ((uint32_t)physical) | flags;
    
    // Invalidate TLB entry for this address
    asm volatile("invlpg (%0)" : : "r" (virtual));
}

// Unmap a virtual address
void unmap_page(void* virtual) {
    uint32_t pd_index = (uint32_t)virtual / (PAGE_SIZE * PAGE_TABLE_ENTRIES);
    uint32_t pt_index = ((uint32_t)virtual / PAGE_SIZE) % PAGE_TABLE_ENTRIES;
    
    if (!(page_directory[pd_index] & 1)) {
        return; // Page directory entry not present
    }
    
    // Get the page table
    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    
    // Clear the entry
    page_table[pt_index] = 0;
    
    // Invalidate TLB entry for this address
    asm volatile("invlpg (%0)" : : "r" (virtual));
}

/**
 * Map a page with specific security flags
 */
int paging_map_page(void* physical, void* virtual, uint32_t flags) {
    // Check security permissions first
    if (!security_check_permission(PERM_MAP)) {
        log_error("PAGING", "Security violation: Attempted to map page without MAP permission");
        return -1;
    }

    uint32_t pd_index = (uint32_t)virtual / (PAGE_SIZE * PAGE_TABLE_ENTRIES);
    uint32_t pt_index = ((uint32_t)virtual / PAGE_SIZE) % PAGE_TABLE_ENTRIES;
    
    // Create a new page table if necessary
    if (!(page_directory[pd_index] & 1)) {
        // Need to allocate a new page table
        void* new_pt = allocate_page();
        if (!new_pt) {
            return -2; // Out of memory
        }
        
        // Clear the new page table
        uint32_t* pt_ptr = (uint32_t*)new_pt;
        for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt_ptr[i] = 0;
        }
        
        // Determine page directory entry flags
        uint32_t pd_flags = PAGE_FLAG_PRESENT;
        if (flags & PAGE_FLAG_USER) {
            pd_flags |= PAGE_FLAG_USER;
        }
        if (flags & PAGE_FLAG_WRITABLE) {
            pd_flags |= PAGE_FLAG_WRITABLE;
        }
        
        page_directory[pd_index] = ((uint32_t)new_pt) | pd_flags;
    }
    
    // Get the page table
    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    
    // Set the entry
    page_table[pt_index] = ((uint32_t)physical) | flags;
    
    // Invalidate TLB entry for this address
    asm volatile("invlpg (%0)" : : "r" (virtual));
    
    // Audit the memory mapping operation
    security_token_t* token = security_get_current_token();
    if (token) {
        char desc[64];
        snprintf(desc, sizeof(desc), "memory mapping at 0x%08x", (uint32_t)virtual);
        security_audit_action("map_page", desc, token, 1);
    }
    
    return 0;
}

/**
 * Get flags for a mapped page
 */
uint32_t paging_get_page_flags(void* virtual) {
    uint32_t pd_index = (uint32_t)virtual / (PAGE_SIZE * PAGE_TABLE_ENTRIES);
    uint32_t pt_index = ((uint32_t)virtual / PAGE_SIZE) % PAGE_TABLE_ENTRIES;
    
    if (!(page_directory[pd_index] & 1)) {
        return 0; // Page directory entry not present
    }
    
    // Get the page table
    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    
    // Return the page flags
    return page_table[pt_index] & 0xFFF;
}

/**
 * Update flags for an existing page mapping
 */
int paging_update_flags(void* virtual, uint32_t flags) {
    // Check security permissions
    if (!security_check_permission(PERM_MAP)) {
        log_error("PAGING", "Security violation: Attempted to update page flags without MAP permission");
        return -1;
    }
    
    uint32_t pd_index = (uint32_t)virtual / (PAGE_SIZE * PAGE_TABLE_ENTRIES);
    uint32_t pt_index = ((uint32_t)virtual / PAGE_SIZE) % PAGE_TABLE_ENTRIES;
    
    if (!(page_directory[pd_index] & 1)) {
        return -2; // Page directory entry not present
    }
    
    // Get the page table
    uint32_t* page_table = (uint32_t*)(page_directory[pd_index] & ~0xFFF);
    
    // Check if the page is present
    if (!(page_table[pt_index] & 1)) {
        return -3; // Page not present
    }
    
    // Update flags while preserving the physical address
    page_table[pt_index] = (page_table[pt_index] & ~0xFFF) | (flags & 0xFFF);
    
    // If user access is requested, ensure the page directory entry allows it
    if (flags & PAGE_FLAG_USER) {
        page_directory[pd_index] |= PAGE_FLAG_USER;
    }
    
    // If writable access is requested, ensure page directory entry allows it
    if (flags & PAGE_FLAG_WRITABLE) {
        page_directory[pd_index] |= PAGE_FLAG_WRITABLE;
    }
    
    // Invalidate TLB entry for this address
    asm volatile("invlpg (%0)" : : "r" (virtual));
    
    return 0;
}

/**
 * Create a new page directory for a process with appropriate permissions
 */
uint32_t paging_create_address_space(int kernel_accessible) {
    // Requires kernel privilege level
    if (!security_check_permission(PERM_MAP)) {
        log_error("PAGING", "Security violation: Attempted to create address space without MAP permission");
        return 0;
    }
    
    // Allocate memory for the new page directory
    uint32_t* new_pd = (uint32_t*)allocate_page();
    if (!new_pd) {
        return 0; // Out of memory
    }
    
    // Clear the new page directory
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        new_pd[i] = 0;
    }
    
    // Copy kernel space mappings (typically the higher half of memory)
    for (int i = PAGE_DIRECTORY_ENTRIES / 2; i < PAGE_DIRECTORY_ENTRIES; i++) {
        new_pd[i] = page_directory[i];
    }
    
    // If kernel accessible, also copy lower mappings that have the kernel flag
    if (kernel_accessible) {
        for (int i = 0; i < PAGE_DIRECTORY_ENTRIES / 2; i++) {
            if (page_directory[i] & 1) {
                // Only copy entries that don't have the user flag
                if (!(page_directory[i] & PAGE_FLAG_USER)) {
                    new_pd[i] = page_directory[i];
                }
            }
        }
    }
    
    return (uint32_t)new_pd;
}

/**
 * Switch to a different page directory (address space)
 */
void paging_switch_address_space(uint32_t page_directory_addr) {
    // This operation requires kernel privilege
    if (!security_check_permission(PERM_MAP)) {
        log_error("PAGING", "Security violation: Attempted to switch address space without MAP permission");
        return;
    }
    
    asm volatile("mov %0, %%cr3" : : "r"(page_directory_addr));
}

/**
 * Get the current page directory physical address
 */
uint32_t paging_get_current_address_space() {
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/**
 * Clone the current address space (for fork operations)
 */
uint32_t paging_clone_address_space(int copy_on_write) {
    // This operation requires kernel privilege
    if (!security_check_permission(PERM_MAP)) {
        log_error("PAGING", "Security violation: Attempted to clone address space without MAP permission");
        return 0;
    }
    
    // Create a new page directory
    uint32_t* new_pd = (uint32_t*)allocate_page();
    if (!new_pd) {
        return 0; // Out of memory
    }
    
    // Get current page directory address
    uint32_t current_pd_addr = paging_get_current_address_space();
    uint32_t* current_pd = (uint32_t*)current_pd_addr;
    
    // Clone the page directory entries
    for (int pd_idx = 0; pd_idx < PAGE_DIRECTORY_ENTRIES; pd_idx++) {
        if (current_pd[pd_idx] & 1) { // If present
            if (pd_idx < PAGE_DIRECTORY_ENTRIES / 2) { // User space
                // For user space, we need to clone the page tables
                uint32_t* current_pt = (uint32_t*)(current_pd[pd_idx] & ~0xFFF);
                uint32_t* new_pt = (uint32_t*)allocate_page();
                
                if (!new_pt) {
                    // Clean up allocated resources
                    // TODO: Free already allocated page tables
                    free_page(new_pd);
                    return 0;
                }
                
                // Copy flags from page directory entry
                new_pd[pd_idx] = (uint32_t)new_pt | (current_pd[pd_idx] & 0xFFF);
                
                // Clone page table entries
                for (int pt_idx = 0; pt_idx < PAGE_TABLE_ENTRIES; pt_idx++) {
                    if (current_pt[pt_idx] & 1) { // If present
                        if (copy_on_write && (current_pt[pt_idx] & PAGE_FLAG_WRITABLE)) {
                            // For copy-on-write, mark pages as read-only
                            new_pt[pt_idx] = current_pt[pt_idx] & ~PAGE_FLAG_WRITABLE;
                            current_pt[pt_idx] = current_pt[pt_idx] & ~PAGE_FLAG_WRITABLE;
                        } else {
                            // Just copy the page table entry as-is
                            new_pt[pt_idx] = current_pt[pt_idx];
                        }
                    } else {
                        new_pt[pt_idx] = 0;
                    }
                }
            } else {
                // For kernel space, just copy the page directory entry
                new_pd[pd_idx] = current_pd[pd_idx];
            }
        } else {
            new_pd[pd_idx] = 0;
        }
    }
    
    return (uint32_t)new_pd;
}

/**
 * Map user memory with appropriate security settings
 */
int paging_map_user_memory(void* virtual, size_t size, int writable, int executable) {
    // This function requires MAP permission
    if (!security_check_permission(PERM_MAP)) {
        log_error("PAGING", "Security violation: Attempted to map user memory without MAP permission");
        return -1;
    }
    
    // Round address down to page boundary
    uintptr_t start_addr = (uintptr_t)virtual & ~0xFFF;
    
    // Calculate end address (rounded up to page boundary)
    uintptr_t end_addr = ((uintptr_t)virtual + size + PAGE_SIZE - 1) & ~0xFFF;
    
    // Calculate number of pages required
    size_t page_count = (end_addr - start_addr) / PAGE_SIZE;
    
    // Allocate physical pages
    void* physical_pages = allocate_pages(page_count);
    if (!physical_pages) {
        return -2; // Out of memory
    }
    
    // Calculate flags
    uint32_t flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
    if (writable) {
        flags |= PAGE_FLAG_WRITABLE;
    }
    
    // Non-executable pages are not directly supported in 32-bit x86,
    // would require PAE or NX bit in newer CPUs
    
    // Map each page
    for (size_t i = 0; i < page_count; i++) {
        void* phys_addr = (void*)((uintptr_t)physical_pages + i * PAGE_SIZE);
        void* virt_addr = (void*)(start_addr + i * PAGE_SIZE);
        
        int result = paging_map_page(phys_addr, virt_addr, flags);
        if (result != 0) {
            // Failed to map page, clean up
            // TODO: Unmap already mapped pages
            free_pages(physical_pages, page_count);
            return -3;
        }
    }
    
    return 0;
}