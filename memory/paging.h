#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// Page states
#define PAGE_FREE      0
#define PAGE_USED      1
#define PAGE_RESERVED  2

// Memory constants
#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIRECTORY_ENTRIES 1024
#define TOTAL_MEMORY_PAGES 1024  // Represents 4MB of memory

// Page flags
#define PAGE_FLAG_PRESENT       0x01   // Page is present in memory
#define PAGE_FLAG_WRITABLE      0x02   // Page is writable
#define PAGE_FLAG_USER          0x04   // Page is accessible from user mode
#define PAGE_FLAG_WRITE_THROUGH 0x08   // Write-through caching enabled
#define PAGE_FLAG_CACHE_DISABLE 0x10   // Caching disabled
#define PAGE_FLAG_ACCESSED      0x20   // Page has been accessed
#define PAGE_FLAG_DIRTY         0x40   // Page has been written to
#define PAGE_FLAG_GLOBAL        0x100  // Page is global (not flushed from TLB)
#define PAGE_FLAG_GUARD         0x200  // Guard page (not a standard x86 flag, used by our OS)

// Memory management functions
void paging_init();
void* allocate_page();
void free_page(void* page);
void* allocate_pages(uint32_t num);
void free_pages(void* start, uint32_t num);
uint32_t get_free_pages_count();

// Memory mapping functions
void map_page(void* physical, void* virtual, uint32_t flags);
void unmap_page(void* virtual);

/**
 * Enhanced page management functions with security support
 */

/**
 * Map a page with specific security flags
 *
 * @param physical Physical address of the page
 * @param virtual Virtual address to map to
 * @param flags Page flags (including security flags)
 * @return 0 on success, error code on failure
 */
int paging_map_page(void* physical, void* virtual, uint32_t flags);

/**
 * Get flags for a mapped page
 *
 * @param virtual Virtual address of the page
 * @return Flags for the specified page, or 0 if not mapped
 */
uint32_t paging_get_page_flags(void* virtual);

/**
 * Update flags for an existing page mapping
 *
 * @param virtual Virtual address of the page
 * @param flags New flags to set
 * @return 0 on success, error code on failure
 */
int paging_update_flags(void* virtual, uint32_t flags);

/**
 * Create a new page directory for a process with appropriate permissions
 *
 * @param kernel_accessible Whether kernel pages should be accessible
 * @return Physical address of the new page directory, or 0 on failure
 */
uint32_t paging_create_address_space(int kernel_accessible);

/**
 * Switch to a different page directory (address space)
 *
 * @param page_directory Physical address of the page directory
 */
void paging_switch_address_space(uint32_t page_directory);

/**
 * Get the current page directory physical address
 *
 * @return Physical address of the current page directory
 */
uint32_t paging_get_current_address_space();

/**
 * Clone the current address space (for fork operations)
 *
 * @param copy_on_write Whether to use copy-on-write semantics
 * @return Physical address of the new page directory, or 0 on failure
 */
uint32_t paging_clone_address_space(int copy_on_write);

/**
 * Map user memory with appropriate security settings
 * 
 * @param virtual Virtual address to map
 * @param size Size of the memory region in bytes
 * @param writable Whether the memory should be writable
 * @param executable Whether the memory should be executable
 * @return 0 on success, error code on failure
 */
int paging_map_user_memory(void* virtual, size_t size, int writable, int executable);

#endif // PAGING_H