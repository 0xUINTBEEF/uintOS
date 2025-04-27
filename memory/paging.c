#include "paging.h"
#include <stdint.h>
#include "../kernel/io.h"

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