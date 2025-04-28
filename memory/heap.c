#include "heap.h"
#include "paging.h"

// Heap constants
#define HEAP_START 0x500000    // 5 MB mark
#define HEAP_SIZE  0x100000    // 1 MB heap size
#define BLOCK_MAGIC 0xDEADBEEF // Magic number to identify valid blocks
#define BLOCK_FOOTER_MAGIC 0xBEEFDEAD // Magic number for block footer

// Memory block header
typedef struct block_header {
    uint32_t magic;               // Magic number for validation
    size_t size;                  // Size of the block (excluding header)
    uint8_t is_free;              // 1 if free, 0 if allocated
    struct block_header *next;    // Pointer to the next block
    struct block_header *prev;    // Pointer to the previous block
} block_header_t;

// Memory block footer for additional validation
typedef struct block_footer {
    uint32_t magic;               // Magic number for validation
    size_t size;                  // Copy of block size for validation
    const block_header_t *header; // Pointer to header for cross-validation
    uint32_t checksum;           // XOR checksum for enhanced corruption detection
} block_footer_t;

// Heap statistics
static heap_stats_t heap_statistics = {
    .total_memory = 0,
    .used_memory = 0,
    .free_memory = 0,
    .allocation_count = 0,
    .peak_usage = 0,
    .failed_allocs = 0
};

// Flag to track if heap is already initialized
static int heap_initialized = 0;

// Pointer to the first block in the heap
static block_header_t *heap_start = NULL;

// Current heap end address
static uintptr_t heap_end = 0;

// Helper function to get the footer for a block
static block_footer_t *get_footer(block_header_t *header) {
    if (!header) return NULL;
    return (block_footer_t*)((uint8_t*)header + sizeof(block_header_t) + header->size);
}

// Helper function to set up a block footer
static void set_footer(block_header_t *header) {
    if (!header) return;
    block_footer_t *footer = get_footer(header);
    footer->magic = BLOCK_FOOTER_MAGIC;
    footer->size = header->size;
    footer->header = header;
    
    // Add XOR checksum for enhanced corruption detection
    footer->checksum = footer->magic ^ footer->size ^ (uintptr_t)footer->header;
}

// Expand the heap by requesting more pages from the paging system
void heap_expand(size_t additional_size) {
    // Round up to page boundary
    size_t pages_needed = (additional_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Request pages from the paging system
    void *new_memory = allocate_pages(pages_needed);
    if (!new_memory) return; // Failed to allocate pages
    
    size_t new_size = pages_needed * PAGE_SIZE;
    
    if (heap_end == 0) {
        // This is the initial allocation
        heap_start = (block_header_t*)new_memory;
        heap_start->magic = BLOCK_MAGIC;
        heap_start->size = new_size - sizeof(block_header_t) - sizeof(block_footer_t);
        heap_start->is_free = 1;
        heap_start->next = NULL;
        heap_start->prev = NULL;
        
        // Set up initial block footer
        set_footer(heap_start);
        
        // Update heap end pointer
        heap_end = (uintptr_t)new_memory + new_size;
        
        // Update statistics
        heap_statistics.total_memory = new_size;
        heap_statistics.free_memory = heap_start->size;
        heap_statistics.used_memory = sizeof(block_header_t) + sizeof(block_footer_t);
    } else {
        // This is an expansion - find the last block
        block_header_t *last_block = heap_start;
        while (last_block->next) {
            last_block = last_block->next;
        }
        
        // Check if the last block is free
        if (last_block->is_free) {
            // Extend the existing free block
            size_t old_size = last_block->size;
            last_block->size += new_size;
            
            // Update footer
            set_footer(last_block);
            
            // Update statistics
            heap_statistics.total_memory += new_size;
            heap_statistics.free_memory += new_size;
        } else {
            // Create a new free block at the end
            block_header_t *new_block = (block_header_t*)heap_end;
            new_block->magic = BLOCK_MAGIC;
            new_block->size = new_size - sizeof(block_header_t) - sizeof(block_footer_t);
            new_block->is_free = 1;
            new_block->prev = last_block;
            new_block->next = NULL;
            
            // Update last block's next pointer
            last_block->next = new_block;
            
            // Set up new block footer
            set_footer(new_block);
            
            // Update heap end pointer
            heap_end = heap_end + new_size;
            
            // Update statistics
            heap_statistics.total_memory += new_size;
            heap_statistics.free_memory += new_block->size;
            heap_statistics.used_memory += (sizeof(block_header_t) + sizeof(block_footer_t));
        }
    }
}

// Initialize the heap
void heap_init(void) {
    // Prevent double initialization
    if (heap_initialized) return;
    
    // Initialize with a default size (can be expanded later)
    heap_expand(HEAP_SIZE);
    
    // Mark heap as initialized
    heap_initialized = 1;
}

// Validate a block's integrity
static int validate_block(block_header_t *block) {
    if (!block) return 0;
    
    // Check header magic
    if (block->magic != BLOCK_MAGIC) return 0;
    
    // Check footer if present
    block_footer_t *footer = get_footer(block);
    if (!footer) return 0;
    
    // Validate footer
    if (footer->magic != BLOCK_FOOTER_MAGIC) return 0;
    if (footer->size != block->size) return 0;
    if (footer->header != block) return 0;
    
    // Validate checksum
    if (footer->checksum != (footer->magic ^ footer->size ^ (uintptr_t)footer->header)) return 0;
    
    return 1; // Block is valid
}

// Split a block if it's too large for the requested size
static void split_block(block_header_t *block, size_t size) {
    // Minimum size for a new block = header + footer + minimum payload
    size_t min_fragment = sizeof(block_header_t) + sizeof(block_footer_t) + 32;
    
    // Only split if the block is significantly larger than needed
    if (block->size > size + min_fragment) {
        // Calculate the remaining size
        size_t remaining_size = block->size - size - sizeof(block_header_t) - sizeof(block_footer_t);
        
        // For small memory blocks, consider fragmentation impact
        // Don't split if remaining space is less than twice the minimum fragment size
        // This helps reduce fragmentation with small memory blocks
        if (remaining_size < 2 * min_fragment) {
            // Just leave the block as is to avoid fragmentation
            return;
        }
        
        block_header_t *new_block = (block_header_t*)((uint8_t*)block + 
                                     sizeof(block_header_t) + size + sizeof(block_footer_t));
        
        // Set up the new block
        new_block->magic = BLOCK_MAGIC;
        new_block->size = remaining_size;
        new_block->is_free = 1;
        new_block->next = block->next;
        new_block->prev = block;
        
        // Set footer for the new block
        set_footer(new_block);
        
        // Update the original block
        block->size = size;
        block->next = new_block;
        
        // Update the footer for the original block
        set_footer(block);
        
        // Update the next block's prev pointer
        if (new_block->next) {
            new_block->next->prev = new_block;
        }
        
        // Update statistics
        heap_statistics.free_memory -= (sizeof(block_header_t) + sizeof(block_footer_t));
        heap_statistics.used_memory += (sizeof(block_header_t) + sizeof(block_footer_t));
    }
}

// Find a suitable free block using first-fit strategy
static block_header_t *find_free_block(size_t size) {
    block_header_t *current = heap_start;
    
    while (current) {
        // Validate the block before using it
        if (!validate_block(current)) {
            // Block corruption detected - could log or panic here
            return NULL;
        }
        
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
        // Validate blocks before merging
        if (!validate_block(current) || !validate_block(current->next)) {
            // Block corruption detected - could log or panic here
            return;
        }
        
        if (current->is_free && current->next->is_free) {
            // Calculate total size including the next block's header and footer
            size_t total_size = current->size + sizeof(block_header_t) + 
                                current->next->size + sizeof(block_footer_t);
            
            // Save reference to next block's next pointer
            block_header_t *next_next = current->next->next;
            
            // Update current block's size and next pointer
            current->size = total_size;
            current->next = next_next;
            
            // Update footer for the merged block
            set_footer(current);
            
            // Update next block's prev pointer if it exists
            if (next_next) {
                next_next->prev = current;
            }
            
            // Update statistics
            heap_statistics.free_memory += (sizeof(block_header_t) + sizeof(block_footer_t));
            heap_statistics.used_memory -= (sizeof(block_header_t) + sizeof(block_footer_t));
            
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
    if (!block) {
        // Try to expand the heap if no suitable block is found
        heap_expand(size + sizeof(block_header_t) + sizeof(block_footer_t));
        block = find_free_block(size);
        if (!block) {
            heap_statistics.failed_allocs++;
            return NULL; // Out of memory
        }
    }
    
    // Split the block if it's much larger than needed
    split_block(block, size);
    
    // Mark the block as allocated
    block->is_free = 0;
    
    // Update statistics
    heap_statistics.free_memory -= block->size;
    heap_statistics.used_memory += block->size;
    heap_statistics.allocation_count++;
    if (heap_statistics.used_memory > heap_statistics.peak_usage) {
        heap_statistics.peak_usage = heap_statistics.used_memory;
    }
    
    // Return the memory address after the header
    return (void*)((uint8_t*)block + sizeof(block_header_t));
}

// Free allocated memory
void free(void *ptr) {
    if (!ptr) return;
    
    // Get the block header
    block_header_t *block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    
    // Validate the block
    if (!validate_block(block)) return;
    
    // Check for double free
    if (block->is_free) return;
    
    // Mark the block as free
    block->is_free = 1;
    
    // Update statistics
    heap_statistics.free_memory += block->size;
    heap_statistics.used_memory -= block->size;
    heap_statistics.allocation_count--;
    
    // Attempt to merge adjacent free blocks
    merge_free_blocks();
}

// Memory copy helper function
static void mem_copy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void mem_set(void *dest, int val, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    while (n--) {
        *d++ = (uint8_t)val;
    }
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
    if (!validate_block(block)) return NULL;
    
    // Align size to 8 bytes
    size = (size + 7) & ~7;
    
    // If the requested size is smaller, we can just resize this block
    if (size <= block->size) {
        // Split the block if it's much larger than needed
        split_block(block, size);
        return ptr;
    }
    
    // Check if we can merge with the next block if it's free
    if (block->next && validate_block(block->next) && block->next->is_free && 
        (block->size + sizeof(block_header_t) + sizeof(block_footer_t) + block->next->size) >= size) {
        
        // Combine with the next block
        size_t new_size = block->size + sizeof(block_header_t) + sizeof(block_footer_t) + block->next->size;
        block->size = new_size;
        block->next = block->next->next;
        
        // Update the footer
        set_footer(block);
        
        // Update the next block's prev pointer if it exists
        if (block->next) {
            block->next->prev = block;
        }
        
        // Update statistics
        heap_statistics.free_memory -= (sizeof(block_header_t) + sizeof(block_footer_t));
        heap_statistics.used_memory += (sizeof(block_header_t) + sizeof(block_footer_t));
        
        // Split if necessary
        split_block(block, size);
        
        return ptr;
    }
    
    // Need to allocate a new block
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    
    // Copy the data to the new block using our helper
    mem_copy(new_ptr, ptr, block->size);
    
    // Free the old block
    free(ptr);
    
    return new_ptr;
}

// Allocate and zero-initialize memory
void *calloc(size_t num, size_t size) {
    // Check for overflow in multiplication
    if (num > 0 && size > SIZE_MAX / num) {
        return NULL; // Overflow
    }
    
    size_t total_size = num * size;
    void *ptr = malloc(total_size);
    
    if (ptr) {
        // Zero out the allocated memory using our helper
        mem_set(ptr, 0, total_size);
    }
    
    return ptr;
}

// Get heap statistics
void heap_get_stats(heap_stats_t *stats) {
    if (stats) {
        *stats = heap_statistics;
    }
}

// Check if a pointer is valid (allocated by our heap)
int is_valid_heap_pointer(void *ptr) {
    if (!ptr || !heap_start) return 0;
    
    // Check if the pointer is within the heap bounds
    if (ptr < (void*)heap_start || ptr >= (void*)heap_end) {
        return 0;
    }
    
    // Get the potential header
    block_header_t *block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    
    // Validate the block and check if it's allocated
    if (!validate_block(block) || block->is_free) {
        return 0;
    }
    
    return 1; // Valid allocated pointer
}

// Dump the current heap layout for debugging purposes
void heap_dump(void) {
    extern void shell_print(const char* str);
    extern void shell_println(const char* str);
    extern void int_to_string(int num, char* str);
    
    shell_println("=== Heap Memory Dump ===");
    
    if (!heap_initialized) {
        shell_println("Heap not initialized");
        return;
    }
    
    block_header_t *current = heap_start;
    int block_count = 0;
    char buffer[64];
    
    while (current) {
        // Print block info
        int_to_string(block_count++, buffer);
        shell_print("Block #");
        shell_print(buffer);
        shell_print(" @ 0x");
        
        // Convert address to hex string - simplified version
        uintptr_t addr = (uintptr_t)current;
        for (int i = 7; i >= 0; i--) {
            int digit = (addr >> (i * 4)) & 0xF;
            buffer[7-i] = digit < 10 ? '0' + digit : 'A' + (digit - 10);
        }
        buffer[8] = 0;
        shell_print(buffer);
        
        // Print block state
        shell_print(" | Size: ");
        int_to_string(current->size, buffer);
        shell_print(buffer);
        
        shell_print(" | ");
        shell_println(current->is_free ? "FREE" : "USED");
        
        // Validate current block
        if (!validate_block(current)) {
            shell_println("  WARNING: CORRUPTED BLOCK DETECTED!");
        }
        
        current = current->next;
    }
}

// Check heap integrity
void heap_check(void) {
    extern void shell_println(const char* str);
    extern void int_to_string(int num, char* str);
    
    shell_println("=== Heap Integrity Check ===");
    
    if (!heap_initialized) {
        shell_println("Heap not initialized");
        return;
    }
    
    block_header_t *current = heap_start;
    int errors = 0;
    char buffer[32];
    
    // Verify the entire heap
    while (current) {
        // Check block validity
        if (!validate_block(current)) {
            errors++;
        }
        
        // Check for adjacent free blocks that should have been merged
        if (current->is_free && current->next && current->next->is_free) {
            shell_println("Error: Adjacent free blocks detected (merge failure)");
            errors++;
        }
        
        // Check that block pointers are consistent
        if (current->next && current->next->prev != current) {
            shell_println("Error: Inconsistent prev/next pointers");
            errors++;
        }
        
        // Move to next block
        current = current->next;
    }
    
    // Verify heap stats against actual heap content
    size_t counted_used = 0;
    size_t counted_free = 0;
    size_t counted_overhead = 0;
    int counted_allocs = 0;
    
    current = heap_start;
    while (current) {
        if (current->is_free) {
            counted_free += current->size;
        } else {
            counted_used += current->size;
            counted_allocs++;
        }
        counted_overhead += (sizeof(block_header_t) + sizeof(block_footer_t));
        current = current->next;
    }
    
    // Check for inconsistencies in statistics
    if (counted_used != heap_statistics.used_memory - counted_overhead) {
        shell_println("Error: Used memory statistics are inconsistent");
        errors++;
    }
    
    if (counted_free != heap_statistics.free_memory) {
        shell_println("Error: Free memory statistics are inconsistent");
        errors++;
    }
    
    if (counted_allocs != heap_statistics.allocation_count) {
        shell_println("Error: Allocation count is inconsistent");
        errors++;
    }
    
    // Print result
    if (errors == 0) {
        shell_println("Heap integrity check passed. No errors detected.");
    } else {
        shell_print("Heap integrity check failed with ");
        int_to_string(errors, buffer);
        shell_print(buffer);
        shell_println(" errors.");
    }
}