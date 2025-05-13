/**
 * @file aslr.h
 * @brief Address Space Layout Randomization Support
 *
 * This module provides ASLR functionality to randomize the virtual memory
 * layout of processes, making the OS more resilient against memory-based
 * exploits like buffer overflows and return-to-libc attacks.
 */

#ifndef ASLR_H
#define ASLR_H

#include <stdint.h>
#include <stddef.h>

/**
 * ASLR offset types - these define which parts of memory get randomized
 */
#define ASLR_STACK_OFFSET      0x00000001  // Randomize stack locations
#define ASLR_HEAP_OFFSET       0x00000002  // Randomize heap locations 
#define ASLR_MMAP_OFFSET       0x00000004  // Randomize mmap regions
#define ASLR_EXEC_OFFSET       0x00000008  // Randomize executable locations
#define ASLR_LIB_OFFSET        0x00000010  // Randomize shared library locations
#define ASLR_VDSO_OFFSET       0x00000020  // Randomize VDSO page location
#define ASLR_ALL               0x0000003F  // All of the above

/**
 * ASLR entropy levels - more bits = more randomization
 */
#define ASLR_ENTROPY_LOW       8   // 8 bits - 256 possible positions
#define ASLR_ENTROPY_MEDIUM    16  // 16 bits - 65536 possible positions
#define ASLR_ENTROPY_HIGH      24  // 24 bits - ~16M possible positions
#define ASLR_ENTROPY_DEFAULT   ASLR_ENTROPY_MEDIUM

/**
 * Initialize ASLR subsystem
 *
 * @param enabled Whether ASLR should be enabled by default
 * @param entropy_bits Number of bits of entropy to use (8-24)
 * @param flags Which memory regions to randomize
 * @return 0 on success, error code on failure
 */
int aslr_init(int enabled, uint8_t entropy_bits, uint32_t flags);

/**
 * Enable or disable ASLR globally
 *
 * @param enabled Whether ASLR should be enabled
 */
void aslr_set_enabled(int enabled);

/**
 * Set the entropy level for ASLR
 *
 * @param entropy_bits Number of bits of entropy to use (8-24)
 */
void aslr_set_entropy(uint8_t entropy_bits);

/**
 * Get a randomized offset for a memory region
 *
 * @param offset_type Type of offset to generate (ASLR_*_OFFSET)
 * @return Randomized offset value aligned to page boundaries
 */
uint32_t aslr_get_random_offset(uint32_t offset_type);

/**
 * Apply ASLR to a virtual address
 *
 * @param base_addr Base address before ASLR
 * @param offset_type Type of memory region
 * @return Randomized address
 */
void* aslr_randomize_address(void* base_addr, uint32_t offset_type);

/**
 * Get ASLR status
 *
 * @return 1 if enabled, 0 if disabled
 */
int aslr_is_enabled(void);

/**
 * Get current entropy bits
 *
 * @return Current entropy bits (8-24)
 */
uint8_t aslr_get_entropy(void);

/**
 * Set entropy bits
 *
 * @param entropy_bits Number of bits of entropy to use (8-24)
 */
void aslr_set_entropy(uint8_t entropy_bits);

/**
 * Get current ASLR flags
 *
 * @return Current flags (which memory regions are randomized)
 */
uint32_t aslr_get_flags(void);

/**
 * Set ASLR flags
 *
 * @param flags Flags indicating which memory regions to randomize
 */
void aslr_set_flags(uint32_t flags);

#endif /* ASLR_H */
