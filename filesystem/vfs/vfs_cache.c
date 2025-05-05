#include "vfs.h"
#include "../../kernel/logging/log.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define CACHE_MAGIC      0xCAC4E000
#define CACHE_HASH_SIZE  256

typedef struct vfs_cache_block_s vfs_cache_block_t;

// Global cache
static vfs_cache_t* global_cache = NULL;

// Hash table for quick block lookup
static vfs_cache_block_t* cache_hash_table[CACHE_HASH_SIZE];

// LRU list tracking
static vfs_cache_block_t* lru_head = NULL;
static vfs_cache_block_t* lru_tail = NULL;

// Cache statistics
static uint32_t cache_lookups = 0;
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;
static uint32_t cache_evictions = 0;
static uint32_t cache_writebacks = 0;

// Function prototypes
static uint32_t cache_hash(uint32_t dev_id, uint32_t block_id);
static vfs_cache_block_t* cache_lookup(uint32_t dev_id, uint32_t block_id);
static vfs_cache_block_t* cache_alloc_block(void);
static void cache_mark_accessed(vfs_cache_block_t* block);
static int cache_evict_block(void);
static int cache_writeback_block(vfs_cache_block_t* block);

/**
 * Initialize the VFS cache system
 *
 * @param block_size Size of each cache block
 * @param num_blocks Number of blocks in cache
 * @param flags Cache flags
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_init(uint32_t block_size, uint32_t num_blocks, uint8_t flags) {
    // Don't re-initialize if already exists
    if (global_cache) {
        log_warning("VFS: Cache already initialized");
        return VFS_SUCCESS;
    }
    
    // Limit the number of blocks to a reasonable amount
    if (num_blocks > VFS_MAX_CACHE_BLOCKS) {
        num_blocks = VFS_MAX_CACHE_BLOCKS;
    }
    
    // Make sure block size is reasonable
    if (block_size < 512) {
        block_size = 512;
    }
    
    // Align block size to 512 bytes
    if (block_size & 511) {
        block_size = (block_size + 512) & ~511;
    }
    
    // Allocate the cache structure
    global_cache = (vfs_cache_t*)malloc(sizeof(vfs_cache_t));
    if (!global_cache) {
        log_error("VFS: Failed to allocate cache structure");
        return VFS_ERR_NO_SPACE;
    }
    
    // Initialize the cache structure
    memset(global_cache, 0, sizeof(vfs_cache_t));
    memset(cache_hash_table, 0, sizeof(cache_hash_table));
    
    // Set cache parameters
    global_cache->block_size = block_size;
    global_cache->num_blocks = num_blocks;
    global_cache->flags = flags;
    global_cache->enabled = 1;
    
    // Allocate each block
    for (uint32_t i = 0; i < num_blocks; i++) {
        // Allocate block structure
        vfs_cache_block_t* block = (vfs_cache_block_t*)malloc(sizeof(vfs_cache_block_t));
        if (!block) {
            log_error("VFS: Failed to allocate cache block %u", i);
            // Clean up already allocated blocks
            for (uint32_t j = 0; j < i; j++) {
                if (global_cache->blocks[j]) {
                    free(global_cache->blocks[j]->data);
                    free(global_cache->blocks[j]);
                }
            }
            free(global_cache);
            global_cache = NULL;
            return VFS_ERR_NO_SPACE;
        }
        
        // Initialize block structure
        memset(block, 0, sizeof(vfs_cache_block_t));
        
        // Allocate block data
        block->data = (uint8_t*)malloc(block_size);
        if (!block->data) {
            log_error("VFS: Failed to allocate cache block data %u", i);
            // Clean up
            free(block);
            for (uint32_t j = 0; j < i; j++) {
                if (global_cache->blocks[j]) {
                    free(global_cache->blocks[j]->data);
                    free(global_cache->blocks[j]);
                }
            }
            free(global_cache);
            global_cache = NULL;
            return VFS_ERR_NO_SPACE;
        }
        
        // Set block parameters
        block->size = block_size;
        
        // Add to cache
        global_cache->blocks[i] = block;
        
        // Add to LRU list (front)
        if (!lru_head) {
            lru_head = lru_tail = block;
        } else {
            block->next = lru_head;
            lru_head = block;
        }
    }
    
    // Reset statistics
    cache_lookups = cache_hits = cache_misses = cache_evictions = cache_writebacks = 0;
    global_cache->hits = global_cache->misses = 0;
    
    log_info("VFS: Cache initialized with %u blocks of %u bytes (%u KB total)",
            num_blocks, block_size, (num_blocks * block_size) / 1024);
    
    return VFS_SUCCESS;
}

/**
 * Control caching for a mount point
 *
 * @param mount_point Path to mount point
 * @param enable Whether to enable caching
 * @param flags Cache flags
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_control(const char* mount_point, uint8_t enable, uint8_t flags) {
    // Check if cache is initialized
    if (!global_cache) {
        log_warning("VFS: Cache not initialized");
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Find mount point
    vfs_mount_t* mount = NULL;
    // This would access mount point table in the full VFS implementation
    
    if (!mount) {
        log_error("VFS: Mount point %s not found", mount_point);
        return VFS_ERR_NOT_FOUND;
    }
    
    // If we're disabling caching, flush dirty blocks
    if (!enable && mount->cache && mount->cache->enabled) {
        // Flush all dirty blocks for this mount point
        for (uint32_t i = 0; i < global_cache->num_blocks; i++) {
            vfs_cache_block_t* block = global_cache->blocks[i];
            if (block && block->dirty && block->dev_id == (uint32_t)mount->device) {
                cache_writeback_block(block);
            }
        }
    }
    
    // Allocate cache if needed
    if (!mount->cache) {
        mount->cache = (vfs_cache_t*)malloc(sizeof(vfs_cache_t));
        if (!mount->cache) {
            log_error("VFS: Failed to allocate cache for mount point %s", mount_point);
            return VFS_ERR_NO_SPACE;
        }
        
        // Copy from global cache settings
        mount->cache->block_size = global_cache->block_size;
        mount->cache->num_blocks = 0; // Uses global cache blocks
        mount->cache->enabled = 0;
        mount->cache->hits = mount->cache->misses = 0;
    }
    
    // Update cache settings
    mount->cache->enabled = enable;
    mount->cache->flags = flags;
    
    log_info("VFS: %s caching for %s (flags=0x%x)", 
            enable ? "Enabled" : "Disabled", mount_point, flags);
    
    return VFS_SUCCESS;
}

/**
 * Read a block from cache
 *
 * @param dev_id Device identifier
 * @param block_id Block identifier
 * @param buffer Output buffer (must be at least block_size bytes)
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_read_block(uint32_t dev_id, uint32_t block_id, void* buffer) {
    // Check if cache is initialized and enabled
    if (!global_cache || !global_cache->enabled) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Try to find block in cache
    cache_lookups++;
    vfs_cache_block_t* block = cache_lookup(dev_id, block_id);
    
    if (block) {
        // Cache hit
        cache_hits++;
        global_cache->hits++;
        
        // Update access statistics
        block->access_count++;
        cache_mark_accessed(block);
        
        // Copy data to buffer
        memcpy(buffer, block->data, block->size);
        
        return VFS_SUCCESS;
    }
    
    // Cache miss
    cache_misses++;
    global_cache->misses++;
    
    // Allocate a new cache block
    block = cache_alloc_block();
    if (!block) {
        // Failed to allocate, try to evict an existing block
        if (cache_evict_block() != VFS_SUCCESS) {
            log_error("VFS: Failed to evict cache block");
            return VFS_ERR_NO_SPACE;
        }
        
        // Try again
        block = cache_alloc_block();
        if (!block) {
            log_error("VFS: Failed to allocate cache block after eviction");
            return VFS_ERR_NO_SPACE;
        }
    }
    
    // Set block information
    block->dev_id = dev_id;
    block->block_id = block_id;
    block->dirty = 0;
    block->access_count = 1;
    
    // This is where we'd read from the actual device
    // For now, just zero the buffer as a placeholder
    memset(block->data, 0, block->size);
    
    // Add to hash table
    uint32_t hash = cache_hash(dev_id, block_id);
    block->next = cache_hash_table[hash];
    cache_hash_table[hash] = block;
    
    // Add to LRU list
    cache_mark_accessed(block);
    
    // Copy data to buffer
    memcpy(buffer, block->data, block->size);
    
    return VFS_SUCCESS;
}

/**
 * Write a block to cache
 *
 * @param dev_id Device identifier
 * @param block_id Block identifier
 * @param buffer Input buffer (must be at least block_size bytes)
 * @param sync Whether to sync immediately to disk
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_write_block(uint32_t dev_id, uint32_t block_id, const void* buffer, int sync) {
    // Check if cache is initialized and enabled
    if (!global_cache || !global_cache->enabled) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Try to find block in cache
    vfs_cache_block_t* block = cache_lookup(dev_id, block_id);
    
    if (!block) {
        // Block not in cache, allocate a new one
        block = cache_alloc_block();
        if (!block) {
            // Failed to allocate, try to evict an existing block
            if (cache_evict_block() != VFS_SUCCESS) {
                log_error("VFS: Failed to evict cache block");
                return VFS_ERR_NO_SPACE;
            }
            
            // Try again
            block = cache_alloc_block();
            if (!block) {
                log_error("VFS: Failed to allocate cache block after eviction");
                return VFS_ERR_NO_SPACE;
            }
        }
        
        // Set block information
        block->dev_id = dev_id;
        block->block_id = block_id;
        block->access_count = 1;
        
        // Add to hash table
        uint32_t hash = cache_hash(dev_id, block_id);
        block->next = cache_hash_table[hash];
        cache_hash_table[hash] = block;
    }
    
    // Update access statistics
    block->access_count++;
    cache_mark_accessed(block);
    
    // Copy data from buffer
    memcpy(block->data, buffer, block->size);
    
    // Mark as dirty
    block->dirty = 1;
    
    // If sync requested, write back to disk immediately
    if (sync) {
        return cache_writeback_block(block);
    }
    
    return VFS_SUCCESS;
}

/**
 * Flush a specific block from cache
 *
 * @param dev_id Device identifier
 * @param block_id Block identifier
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_flush_block(uint32_t dev_id, uint32_t block_id) {
    // Check if cache is initialized
    if (!global_cache) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Try to find block in cache
    vfs_cache_block_t* block = cache_lookup(dev_id, block_id);
    
    if (!block) {
        // Block not in cache, nothing to do
        return VFS_SUCCESS;
    }
    
    // If dirty, write back
    if (block->dirty) {
        return cache_writeback_block(block);
    }
    
    return VFS_SUCCESS;
}

/**
 * Invalidate a specific block in cache
 *
 * @param dev_id Device identifier
 * @param block_id Block identifier
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_invalidate_block(uint32_t dev_id, uint32_t block_id) {
    // Check if cache is initialized
    if (!global_cache) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Try to find block in cache
    uint32_t hash = cache_hash(dev_id, block_id);
    vfs_cache_block_t** pprev = &cache_hash_table[hash];
    vfs_cache_block_t* block = cache_hash_table[hash];
    
    while (block) {
        if (block->dev_id == dev_id && block->block_id == block_id) {
            // Found it, remove from hash table
            *pprev = block->next;
            
            // If dirty, write back
            if (block->dirty) {
                cache_writeback_block(block);
            }
            
            // Reset block information
            block->dev_id = 0;
            block->block_id = 0;
            block->dirty = 0;
            block->next = NULL;
            
            return VFS_SUCCESS;
        }
        
        pprev = &block->next;
        block = block->next;
    }
    
    // Block not in cache
    return VFS_SUCCESS;
}

/**
 * Flush all dirty blocks in cache
 *
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_flush_all(void) {
    // Check if cache is initialized
    if (!global_cache) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    int result = VFS_SUCCESS;
    
    // Go through all cache blocks
    for (uint32_t i = 0; i < global_cache->num_blocks; i++) {
        vfs_cache_block_t* block = global_cache->blocks[i];
        if (block && block->dirty) {
            int ret = cache_writeback_block(block);
            if (ret != VFS_SUCCESS) {
                result = ret;
            }
        }
    }
    
    return result;
}

/**
 * Invalidate all blocks for a specific device
 *
 * @param dev_id Device identifier
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_invalidate_device(uint32_t dev_id) {
    // Check if cache is initialized
    if (!global_cache) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Go through all hash table entries
    for (uint32_t hash = 0; hash < CACHE_HASH_SIZE; hash++) {
        vfs_cache_block_t** pprev = &cache_hash_table[hash];
        vfs_cache_block_t* block = cache_hash_table[hash];
        
        while (block) {
            if (block->dev_id == dev_id) {
                // Found a block for this device
                vfs_cache_block_t* next = block->next;
                
                // Remove from hash table
                *pprev = next;
                
                // If dirty, write back
                if (block->dirty) {
                    cache_writeback_block(block);
                }
                
                // Reset block information
                block->dev_id = 0;
                block->block_id = 0;
                block->dirty = 0;
                block->next = NULL;
                
                // Move to next
                block = next;
            } else {
                // Move to next
                pprev = &block->next;
                block = block->next;
            }
        }
    }
    
    return VFS_SUCCESS;
}

/**
 * Get cache statistics
 *
 * @param hits Output variable for cache hits
 * @param misses Output variable for cache misses
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_get_stats(uint32_t* hits, uint32_t* misses) {
    if (!global_cache) {
        if (hits) *hits = 0;
        if (misses) *misses = 0;
        return VFS_ERR_UNSUPPORTED;
    }
    
    if (hits) *hits = cache_hits;
    if (misses) *misses = cache_misses;
    
    return VFS_SUCCESS;
}

/**
 * Get hit ratio as a percentage
 *
 * @return Hit ratio (0-100), or negative error code on failure
 */
int vfs_cache_get_hit_ratio(void) {
    if (!global_cache || cache_lookups == 0) {
        return 0;
    }
    
    return (cache_hits * 100) / cache_lookups;
}

/**
 * Calculate hash for a block
 *
 * @param dev_id Device identifier
 * @param block_id Block identifier
 * @return Hash value
 */
static uint32_t cache_hash(uint32_t dev_id, uint32_t block_id) {
    // Simple hash function combining device and block IDs
    return ((dev_id << 16) ^ block_id) % CACHE_HASH_SIZE;
}

/**
 * Look up a block in the cache
 *
 * @param dev_id Device identifier
 * @param block_id Block identifier
 * @return Pointer to cache block, or NULL if not found
 */
static vfs_cache_block_t* cache_lookup(uint32_t dev_id, uint32_t block_id) {
    // Calculate hash
    uint32_t hash = cache_hash(dev_id, block_id);
    
    // Look in hash chain
    vfs_cache_block_t* block = cache_hash_table[hash];
    while (block) {
        if (block->dev_id == dev_id && block->block_id == block_id) {
            // Found it
            return block;
        }
        block = block->next;
    }
    
    // Not found
    return NULL;
}

/**
 * Allocate a free cache block
 *
 * @return Pointer to a free cache block, or NULL if none available
 */
static vfs_cache_block_t* cache_alloc_block(void) {
    // Look for a free block
    for (uint32_t i = 0; i < global_cache->num_blocks; i++) {
        vfs_cache_block_t* block = global_cache->blocks[i];
        if (block && block->dev_id == 0 && block->block_id == 0) {
            // Found a free block
            return block;
        }
    }
    
    // No free blocks
    return NULL;
}

/**
 * Mark a block as recently accessed (moves to front of LRU list)
 *
 * @param block Cache block
 */
static void cache_mark_accessed(vfs_cache_block_t* block) {
    // Update access timestamp
    block->last_access = /* current time would be used here */0;
    
    // If this is already the head, nothing to do
    if (block == lru_head) {
        return;
    }
    
    // First, remove from current position
    if (block == lru_tail) {
        // It's the tail
        lru_tail = NULL; // We'll fix this below
    } else {
        // It's in the middle
        // Find the previous block
        vfs_cache_block_t* prev = lru_head;
        while (prev && prev->next != block) {
            prev = prev->next;
        }
        
        if (prev) {
            // Remove from list
            prev->next = block->next;
            
            // If this was the tail, update tail
            if (lru_tail == block) {
                lru_tail = prev;
            }
        }
    }
    
    // Now, add to head
    block->next = lru_head;
    lru_head = block;
    
    // If tail is NULL, this is the only element
    if (!lru_tail) {
        lru_tail = block;
    }
}

/**
 * Evict a block from cache based on LRU policy
 *
 * @return 0 on success, negative error code on failure
 */
static int cache_evict_block(void) {
    // No blocks to evict
    if (!lru_tail) {
        return VFS_ERR_NO_SPACE;
    }
    
    // Get the least recently used block (at tail)
    vfs_cache_block_t* block = lru_tail;
    
    // If dirty, write back
    if (block->dirty) {
        cache_writeback_block(block);
    }
    
    // Remove from hash table
    if (block->dev_id != 0 || block->block_id != 0) {
        uint32_t hash = cache_hash(block->dev_id, block->block_id);
        vfs_cache_block_t** pprev = &cache_hash_table[hash];
        vfs_cache_block_t* curr = cache_hash_table[hash];
        
        while (curr) {
            if (curr == block) {
                // Remove from hash chain
                *pprev = curr->next;
                break;
            }
            
            pprev = &curr->next;
            curr = curr->next;
        }
    }
    
    // Reset block information
    block->dev_id = 0;
    block->block_id = 0;
    block->dirty = 0;
    
    // Move up in LRU list (not to the front)
    if (lru_tail->next) {
        // Move one position up
        vfs_cache_block_t* prev = lru_head;
        while (prev && prev->next != lru_tail) {
            prev = prev->next;
        }
        
        if (prev) {
            // Update pointers
            lru_tail = prev;
        }
    }
    
    cache_evictions++;
    
    return VFS_SUCCESS;
}

/**
 * Write a dirty cache block back to disk
 *
 * @param block Cache block
 * @return 0 on success, negative error code on failure
 */
static int cache_writeback_block(vfs_cache_block_t* block) {
    if (!block || !block->dirty) {
        return VFS_SUCCESS;
    }
    
    // This is where we'd write to the actual device
    // For now, just mark as clean
    block->dirty = 0;
    
    cache_writebacks++;
    
    return VFS_SUCCESS;
}

/**
 * Shut down the cache system, flushing all dirty blocks
 *
 * @return 0 on success, negative error code on failure
 */
int vfs_cache_shutdown(void) {
    if (!global_cache) {
        return VFS_SUCCESS;
    }
    
    // Flush all dirty blocks
    vfs_cache_flush_all();
    
    // Free all blocks
    for (uint32_t i = 0; i < global_cache->num_blocks; i++) {
        if (global_cache->blocks[i]) {
            if (global_cache->blocks[i]->data) {
                free(global_cache->blocks[i]->data);
            }
            free(global_cache->blocks[i]);
        }
    }
    
    // Free cache structure
    free(global_cache);
    global_cache = NULL;
    
    // Reset LRU list
    lru_head = lru_tail = NULL;
    
    // Clear hash table
    memset(cache_hash_table, 0, sizeof(cache_hash_table));
    
    log_info("VFS: Cache shutdown (hits=%u, misses=%u, hit ratio=%u%%)", 
            cache_hits, cache_misses, 
            cache_lookups > 0 ? (cache_hits * 100) / cache_lookups : 0);
    
    return VFS_SUCCESS;
}