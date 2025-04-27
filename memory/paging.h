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

#endif // PAGING_H