#include "heap.h"
#include "paging.h"

// Heap constants
#define HEAP_START 0x500000    // 5 MB mark
#define HEAP_SIZE  0x100000    // 1 MB heap size
#define BLOCK_MAGIC 0xDEADBEEF // Magic number to identify valid blocks

// Memory block header
typedef struct block_header {
    uint32_t magic;               // Magic number for validation
    size_t size;                  // Size of the block (excluding header)
    uint8_t is_free;              // 1 if free, 0 if allocated
    struct block_header *next;    // Pointer to the next block
    struct block_header *prev;    // Pointer to the previous block
} block_header_t;

// Heap statistics
static heap_stats_t heap_statistics = {
    .total_memory = 0,
    .used_memory = 0,
    .free_memory = 0,
    .allocation_count = 0
};

// Pointer to the first block in the heap
static block_header_t *heap_start = NULL;

// Initialize the heap
void heap_init(void) {
    // Set up the initial heap block
    heap_start = (block_header_t*)HEAP_START;
    heap_start->magic = BLOCK_MAGIC;
    heap_start->size = HEAP_SIZE - sizeof(block_header_t);
    heap_start->is_free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    // Update statistics
    heap_statistics.total_memory = HEAP_SIZE;
    heap_statistics.free_memory = HEAP_SIZE - sizeof(block_header_t);
    heap_statistics.used_memory = sizeof(block_header_t);
    heap_statistics.allocation_count = 0;
}

// Split a block if it's too large for the requested size
static void split_block(block_header_t *block, size_t size) {
    // Only split if the block is significantly larger than needed
    if (block->size > size + sizeof(block_header_t) + 32) { // 32 bytes minimum fragment size
        block_header_t *new_block = (block_header_t*)((uint8_t*)block + sizeof(block_header_t) + size);
        
        // Set up the new block
        new_block->magic = BLOCK_MAGIC;
        new_block->size = block->size - size - sizeof(block_header_t);
        new_block->is_free = 1;
        new_block->next = block->next;
        new_block->prev = block;
        
        // Update the original block
        block->size = size;
        block->next = new_block;
        
        // Update the next block's prev pointer
        if (new_block->next) {
            new_block->next->prev = new_block;
        }
        
        // Update statistics
        heap_statistics.free_memory -= sizeof(block_header_t);
        heap_statistics.used_memory += sizeof(block_header_t);
    }
}

// Find a suitable free block using first-fit strategy
static block_header_t *find_free_block(size_t size) {
    block_header_t *current = heap_start;
    
    while (current) {
        // Check if block is free and large enough
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    
    return NULL; // No suitable block found
}

// Merge adjacent free blocks
static void merge_free_blocks(void) {
    block_header_t *current = heap_start;
    
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            // Merge with next block
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
            
            // Update next block's prev pointer if it exists
            if (current->next) {
                current->next->prev = current;
            }
            
            // Update statistics
            heap_statistics.free_memory += sizeof(block_header_t);
            heap_statistics.used_memory -= sizeof(block_header_t);
            
            // Continue from the same block to check for more merges
            continue;
        }
        
        // Move to the next block
        current = current->next;
    }
}

// Allocate memory
void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    // Align size to 8 bytes for better memory access
    size = (size + 7) & ~7;
    
    // Find a free block
    block_header_t *block = find_free_block(size);
    if (!block) return NULL; // Out of memory
    
    // Split the block if it's much larger than needed
    split_block(block, size);
    
    // Mark the block as allocated
    block->is_free = 0;
    
    // Update statistics
    heap_statistics.free_memory -= block->size;
    heap_statistics.used_memory += block->size;
    heap_statistics.allocation_count++;
    
    // Return the memory address after the header
    return (void*)((uint8_t*)block + sizeof(block_header_t));
}

// Free allocated memory
void free(void *ptr) {
    if (!ptr) return;
    
    // Get the block header
    block_header_t *block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    
    // Validate the block
    if (block->magic != BLOCK_MAGIC) return;
    
    // Mark the block as free
    block->is_free = 1;
    
    // Update statistics
    heap_statistics.free_memory += block->size;
    heap_statistics.used_memory -= block->size;
    heap_statistics.allocation_count--;
    
    // Attempt to merge adjacent free blocks
    merge_free_blocks();
}

// Reallocate memory block
void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    // Get the block header
    block_header_t *block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    
    // Validate the block
    if (block->magic != BLOCK_MAGIC) return NULL;
    
    // Align size to 8 bytes
    size = (size + 7) & ~7;
    
    // If the requested size is smaller, we can just resize this block
    if (size <= block->size) {
        // Split the block if it's much larger than needed
        split_block(block, size);
        return ptr;
    }
    
    // If the next block is free and has enough space
    if (block->next && block->next->is_free && 
        (block->size + sizeof(block_header_t) + block->next->size) >= size) {
        
        // Merge with the next block
        block->size += sizeof(block_header_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
        
        // Update statistics
        heap_statistics.free_memory -= sizeof(block_header_t);
        heap_statistics.used_memory += sizeof(block_header_t);
        
        // Split if necessary
        split_block(block, size);
        
        return ptr;
    }
    
    // Need to allocate a new block
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    
    // Copy the data to the new block
    for (size_t i = 0; i < block->size; i++) {
        ((uint8_t*)new_ptr)[i] = ((uint8_t*)ptr)[i];
    }
    
    // Free the old block
    free(ptr);
    
    return new_ptr;
}

// Allocate and zero-initialize memory
void *calloc(size_t num, size_t size) {
    size_t total_size = num * size;
    void *ptr = malloc(total_size);
    
    if (ptr) {
        // Zero out the allocated memory
        for (size_t i = 0; i < total_size; i++) {
            ((uint8_t*)ptr)[i] = 0;
        }
    }
    
    return ptr;
}

// Get heap statistics
void heap_get_stats(heap_stats_t *stats) {
    if (stats) {
        *stats = heap_statistics;
    }
}