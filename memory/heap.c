#include "heap.h"
#include "paging.h"
#include "../kernel/logging/log.h"
#include <stdint.h>
#include <string.h>

#define HEAP_MAGIC        0xC0DEABA
#define HEAP_MIN_SIZE     4096
#define HEAP_ALIGN        16
#define HEAP_GUARD_SIZE   16  // Size of guard region before/after allocations
#define HEAP_MAX_BINS     11  // Power-of-2 bins from 16 bytes to 16 KB
#define HEAP_BLOCK_FREE   0
#define HEAP_BLOCK_USED   1

// Enhanced memory safety with canary values
#define HEAP_CANARY_VALUE 0xFEEDFACE
#define USE_GUARD_PAGES   1   // Use guard pages for large allocations
#define POISON_FREED      1   // Fill freed blocks with pattern
#define POISON_PATTERN    0xDE // Pattern to fill freed memory

// Forward declaration for heap expansion
static void* heap_extend(heap_t* heap, uint32_t size);

// Improved heap block header with memory safety features
typedef struct block_header {
    uint32_t magic;                   // Magic value to detect corruption
    uint32_t size;                    // Size of this block (including header)
    uint8_t status;                   // HEAP_BLOCK_FREE or HEAP_BLOCK_USED
    struct block_header* prev;        // Previous block in physical memory
    struct block_header* next;        // Next block in physical memory
    struct block_header* next_free;   // Next free block (if free)
    struct block_header* prev_free;   // Previous free block (if free)
    uint32_t canary;                  // Canary value to detect buffer overflows
    char padding[8];                  // Padding to align data
} block_header_t;

// Footer to detect buffer underflows
typedef struct block_footer {
    uint32_t magic;                   // Should match the header's magic
    uint32_t header_ptr;              // Pointer to the header (for validation)
    uint32_t size;                    // Size (should match header's size)
    uint32_t canary;                  // Canary value to detect buffer overflows
} block_footer_t;

// Get the footer from a header
#define HEADER_TO_FOOTER(header) \
    ((block_footer_t*)((char*)(header) + (header)->size - sizeof(block_footer_t)))

// Get the data pointer from a header
#define HEADER_TO_DATA(header) \
    ((void*)((char*)(header) + sizeof(block_header_t)))

// Get the header from a data pointer
#define DATA_TO_HEADER(data) \
    ((block_header_t*)((char*)(data) - sizeof(block_header_t)))

// Initialize heap statistics (for monitoring)
typedef struct heap_stats {
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t current_allocations;
    uint32_t allocated_bytes;
    uint32_t free_bytes;
    uint32_t total_size;
    uint32_t fragmentation_count;
    uint32_t error_count;
    uint32_t largest_allocation;
    uint32_t smallest_allocation;
} heap_stats_t;

// Global heap stats (can be exposed via API)
static heap_stats_t heap_stats = {0};

// Initialize the heap
heap_t* heap_create(void* start, uint32_t size) {
    // Ensure minimum size
    if (size < HEAP_MIN_SIZE) {
        size = HEAP_MIN_SIZE;
    }
    
    // Align the heap start address
    uintptr_t addr = (uintptr_t)start;
    if (addr % HEAP_ALIGN != 0) {
        addr = (addr + HEAP_ALIGN) & ~(HEAP_ALIGN - 1);
        size -= addr - (uintptr_t)start;
    }
    
    // Create the heap structure at the beginning of the region
    heap_t* heap = (heap_t*)addr;
    addr += sizeof(heap_t);
    size -= sizeof(heap_t);
    
    // Initialize heap structure
    heap->start = (void*)addr;
    heap->end = (void*)(addr + size);
    heap->max_size = size;
    heap->current_size = size;
    heap->error_count = 0;
    
    // Initialize free list bins (power of 2 sizes)
    for (int i = 0; i < HEAP_MAX_BINS; i++) {
        heap->bins[i] = NULL;
    }
    
    // Create the initial free block
    block_header_t* initial_block = (block_header_t*)addr;
    initial_block->magic = HEAP_MAGIC;
    initial_block->size = size;
    initial_block->status = HEAP_BLOCK_FREE;
    initial_block->prev = NULL;
    initial_block->next = NULL;
    initial_block->prev_free = NULL;
    initial_block->canary = HEAP_CANARY_VALUE;
    
    // Create footer
    block_footer_t* footer = HEADER_TO_FOOTER(initial_block);
    footer->magic = HEAP_MAGIC;
    footer->header_ptr = (uint32_t)initial_block;
    footer->size = size;
    footer->canary = HEAP_CANARY_VALUE;
    
    // Add to appropriate size bin
    int bin = get_bin_for_size(size - sizeof(block_header_t) - sizeof(block_footer_t));
    
    if (bin < HEAP_MAX_BINS) {
        // Add to appropriate bin
        initial_block->next_free = heap->bins[bin];
        if (heap->bins[bin]) {
            heap->bins[bin]->prev_free = initial_block;
        }
        heap->bins[bin] = initial_block;
    } else {
        // Add to last bin for oversized blocks
        initial_block->next_free = heap->bins[HEAP_MAX_BINS - 1];
        if (heap->bins[HEAP_MAX_BINS - 1]) {
            heap->bins[HEAP_MAX_BINS - 1]->prev_free = initial_block;
        }
        heap->bins[HEAP_MAX_BINS - 1] = initial_block;
    }
    
    // Initialize heap stats
    heap_stats.total_size = size;
    heap_stats.free_bytes = size;
    
    log_info("Heap: Initialized at %p with size %u bytes", heap->start, size);
    return heap;
}

// Calculate which bin a size belongs in
int get_bin_for_size(uint32_t size) {
    // Minimum allocation size is 16 bytes (bin 0)
    if (size <= 16) {
        return 0;
    }
    
    // Bins are powers of 2: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384+
    int bin = 0;
    size = size - 1; // Ensure we round up to next power of 2
    
    while (size > 16) {
        size >>= 1;
        bin++;
    }
    
    return (bin < HEAP_MAX_BINS) ? bin : HEAP_MAX_BINS - 1;
}

// Find a suitable block for allocation
static block_header_t* find_free_block(heap_t* heap, uint32_t size) {
    // Calculate which bin to start with
    int bin = get_bin_for_size(size);
    
    // Look in each bin, starting from the smallest appropriate one
    for (int i = bin; i < HEAP_MAX_BINS; i++) {
        block_header_t* current = heap->bins[i];
        
        // Search this bin
        while (current) {
            uint32_t available_size = current->size - sizeof(block_header_t) - sizeof(block_footer_t);
            
            if (available_size >= size) {
                // Found a suitable block
                return current;
            }
            
            current = current->next_free;
        }
    }
    
    // No suitable block found
    return NULL;
}

// Remove a block from its free bin
static void remove_from_free_list(heap_t* heap, block_header_t* block) {
    // Calculate which bin this block is in
    int bin = get_bin_for_size(block->size - sizeof(block_header_t) - sizeof(block_footer_t));
    if (bin >= HEAP_MAX_BINS) bin = HEAP_MAX_BINS - 1;
    
    // Update adjacent free blocks
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        // Block was the head of the list
        heap->bins[bin] = block->next_free;
    }
    
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
    
    // Clear free pointers
    block->next_free = NULL;
    block->prev_free = NULL;
}

// Add a block to the appropriate free bin
static void add_to_free_list(heap_t* heap, block_header_t* block) {
    // Calculate which bin this block should go in
    int bin = get_bin_for_size(block->size - sizeof(block_header_t) - sizeof(block_footer_t));
    if (bin >= HEAP_MAX_BINS) bin = HEAP_MAX_BINS - 1;
    
    // Add to front of bin
    block->next_free = heap->bins[bin];
    block->prev_free = NULL;
    
    if (heap->bins[bin]) {
        heap->bins[bin]->prev_free = block;
    }
    
    heap->bins[bin] = block;
}

// Validate block header and footer
static int validate_block(block_header_t* header) {
    // Check magic value
    if (header->magic != HEAP_MAGIC) {
        log_error("Heap: Block at %p has invalid magic value: 0x%x", header, header->magic);
        heap_stats.error_count++;
        return 0;
    }
    
    // Check canary value
    if (header->canary != HEAP_CANARY_VALUE) {
        log_error("Heap: Block at %p has corrupted canary value: 0x%x", header, header->canary);
        heap_stats.error_count++;
        return 0;
    }
    
    // Check footer
    block_footer_t* footer = HEADER_TO_FOOTER(header);
    
    if (footer->magic != HEAP_MAGIC) {
        log_error("Heap: Block at %p has footer with invalid magic: 0x%x", header, footer->magic);
        heap_stats.error_count++;
        return 0;
    }
    
    if (footer->header_ptr != (uint32_t)header) {
        log_error("Heap: Block at %p has mismatched footer header pointer: %p", 
                 header, (void*)footer->header_ptr);
        heap_stats.error_count++;
        return 0;
    }
    
    if (footer->size != header->size) {
        log_error("Heap: Block at %p has mismatched size: header=%u, footer=%u", 
                 header, header->size, footer->size);
        heap_stats.error_count++;
        return 0;
    }
    
    if (footer->canary != HEAP_CANARY_VALUE) {
        log_error("Heap: Buffer overflow detected in block at %p", header);
        heap_stats.error_count++;
        return 0;
    }
    
    return 1;
}

// Split a block if it's too large
static block_header_t* split_block(heap_t* heap, block_header_t* block, uint32_t size) {
    // Calculate actual needed size with header and footer
    uint32_t required_size = size + sizeof(block_header_t) + sizeof(block_footer_t);
    
    // Only split if the remaining size is enough for a new block
    uint32_t min_split_size = 32 + sizeof(block_header_t) + sizeof(block_footer_t);
    
    if (block->size - required_size >= min_split_size) {
        // Calculate new block position
        uint8_t* block_start = (uint8_t*)block;
        uint8_t* new_block_start = block_start + required_size;
        
        // Create new block header
        block_header_t* new_block = (block_header_t*)new_block_start;
        new_block->magic = HEAP_MAGIC;
        new_block->size = block->size - required_size;
        new_block->status = HEAP_BLOCK_FREE;
        new_block->prev = block;
        new_block->next = block->next;
        new_block->next_free = NULL;
        new_block->prev_free = NULL;
        new_block->canary = HEAP_CANARY_VALUE;
        
        // Update new block footer
        block_footer_t* new_footer = HEADER_TO_FOOTER(new_block);
        new_footer->magic = HEAP_MAGIC;
        new_footer->header_ptr = (uint32_t)new_block;
        new_footer->size = new_block->size;
        new_footer->canary = HEAP_CANARY_VALUE;
        
        // Update original block
        block->size = required_size;
        block->next = new_block;
        
        // Update next block's prev pointer if it exists
        if (new_block->next) {
            new_block->next->prev = new_block;
        }
        
        // Update original block's footer
        block_footer_t* footer = HEADER_TO_FOOTER(block);
        footer->magic = HEAP_MAGIC;
        footer->header_ptr = (uint32_t)block;
        footer->size = block->size;
        footer->canary = HEAP_CANARY_VALUE;
        
        // Add the new block to free list
        add_to_free_list(heap, new_block);
        
        // Update heap stats
        heap_stats.free_bytes -= required_size;
        
        return new_block;
    }
    
    return NULL;
}

// Try to merge adjacent free blocks
static block_header_t* merge_blocks(heap_t* heap, block_header_t* block) {
    if (!block || block->status != HEAP_BLOCK_FREE) {
        return block;
    }
    
    // Try to merge with next block
    if (block->next && block->next->status == HEAP_BLOCK_FREE) {
        block_header_t* next_block = block->next;
        
        // Validate the next block before merging
        if (!validate_block(next_block)) {
            log_error("Heap: Failed to merge with corrupted next block at %p", next_block);
            return block;
        }
        
        // Remove next block from free list
        remove_from_free_list(heap, next_block);
        
        // Combine sizes
        block->size += next_block->size;
        
        // Update next pointer
        block->next = next_block->next;
        
        // Update next block's prev pointer if it exists
        if (next_block->next) {
            next_block->next->prev = block;
        }
        
        // Update footer
        block_footer_t* footer = HEADER_TO_FOOTER(block);
        footer->magic = HEAP_MAGIC;
        footer->header_ptr = (uint32_t)block;
        footer->size = block->size;
        footer->canary = HEAP_CANARY_VALUE;
        
        // Mark merged
        heap_stats.fragmentation_count--;
    }
    
    // Try to merge with previous block
    if (block->prev && block->prev->status == HEAP_BLOCK_FREE) {
        block_header_t* prev_block = block->prev;
        
        // Validate the previous block before merging
        if (!validate_block(prev_block)) {
            log_error("Heap: Failed to merge with corrupted previous block at %p", prev_block);
            return block;
        }
        
        // Remove both blocks from free lists
        remove_from_free_list(heap, prev_block);
        remove_from_free_list(heap, block);
        
        // Combine sizes
        prev_block->size += block->size;
        
        // Update next pointer
        prev_block->next = block->next;
        
        // Update next block's prev pointer if it exists
        if (block->next) {
            block->next->prev = prev_block;
        }
        
        // Update footer
        block_footer_t* footer = HEADER_TO_FOOTER(prev_block);
        footer->magic = HEAP_MAGIC;
        footer->header_ptr = (uint32_t)prev_block;
        footer->size = prev_block->size;
        footer->canary = HEAP_CANARY_VALUE;
        
        // Add merged block to free list
        add_to_free_list(heap, prev_block);
        
        // Return the previous block as it's now the merged block
        block = prev_block;
        
        // Mark merged
        heap_stats.fragmentation_count--;
    }
    
    return block;
}

// Extend heap size if possible
static void* heap_extend(heap_t* heap, uint32_t min_size) {
    // Calculate how much we need to expand
    uint32_t extension_size = min_size;
    
    // Round up to a multiple of page size
    if (extension_size % PAGE_SIZE != 0) {
        extension_size = ((extension_size / PAGE_SIZE) + 1) * PAGE_SIZE;
    }
    
    // Request more memory from the page allocator
    void* new_area = allocate_pages(extension_size / PAGE_SIZE);
    if (!new_area) {
        log_error("Heap: Failed to allocate %u bytes to extend heap", extension_size);
        return NULL;
    }
    
    // Create a new free block
    block_header_t* new_block = (block_header_t*)new_area;
    new_block->magic = HEAP_MAGIC;
    new_block->size = extension_size;
    new_block->status = HEAP_BLOCK_FREE;
    new_block->prev = NULL;  // Will update below
    new_block->next = NULL;
    new_block->prev_free = NULL;
    new_block->next_free = NULL;
    new_block->canary = HEAP_CANARY_VALUE;
    
    // Create footer
    block_footer_t* footer = HEADER_TO_FOOTER(new_block);
    footer->magic = HEAP_MAGIC;
    footer->header_ptr = (uint32_t)new_block;
    footer->size = extension_size;
    footer->canary = HEAP_CANARY_VALUE;
    
    // Find the last block in the heap
    block_header_t* last_block = (block_header_t*)heap->start;
    while (last_block->next) {
        last_block = last_block->next;
    }
    
    // Connect to existing heap
    last_block->next = new_block;
    new_block->prev = last_block;
    
    // Add to free list
    add_to_free_list(heap, new_block);
    
    // Update heap info
    heap->end = (void*)((uintptr_t)new_area + extension_size);
    heap->current_size += extension_size;
    heap_stats.total_size += extension_size;
    heap_stats.free_bytes += extension_size;
    
    log_info("Heap: Extended by %u bytes, new size: %u bytes", 
             extension_size, heap->current_size);
             
    // If the previous block is free, merge them
    if (last_block->status == HEAP_BLOCK_FREE) {
        new_block = merge_blocks(heap, last_block);
    }
    
    return new_block;
}

// Heap allocation function
void* heap_alloc(heap_t* heap, uint32_t size) {
    // Minimum allocation size
    if (size < 16) {
        size = 16;
    }
    
    // Align size
    if (size % HEAP_ALIGN != 0) {
        size = (size + HEAP_ALIGN) & ~(HEAP_ALIGN - 1);
    }
    
    // For large allocations, consider using guard pages
    if (USE_GUARD_PAGES && size >= PAGE_SIZE) {
        // Allocate directly from the page allocator with guard pages
        uint32_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE + 2; // +2 for guard pages
        void* guarded_alloc = allocate_pages(pages_needed);
        if (!guarded_alloc) {
            log_error("Heap: Failed to allocate %u pages for large allocation", pages_needed);
            return NULL;
        }
        
        // Set up guard pages
        void* user_area = (void*)((uintptr_t)guarded_alloc + PAGE_SIZE);
        paging_update_flags(guarded_alloc, PAGE_FLAG_PRESENT); // Mark as read-only
        paging_update_flags((void*)((uintptr_t)user_area + size), PAGE_FLAG_PRESENT); // Mark as read-only
        
        // Store size information for later freeing
        *(uint32_t*)user_area = pages_needed;
        
        // Update stats
        heap_stats.total_allocations++;
        heap_stats.current_allocations++;
        heap_stats.allocated_bytes += size;
        
        if (size > heap_stats.largest_allocation) {
            heap_stats.largest_allocation = size;
        }
        
        if (heap_stats.smallest_allocation == 0 || size < heap_stats.smallest_allocation) {
            heap_stats.smallest_allocation = size;
        }
        
        // Return area after guard page and size info
        return (void*)((uintptr_t)user_area + sizeof(uint32_t));
    }
    
    // Find a suitable free block
    block_header_t* block = find_free_block(heap, size);
    
    // If no suitable block found, extend the heap
    if (!block) {
        uint32_t extend_size = size + sizeof(block_header_t) + sizeof(block_footer_t);
        block = heap_extend(heap, extend_size);
        if (!block) {
            log_error("Heap: Out of memory - failed to allocate %u bytes", size);
            return NULL;
        }
    }
    
    // Remove block from free list
    remove_from_free_list(heap, block);
    
    // Split block if it's much larger than needed
    split_block(heap, block, size);
    
    // Mark block as used
    block->status = HEAP_BLOCK_USED;
    
    // Update heap stats
    heap_stats.total_allocations++;
    heap_stats.current_allocations++;
    heap_stats.allocated_bytes += block->size - sizeof(block_header_t) - sizeof(block_footer_t);
    heap_stats.free_bytes -= block->size;
    
    if (size > heap_stats.largest_allocation) {
        heap_stats.largest_allocation = size;
    }
    
    if (heap_stats.smallest_allocation == 0 || size < heap_stats.smallest_allocation) {
        heap_stats.smallest_allocation = size;
    }
    
    // Clear memory before returning (security)
    void* data = HEADER_TO_DATA(block);
    memset(data, 0, size);
    
    return data;
}

// Free allocated memory
void heap_free(heap_t* heap, void* ptr) {
    if (!ptr) {
        return;
    }
    
    // Check if this is a large guarded allocation
    if (USE_GUARD_PAGES) {
        // Get page aligned address
        void* page_addr = (void*)((uintptr_t)ptr & ~(PAGE_SIZE - 1));
        uint32_t* size_ptr = (uint32_t*)page_addr;
        
        // Check if it looks like a guarded allocation
        if ((uintptr_t)ptr - (uintptr_t)page_addr >= sizeof(uint32_t) &&
            (uintptr_t)ptr - (uintptr_t)page_addr <= 64) {
            
            // Free the entire range including guard pages
            free_pages((void*)((uintptr_t)page_addr - PAGE_SIZE), *size_ptr);
            
            // Update stats
            heap_stats.total_frees++;
            heap_stats.current_allocations--;
            
            return;
        }
    }
    
    // Get block header
    block_header_t* block = DATA_TO_HEADER(ptr);
    
    // Validate block
    if (!validate_block(block)) {
        log_error("Heap: Attempted to free invalid/corrupted block at %p", block);
        heap->error_count++;
        return;
    }
    
    // Check if block is already free
    if (block->status == HEAP_BLOCK_FREE) {
        log_error("Heap: Double free detected at %p", block);
        heap->error_count++;
        return;
    }
    
    // Mark block as free
    block->status = HEAP_BLOCK_FREE;
    
    // Update stats
    heap_stats.total_frees++;
    heap_stats.current_allocations--;
    heap_stats.free_bytes += block->size;
    heap_stats.allocated_bytes -= block->size - sizeof(block_header_t) - sizeof(block_footer_t);
    
    // Fill with poison value
    if (POISON_FREED) {
        memset(HEADER_TO_DATA(block), POISON_PATTERN, 
               block->size - sizeof(block_header_t) - sizeof(block_footer_t));
    }
    
    // Try to merge with adjacent free blocks
    block = merge_blocks(heap, block);
    
    // Add to free list
    add_to_free_list(heap, block);
}

// Reallocate memory with new size
void* heap_realloc(heap_t* heap, void* ptr, uint32_t size) {
    if (!ptr) {
        // If ptr is NULL, equivalent to heap_alloc
        return heap_alloc(heap, size);
    }
    
    if (size == 0) {
        // If size is 0, equivalent to heap_free
        heap_free(heap, ptr);
        return NULL;
    }
    
    // Align size
    if (size % HEAP_ALIGN != 0) {
        size = (size + HEAP_ALIGN) & ~(HEAP_ALIGN - 1);
    }
    
    // Check if this is a large guarded allocation
    if (USE_GUARD_PAGES) {
        // Get page aligned address
        void* page_addr = (void*)((uintptr_t)ptr & ~(PAGE_SIZE - 1));
        uint32_t* size_ptr = (uint32_t*)page_addr;
        
        // Check if it looks like a guarded allocation
        if ((uintptr_t)ptr - (uintptr_t)page_addr >= sizeof(uint32_t) &&
            (uintptr_t)ptr - (uintptr_t)page_addr <= 64) {
            
            // Calculate current size (excluding guard pages)
            uint32_t current_size = (*size_ptr - 2) * PAGE_SIZE;
            
            // If new size fits in current allocation, just return the same pointer
            if (size <= current_size) {
                return ptr;
            }
            
            // Otherwise, allocate new space and copy
            void* new_ptr = heap_alloc(heap, size);
            if (!new_ptr) {
                return NULL;
            }
            
            memcpy(new_ptr, ptr, current_size);
            heap_free(heap, ptr);
            return new_ptr;
        }
    }
    
    // Get block header
    block_header_t* block = DATA_TO_HEADER(ptr);
    
    // Validate block
    if (!validate_block(block)) {
        log_error("Heap: Attempted to realloc invalid/corrupted block at %p", block);
        heap->error_count++;
        return NULL;
    }
    
    // Calculate current usable size
    uint32_t current_size = block->size - sizeof(block_header_t) - sizeof(block_footer_t);
    
    // If new size is smaller, we can split the block
    if (size <= current_size) {
        // Only split if the difference is significant
        if (current_size - size >= 64) {
            split_block(heap, block, size);
        }
        return ptr;
    }
    
    // Check if next block is free and has enough space
    if (block->next && block->next->status == HEAP_BLOCK_FREE) {
        uint32_t combined_size = block->size + block->next->size;
        uint32_t combined_usable = combined_size - sizeof(block_header_t) - sizeof(block_footer_t);
        
        if (combined_usable >= size) {
            // Remove next block from free list
            remove_from_free_list(heap, block->next);
            
            // Combine the blocks
            block->size = combined_size;
            block->next = block->next->next;
            
            if (block->next) {
                block->next->prev = block;
            }
            
            // Update footer
            block_footer_t* footer = HEADER_TO_FOOTER(block);
            footer->magic = HEAP_MAGIC;
            footer->header_ptr = (uint32_t)block;
            footer->size = block->size;
            footer->canary = HEAP_CANARY_VALUE;
            
            // Split if there's still excess space
            if (combined_usable - size >= 64) {
                split_block(heap, block, size);
            }
            
            return ptr;
        }
    }
    
    // Need to allocate new space and copy
    void* new_ptr = heap_alloc(heap, size);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy old data
    memcpy(new_ptr, ptr, current_size);
    
    // Free old block
    heap_free(heap, ptr);
    
    return new_ptr;
}

// Get usable size from a pointer
uint32_t heap_get_size(void* ptr) {
    if (!ptr) {
        return 0;
    }
    
    // Check if this is a large guarded allocation
    if (USE_GUARD_PAGES) {
        // Get page aligned address
        void* page_addr = (void*)((uintptr_t)ptr & ~(PAGE_SIZE - 1));
        uint32_t* size_ptr = (uint32_t*)page_addr;
        
        // Check if it looks like a guarded allocation
        if ((uintptr_t)ptr - (uintptr_t)page_addr >= sizeof(uint32_t) &&
            (uintptr_t)ptr - (uintptr_t)page_addr <= 64) {
            
            // Calculate usable size (excluding guard pages and size header)
            return (*size_ptr - 2) * PAGE_SIZE - sizeof(uint32_t);
        }
    }
    
    block_header_t* block = DATA_TO_HEADER(ptr);
    
    // Validate block
    if (block->magic != HEAP_MAGIC || block->canary != HEAP_CANARY_VALUE) {
        return 0; // Invalid block
    }
    
    return block->size - sizeof(block_header_t) - sizeof(block_footer_t);
}

// Check the heap for corruption
int heap_check_integrity(heap_t* heap) {
    int errors = 0;
    
    // Check every block in the heap
    block_header_t* current = (block_header_t*)heap->start;
    
    while (current) {
        if (!validate_block(current)) {
            errors++;
        }
        
        current = current->next;
    }
    
    // Check free lists for consistency
    for (int i = 0; i < HEAP_MAX_BINS; i++) {
        block_header_t* current = heap->bins[i];
        
        while (current) {
            if (current->status != HEAP_BLOCK_FREE) {
                log_error("Heap: Block at %p in free list is not marked as free", current);
                errors++;
            }
            
            current = current->next_free;
        }
    }
    
    // Update heap error count
    heap->error_count += errors;
    
    return (errors == 0);
}

// Dump heap statistics
void heap_dump_stats(heap_t* heap) {
    log_info("Heap Statistics:");
    log_info("  Total Size: %u bytes", heap_stats.total_size);
    log_info("  Free Space: %u bytes (%u%%)", 
             heap_stats.free_bytes, 
             heap_stats.free_bytes * 100 / heap_stats.total_size);
    log_info("  Allocations: %u total, %u current", 
             heap_stats.total_allocations, 
             heap_stats.current_allocations);
    log_info("  Fragmentation Count: %u", heap_stats.fragmentation_count);
    log_info("  Largest Allocation: %u bytes", heap_stats.largest_allocation);
    log_info("  Smallest Allocation: %u bytes", heap_stats.smallest_allocation);
    log_info("  Errors Detected: %u", heap_stats.error_count + heap->error_count);
}

// Get heap stats
void heap_get_stats(heap_stats_t* stats) {
    if (stats) {
        memcpy(stats, &heap_stats, sizeof(heap_stats_t));
    }
}

// Reset the heap statistics
void heap_reset_stats() {
    // Keep total_size and free_bytes as they are
    uint32_t total = heap_stats.total_size;
    uint32_t free_bytes = heap_stats.free_bytes;
    
    memset(&heap_stats, 0, sizeof(heap_stats_t));
    
    heap_stats.total_size = total;
    heap_stats.free_bytes = free_bytes;
}