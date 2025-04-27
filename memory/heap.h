#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

// Heap management functions
void heap_init(void);
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t num, size_t size);

// Memory validation function
int is_valid_heap_pointer(void *ptr);

// Memory statistics
typedef struct {
    size_t total_memory;
    size_t used_memory;
    size_t free_memory;
    size_t allocation_count;
} heap_stats_t;

void heap_get_stats(heap_stats_t *stats);

#endif // HEAP_H