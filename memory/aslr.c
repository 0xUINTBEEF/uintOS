/**
 * @file aslr.c
 * @brief Address Space Layout Randomization Implementation
 *
 * This file implements ASLR functionality to randomize the memory layout
 * of processes, making them more resistant to memory-based exploits.
 */

#include "aslr.h"
#include "../kernel/logging/log.h"
#include "../hal/include/hal_io.h"
#include "../kernel/sync.h"
#include <string.h>

#define ASLR_TAG "ASLR"

// Global ASLR configuration
static struct {
    int enabled;                     // Whether ASLR is enabled
    uint8_t entropy_bits;            // Number of bits of entropy
    uint32_t flags;                  // Which memory regions to randomize
    mutex_t lock;                    // Lock for thread safety
    uint32_t last_random_value;      // Last random value generated
    uint64_t random_seed;            // Seed for PRNG
} aslr_config;

// PAGE_SIZE is likely already defined elsewhere, but just in case
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

// Default base addresses for various memory regions (can be customized later)
#define DEFAULT_STACK_BASE    0xC0000000  // Top of user space
#define DEFAULT_HEAP_BASE     0x08000000  // After program code
#define DEFAULT_MMAP_BASE     0x40000000  // Middle of address space
#define DEFAULT_EXEC_BASE     0x00400000  // Standard executable base
#define DEFAULT_LIB_BASE      0x20000000  // Libraries region
#define DEFAULT_VDSO_BASE     0xF0000000  // VDSO page region

// Maximum allowed offsets for each region type (in pages)
#define MAX_STACK_DELTA       0x0FFF      // ~16MB of randomization
#define MAX_HEAP_DELTA        0x0FFF      // ~16MB of randomization
#define MAX_MMAP_DELTA        0xFFFF      // ~256MB of randomization
#define MAX_EXEC_DELTA        0x007F      // ~512KB of randomization (small to maintain alignment constraints)
#define MAX_LIB_DELTA         0x0FFF      // ~16MB of randomization
#define MAX_VDSO_DELTA        0x003F      // ~256KB of randomization

/**
 * Simple random number generator for ASLR
 * Uses a xorshift PRNG algorithm
 *
 * @return A pseudo-random 32-bit value
 */
static uint32_t aslr_generate_random(void) {
    // Use a mutex for thread safety
    mutex_lock(&aslr_config.lock);

    // Update the random seed using xorshift algorithm
    uint64_t x = aslr_config.random_seed;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    aslr_config.random_seed = x;
    
    // Use the high 32 bits for better randomness
    uint32_t result = (uint32_t)(x >> 32);
    
    // Store for potential debugging
    aslr_config.last_random_value = result;
    
    mutex_unlock(&aslr_config.lock);
    return result;
}

/**
 * Initialize the random seed from system sources
 * This should be called early during boot, collecting
 * entropy from various system sources.
 */
static void aslr_init_random_seed(void) {
    uint64_t seed = 0;
    
    // Try to get seed from hardware if available
    // On x86, we can use RDTSC (Read Time-Stamp Counter)
    uint32_t low, high;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    seed = ((uint64_t)high << 32) | low;
    
    // Mix in some additional entropy sources
    // - Current system uptime
    extern uint32_t get_system_uptime_ms(void);
    seed ^= (uint64_t)get_system_uptime_ms() << 13;
    
    // - Memory address of this function (will vary slightly between boots)
    seed ^= (uint64_t)(uintptr_t)&aslr_init_random_seed;
    
    // - Pointer to stack (will vary between boots if kernel ASLR is enabled)
    void* stack_ptr;
    asm volatile("movl %%esp, %0" : "=r" (stack_ptr));
    seed ^= (uint64_t)(uintptr_t)stack_ptr << 21;
    
    // - Last detected hardware interrupt time
    extern uint64_t get_last_interrupt_time(void);
    seed ^= get_last_interrupt_time();
    
    // For better entropy, we can also include other hardware-specific values:
    // - PCI device enumeration order
    // - Variations in memory timings
    // - Network device MAC addresses
    
    // For a real system, even more entropy would be collected from hardware
    // random number generators or timing jitter in I/O operations
    
    aslr_config.random_seed = seed;
    
    log_debug(ASLR_TAG, "Initialized random seed from system entropy sources");
}

/**
 * Initialize ASLR subsystem
 */
int aslr_init(int enabled, uint8_t entropy_bits, uint32_t flags) {
    // Initialize mutex
    mutex_init(&aslr_config.lock);
    
    // Set default configuration
    aslr_config.enabled = enabled;
    aslr_config.entropy_bits = (entropy_bits > 24) ? 24 : entropy_bits;
    aslr_config.flags = flags;
    
    // Initialize the random seed
    aslr_init_random_seed();
    
    log_info(ASLR_TAG, "ASLR initialized: %s, entropy: %u bits", 
             enabled ? "enabled" : "disabled", aslr_config.entropy_bits);
    
    return 0;
}

/**
 * Enable or disable ASLR globally
 */
void aslr_set_enabled(int enabled) {
    mutex_lock(&aslr_config.lock);
    aslr_config.enabled = enabled;
    mutex_unlock(&aslr_config.lock);
    
    log_info(ASLR_TAG, "ASLR %s", enabled ? "enabled" : "disabled");
}

/**
 * Set the entropy level for ASLR
 */
void aslr_set_entropy(uint8_t entropy_bits) {
    mutex_lock(&aslr_config.lock);
    aslr_config.entropy_bits = (entropy_bits > 24) ? 24 : entropy_bits;
    mutex_unlock(&aslr_config.lock);
    
    log_info(ASLR_TAG, "ASLR entropy set to %u bits", aslr_config.entropy_bits);
}

/**
 * Get ASLR status
 */
int aslr_is_enabled(void) {
    return aslr_config.enabled;
}

/**
 * Get current entropy bits
 */
uint8_t aslr_get_entropy(void) {
    return aslr_config.entropy_bits;
}

/**
 * Get current ASLR flags
 */
uint32_t aslr_get_flags(void) {
    return aslr_config.flags;
}

/**
 * Set ASLR flags
 */
void aslr_set_flags(uint32_t flags) {
    mutex_lock(&aslr_config.lock);
    aslr_config.flags = flags & ASLR_ALL;  // Ensure only valid bits are set
    mutex_unlock(&aslr_config.lock);
    
    log_info(ASLR_TAG, "ASLR regions mask set to 0x%08x", aslr_config.flags);
}

/**
 * Get a randomized offset for a memory region
 */
uint32_t aslr_get_random_offset(uint32_t offset_type) {
    if (!aslr_config.enabled || !(aslr_config.flags & offset_type)) {
        return 0;  // ASLR disabled or this offset type is not enabled
    }
    
    // Generate a random value
    uint32_t random = aslr_generate_random();
    
    // Limit random bits to the configured entropy level
    uint32_t mask = (1 << aslr_config.entropy_bits) - 1;
    random &= mask;
    
    // Scale the random value according to the offset type
    uint32_t max_pages;
    switch (offset_type) {
        case ASLR_STACK_OFFSET:
            max_pages = MAX_STACK_DELTA;
            break;
        case ASLR_HEAP_OFFSET:
            max_pages = MAX_HEAP_DELTA;
            break;
        case ASLR_MMAP_OFFSET:
            max_pages = MAX_MMAP_DELTA;
            break;
        case ASLR_EXEC_OFFSET:
            max_pages = MAX_EXEC_DELTA;
            break;
        case ASLR_LIB_OFFSET:
            max_pages = MAX_LIB_DELTA;
            break;
        case ASLR_VDSO_OFFSET:
            max_pages = MAX_VDSO_DELTA;
            break;
        default:
            return 0;  // Unknown offset type
    }
    
    // Scale the random value to the maximum delta for this offset type
    // Use modulo to keep within range
    uint32_t scaled_random = random % max_pages;
    
    // Convert to page-aligned byte offset
    return scaled_random * PAGE_SIZE;
}

/**
 * Apply ASLR to a virtual address
 */
void* aslr_randomize_address(void* base_addr, uint32_t offset_type) {
    if (!aslr_config.enabled || !base_addr) {
        return base_addr;  // ASLR disabled or null pointer
    }
    
    // Get the random offset for this memory region type
    uint32_t offset = aslr_get_random_offset(offset_type);
    
    // For stack, we subtract the offset (stack grows down)
    if (offset_type == ASLR_STACK_OFFSET) {
        return (void*)((uintptr_t)base_addr - offset);
    }
    
    // For all other regions, add the offset
    return (void*)((uintptr_t)base_addr + offset);
}
