/**
 * HAL Memory Implementation for x86 Architecture
 *
 * This file implements memory management functions defined in hal_memory.h
 * for the x86 architecture, providing physical and virtual memory management,
 * page table operations, and cache control.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../include/hal_memory.h"
#include "../include/hal_cpu.h"
#include "../../kernel/logging/log.h"

// x86-specific constants
#define PAGE_SIZE               4096
#define PAGE_SHIFT              12
#define ENTRIES_PER_TABLE       1024
#define PAGE_PRESENT            0x001
#define PAGE_WRITE              0x002
#define PAGE_USER               0x004
#define PAGE_WRITE_THROUGH      0x008
#define PAGE_CACHE_DISABLE      0x010
#define PAGE_ACCESSED           0x020
#define PAGE_DIRTY              0x040
#define PAGE_SIZE_BIT           0x080
#define PAGE_GLOBAL             0x100
#define PAGE_NX                 0x8000000000000000ULL  // PAE/64-bit only

// Page directory and table types
typedef uint32_t page_directory_entry_t;
typedef uint32_t page_table_entry_t;

// Current page directory (physical address)
static uintptr_t current_page_directory = 0;

// Memory map
static hal_memory_map_t memory_map;
static hal_physical_range_t memory_ranges[32];  // Support up to 32 memory regions

// Physical memory allocation bitmap
static uint8_t* physical_bitmap = NULL;
static size_t physical_bitmap_size = 0;
static size_t total_physical_pages = 0;
static size_t free_physical_pages = 0;

// Memory feature flags
static bool supports_no_execute = false;
static bool supports_global_pages = false;
static bool supports_pae = false;
static bool paging_enabled = false;

/**
 * Initialize the memory subsystem
 */
int hal_memory_initialize(void) {
    log_info("HAL Memory", "Initializing memory subsystem for x86");
    
    // Detect CPU features related to memory management
    hal_cpu_info_t cpu_info;
    hal_cpu_get_info(&cpu_info);
    
    supports_no_execute = cpu_info.has_nx;
    supports_global_pages = cpu_info.has_pge;
    supports_pae = cpu_info.has_pae;
    
    log_debug("HAL Memory", "CPU features: NX=%s, Global Pages=%s, PAE=%s",
              supports_no_execute ? "supported" : "unsupported",
              supports_global_pages ? "supported" : "unsupported",
              supports_pae ? "supported" : "unsupported");
    
    // Initialize memory map
    memory_map.range_count = 0;
    memory_map.ranges = memory_ranges;
    
    // Parse memory map provided by bootloader
    // For now, use a simple memory layout for demonstration
    // In a real OS, this would parse E820 entries or similar

    // Assume we have 128MB of RAM for demonstration
    const size_t demo_ram_size = 128 * 1024 * 1024; // 128 MB
    
    // First 1MB is reserved (BIOS, video memory, etc.)
    memory_ranges[0].start = 0;
    memory_ranges[0].size = 1 * 1024 * 1024;
    memory_ranges[0].type = HAL_MEM_RESERVED;
    memory_ranges[0].available = false;
    
    // Main memory (1MB - 128MB)
    memory_ranges[1].start = 1 * 1024 * 1024;
    memory_ranges[1].size = demo_ram_size - memory_ranges[0].size;
    memory_ranges[1].type = HAL_MEM_RAM;
    memory_ranges[1].available = true;
    
    memory_map.range_count = 2;
    
    // Calculate total and free memory
    total_physical_pages = 0;
    free_physical_pages = 0;
    
    for (size_t i = 0; i < memory_map.range_count; i++) {
        if (memory_map.ranges[i].type == HAL_MEM_RAM) {
            size_t pages = memory_map.ranges[i].size / PAGE_SIZE;
            total_physical_pages += pages;
            
            if (memory_map.ranges[i].available) {
                free_physical_pages += pages;
            }
        }
    }
    
    log_info("HAL Memory", "Total physical memory: %zu MB (%zu pages)",
             (total_physical_pages * PAGE_SIZE) / (1024 * 1024),
             total_physical_pages);
    log_info("HAL Memory", "Available physical memory: %zu MB (%zu pages)",
             (free_physical_pages * PAGE_SIZE) / (1024 * 1024),
             free_physical_pages);
    
    // Initialize physical memory bitmap
    // Each bit represents one page, so we need (total_pages / 8) bytes
    physical_bitmap_size = (total_physical_pages + 7) / 8;
    
    // Allocate bitmap from kernel heap (in a real OS this would use a bootstrap allocator)
    // For now, place it at a predefined location after kernel
    physical_bitmap = (uint8_t*)0x00400000;  // Example address
    
    // Mark all pages as used initially
    memset(physical_bitmap, 0xFF, physical_bitmap_size);
    
    // Mark available regions as free
    for (size_t i = 0; i < memory_map.range_count; i++) {
        if (memory_map.ranges[i].available) {
            uintptr_t start_page = memory_map.ranges[i].start / PAGE_SIZE;
            size_t num_pages = memory_map.ranges[i].size / PAGE_SIZE;
            
            for (size_t page = 0; page < num_pages; page++) {
                size_t bit_index = start_page + page;
                physical_bitmap[bit_index / 8] &= ~(1 << (bit_index % 8));
            }
        }
    }
    
    // Mark the bitmap itself as used
    uintptr_t bitmap_start_page = (uintptr_t)physical_bitmap / PAGE_SIZE;
    size_t bitmap_pages = (physical_bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t page = 0; page < bitmap_pages; page++) {
        size_t bit_index = bitmap_start_page + page;
        physical_bitmap[bit_index / 8] |= (1 << (bit_index % 8));
    }
    
    // Check if paging is enabled
    uint32_t cr0 = hal_cpu_read_cr0();
    paging_enabled = (cr0 & 0x80000000) != 0;
    
    if (paging_enabled) {
        log_debug("HAL Memory", "Paging is already enabled");
        // Get current page directory
        current_page_directory = hal_cpu_read_cr3();
    } else {
        log_debug("HAL Memory", "Paging is not enabled");
    }
    
    // Enable global pages if supported
    if (supports_global_pages) {
        uint32_t cr4 = hal_cpu_read_cr4();
        hal_cpu_write_cr4(cr4 | 0x80);  // Set PGE bit
        log_debug("HAL Memory", "Enabled global pages");
    }
    
    return 0;
}

/**
 * Shutdown the memory subsystem
 */
int hal_memory_finalize(void) {
    log_info("HAL Memory", "Shutting down memory subsystem");
    
    // Nothing specific to clean up for now
    
    return 0;
}

/**
 * Get the system memory map
 */
int hal_memory_get_map(hal_memory_map_t* map) {
    if (map == NULL) {
        return -1;
    }
    
    // Copy memory map
    map->range_count = memory_map.range_count;
    map->ranges = memory_map.ranges;
    
    return 0;
}

/**
 * Allocate physical memory
 */
uintptr_t hal_physical_alloc(size_t pages) {
    if (pages == 0 || pages > free_physical_pages) {
        return 0;
    }
    
    size_t consecutive_free = 0;
    size_t start_page = 0;
    
    // Find a block of contiguous free pages
    for (size_t i = 0; i < total_physical_pages; i++) {
        if ((physical_bitmap[i / 8] & (1 << (i % 8))) == 0) {
            // Page is free
            if (consecutive_free == 0) {
                start_page = i;
            }
            consecutive_free++;
            
            if (consecutive_free >= pages) {
                // Found enough consecutive pages
                // Mark them as used
                for (size_t j = 0; j < pages; j++) {
                    size_t page_index = start_page + j;
                    physical_bitmap[page_index / 8] |= (1 << (page_index % 8));
                }
                
                free_physical_pages -= pages;
                return start_page * PAGE_SIZE;
            }
        } else {
            // Page is used, reset counter
            consecutive_free = 0;
        }
    }
    
    // Could not find enough contiguous pages
    return 0;
}

/**
 * Free physical memory
 */
void hal_physical_free(uintptr_t addr, size_t pages) {
    if (addr % PAGE_SIZE != 0 || pages == 0) {
        return;
    }
    
    size_t start_page = addr / PAGE_SIZE;
    
    // Mark pages as free
    for (size_t i = 0; i < pages; i++) {
        size_t page_index = start_page + i;
        
        // Only free the page if it's currently marked as used
        if (page_index < total_physical_pages &&
            (physical_bitmap[page_index / 8] & (1 << (page_index % 8)))) {
            
            physical_bitmap[page_index / 8] &= ~(1 << (page_index % 8));
            free_physical_pages++;
        }
    }
}

/**
 * Get the number of free physical pages
 */
size_t hal_physical_get_free_pages(void) {
    return free_physical_pages;
}

/**
 * Get the total number of physical pages
 */
size_t hal_physical_get_total_pages(void) {
    return total_physical_pages;
}

/**
 * Convert hal_page_flags_t to x86 page flags
 */
static uint32_t convert_page_flags(hal_page_flags_t flags) {
    uint32_t x86_flags = PAGE_PRESENT; // Always present
    
    // Access flags
    if (flags.access == HAL_MEM_ACCESS_RW || 
        flags.access == HAL_MEM_ACCESS_RWX) {
        x86_flags |= PAGE_WRITE;
    }
    
    // Cache mode
    switch (flags.cache) {
        case HAL_CACHE_DISABLED:
            x86_flags |= PAGE_CACHE_DISABLE;
            break;
        case HAL_CACHE_WRITE_THROUGH:
            x86_flags |= PAGE_WRITE_THROUGH;
            break;
        case HAL_CACHE_UNCACHED_DEVICE:
            x86_flags |= (PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH);
            break;
        case HAL_CACHE_WRITE_BACK:
            // Default behavior, no flags needed
            break;
    }
    
    // Other flags
    if (flags.global && supports_global_pages) {
        x86_flags |= PAGE_GLOBAL;
    }
    
    if (flags.user) {
        x86_flags |= PAGE_USER;
    }
    
    // Note: NX bit is handled separately in PAE mode
    
    return x86_flags;
}

/**
 * Map virtual memory to physical memory
 */
int hal_virtual_map(uintptr_t virtual_addr, uintptr_t physical_addr, 
                   size_t pages, hal_page_flags_t flags) {
    if (!paging_enabled || current_page_directory == 0) {
        return -1;
    }
    
    if (virtual_addr % PAGE_SIZE != 0 || physical_addr % PAGE_SIZE != 0 || pages == 0) {
        return -1;
    }
    
    uint32_t x86_flags = convert_page_flags(flags);
    
    // Get page directory
    page_directory_entry_t* page_dir = (page_directory_entry_t*)current_page_directory;
    
    // Map each page
    for (size_t i = 0; i < pages; i++) {
        uintptr_t vaddr = virtual_addr + i * PAGE_SIZE;
        uintptr_t paddr = physical_addr + i * PAGE_SIZE;
        
        // Get indices
        uint32_t pd_index = (vaddr >> 22) & 0x3FF;
        uint32_t pt_index = (vaddr >> 12) & 0x3FF;
        
        // Check if page table exists
        if ((page_dir[pd_index] & PAGE_PRESENT) == 0) {
            // Need to create a page table
            uintptr_t pt_phys = hal_physical_alloc(1);
            if (pt_phys == 0) {
                return -1;
            }
            
            // Clear the new page table
            memset((void*)pt_phys, 0, PAGE_SIZE);
            
            // Update page directory
            page_dir[pd_index] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
        
        // Get page table
        page_table_entry_t* page_table = (page_table_entry_t*)(page_dir[pd_index] & ~0xFFF);
        
        // Map the page
        page_table[pt_index] = paddr | x86_flags;
        
        // Invalidate TLB for this page
        hal_tlb_invalidate_page(vaddr);
    }
    
    return 0;
}

/**
 * Unmap virtual memory
 */
int hal_virtual_unmap(uintptr_t virtual_addr, size_t pages) {
    if (!paging_enabled || current_page_directory == 0) {
        return -1;
    }
    
    if (virtual_addr % PAGE_SIZE != 0 || pages == 0) {
        return -1;
    }
    
    // Get page directory
    page_directory_entry_t* page_dir = (page_directory_entry_t*)current_page_directory;
    
    // Unmap each page
    for (size_t i = 0; i < pages; i++) {
        uintptr_t vaddr = virtual_addr + i * PAGE_SIZE;
        
        // Get indices
        uint32_t pd_index = (vaddr >> 22) & 0x3FF;
        uint32_t pt_index = (vaddr >> 12) & 0x3FF;
        
        // Check if page table exists
        if ((page_dir[pd_index] & PAGE_PRESENT) != 0) {
            // Get page table
            page_table_entry_t* page_table = (page_table_entry_t*)(page_dir[pd_index] & ~0xFFF);
            
            // Unmap the page
            page_table[pt_index] = 0;
            
            // Invalidate TLB for this page
            hal_tlb_invalidate_page(vaddr);
        }
    }
    
    return 0;
}

/**
 * Get the physical address for a virtual address
 */
int hal_virtual_get_mapping(uintptr_t virtual_addr, uintptr_t* physical_addr, 
                          hal_page_flags_t* flags) {
    if (!paging_enabled || current_page_directory == 0) {
        return -1;
    }
    
    if (virtual_addr % PAGE_SIZE != 0 || physical_addr == NULL) {
        return -1;
    }
    
    // Get page directory
    page_directory_entry_t* page_dir = (page_directory_entry_t*)current_page_directory;
    
    // Get indices
    uint32_t pd_index = (virtual_addr >> 22) & 0x3FF;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;
    
    // Check if page table exists
    if ((page_dir[pd_index] & PAGE_PRESENT) == 0) {
        return -1;
    }
    
    // Get page table
    page_table_entry_t* page_table = (page_table_entry_t*)(page_dir[pd_index] & ~0xFFF);
    
    // Check if page exists
    if ((page_table[pt_index] & PAGE_PRESENT) == 0) {
        return -1;
    }
    
    // Get physical address
    *physical_addr = page_table[pt_index] & ~0xFFF;
    
    // Get flags if requested
    if (flags != NULL) {
        uint32_t x86_flags = page_table[pt_index] & 0xFFF;
        
        // Clear default values
        memset(flags, 0, sizeof(hal_page_flags_t));
        
        // Set access permissions
        if (x86_flags & PAGE_WRITE) {
            flags->access = HAL_MEM_ACCESS_RW;
        } else {
            flags->access = HAL_MEM_ACCESS_RO;
        }
        
        // Set cache mode
        if (x86_flags & PAGE_CACHE_DISABLE) {
            if (x86_flags & PAGE_WRITE_THROUGH) {
                flags->cache = HAL_CACHE_UNCACHED_DEVICE;
            } else {
                flags->cache = HAL_CACHE_DISABLED;
            }
        } else if (x86_flags & PAGE_WRITE_THROUGH) {
            flags->cache = HAL_CACHE_WRITE_THROUGH;
        } else {
            flags->cache = HAL_CACHE_WRITE_BACK;
        }
        
        // Set other flags
        flags->global = (x86_flags & PAGE_GLOBAL) != 0;
        flags->dirty = (x86_flags & PAGE_DIRTY) != 0;
        flags->accessed = (x86_flags & PAGE_ACCESSED) != 0;
        flags->user = (x86_flags & PAGE_USER) != 0;
        flags->no_execute = false; // NX bit not available in standard 32-bit paging
    }
    
    return 0;
}

/**
 * Set flags for a virtual memory range
 */
int hal_virtual_set_flags(uintptr_t virtual_addr, size_t pages, hal_page_flags_t flags) {
    if (!paging_enabled || current_page_directory == 0) {
        return -1;
    }
    
    if (virtual_addr % PAGE_SIZE != 0 || pages == 0) {
        return -1;
    }
    
    uint32_t x86_flags = convert_page_flags(flags);
    
    // Get page directory
    page_directory_entry_t* page_dir = (page_directory_entry_t*)current_page_directory;
    
    // Update each page
    for (size_t i = 0; i < pages; i++) {
        uintptr_t vaddr = virtual_addr + i * PAGE_SIZE;
        
        // Get indices
        uint32_t pd_index = (vaddr >> 22) & 0x3FF;
        uint32_t pt_index = (vaddr >> 12) & 0x3FF;
        
        // Check if page table exists
        if ((page_dir[pd_index] & PAGE_PRESENT) != 0) {
            // Get page table
            page_table_entry_t* page_table = (page_table_entry_t*)(page_dir[pd_index] & ~0xFFF);
            
            // Check if page exists
            if ((page_table[pt_index] & PAGE_PRESENT) != 0) {
                // Update flags, preserving physical address
                page_table[pt_index] = (page_table[pt_index] & ~0xFFF) | x86_flags;
                
                // Invalidate TLB for this page
                hal_tlb_invalidate_page(vaddr);
            }
        }
    }
    
    return 0;
}

/**
 * Invalidate a TLB entry for a specific virtual address
 */
void hal_tlb_invalidate_page(uintptr_t virtual_addr) {
    hal_cpu_invalidate_tlb((void*)virtual_addr);
}

/**
 * Invalidate the entire TLB
 */
void hal_tlb_invalidate_all(void) {
    hal_cpu_invalidate_tlb_all();
}

/**
 * Cache Management Functions
 * These delegate to CPU-specific cache instructions
 */

void hal_cache_invalidate_data(void* addr, size_t size) {
    // Call the assembly implementation
    hal_cpu_cache_flush(addr, size);
}

void hal_cache_clean_data(void* addr, size_t size) {
    // On x86, this is the same as flush
    hal_cpu_cache_flush(addr, size);
}

void hal_cache_flush_data(void* addr, size_t size) {
    hal_cpu_cache_flush(addr, size);
}

void hal_cache_invalidate_instruction(void* addr, size_t size) {
    // x86 has a unified instruction/data cache, so this is a no-op
}

void hal_cache_clean_and_invalidate_all(void) {
    hal_cpu_cache_invalidate();
}

/**
 * Memory Barriers
 * These delegate to CPU-specific memory barrier instructions
 */

void hal_memory_barrier(void) {
    hal_cpu_memory_barrier();
}

void hal_memory_barrier_read(void) {
    hal_cpu_memory_barrier_read();
}

void hal_memory_barrier_write(void) {
    hal_cpu_memory_barrier_write();
}

void hal_memory_barrier_instruction(void) {
    hal_cpu_memory_barrier_instruction();
}

/**
 * Architecture-Specific Memory Features
 */

bool hal_memory_supports_no_execute(void) {
    return supports_no_execute;
}

bool hal_memory_supports_global_pages(void) {
    return supports_global_pages;
}

size_t hal_memory_get_page_size(void) {
    return PAGE_SIZE;
}

/**
 * Get the total physical memory size
 */
uint64_t hal_memory_get_physical_size(void) {
    return (uint64_t)total_physical_pages * PAGE_SIZE;
}