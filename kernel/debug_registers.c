#include "debug_registers.h"
#include "logging/log.h"
#include <stddef.h>

/**
 * @file debug_registers.c
 * @brief Hardware debug registers support for uintOS
 *
 * This file implements functionality to interact with CPU hardware debug registers
 * for breakpoint handling and execution tracing.
 */

// Read a debug register
static inline uint32_t read_dr(int reg) {
    uint32_t value = 0;
    switch (reg) {
        case 0:
            asm volatile("mov %%dr0, %0" : "=r"(value));
            break;
        case 1:
            asm volatile("mov %%dr1, %0" : "=r"(value));
            break;
        case 2:
            asm volatile("mov %%dr2, %0" : "=r"(value));
            break;
        case 3:
            asm volatile("mov %%dr3, %0" : "=r"(value));
            break;
        case 6:
            asm volatile("mov %%dr6, %0" : "=r"(value));
            break;
        case 7:
            asm volatile("mov %%dr7, %0" : "=r"(value));
            break;
    }
    return value;
}

// Write a debug register
static inline void write_dr(int reg, uint32_t value) {
    switch (reg) {
        case 0:
            asm volatile("mov %0, %%dr0" :: "r"(value));
            break;
        case 1:
            asm volatile("mov %0, %%dr1" :: "r"(value));
            break;
        case 2:
            asm volatile("mov %0, %%dr2" :: "r"(value));
            break;
        case 3:
            asm volatile("mov %0, %%dr3" :: "r"(value));
            break;
        case 6:
            asm volatile("mov %0, %%dr6" :: "r"(value));
            break;
        case 7:
            asm volatile("mov %0, %%dr7" :: "r"(value));
            break;
    }
}

/**
 * Initialize debug register support
 */
int debug_registers_init(void) {
    // Reset all debug registers to a clean state
    write_dr(0, 0);
    write_dr(1, 0);
    write_dr(2, 0);
    write_dr(3, 0);
    write_dr(7, 0x400); // Set bit 10, which should be set on reserved bits by default

    log_info("KERNEL", "Debug registers initialized");
    return 0;
}

/**
 * Get current debug register state
 */
void debug_registers_get_state(debug_registers_t* regs) {
    if (!regs) return;
    
    regs->dr0 = read_dr(0);
    regs->dr1 = read_dr(1);
    regs->dr2 = read_dr(2);
    regs->dr3 = read_dr(3);
    regs->dr6 = read_dr(6);
    regs->dr7 = read_dr(7);
}

/**
 * Set debug registers state
 */
int debug_registers_set_state(const debug_registers_t* regs) {
    if (!regs) return -1;
    
    // Make sure reserved bits in DR7 are properly set
    uint32_t dr7 = regs->dr7;
    dr7 |= 0x400;  // Set bit 10 which is reserved
    
    write_dr(0, regs->dr0);
    write_dr(1, regs->dr1);
    write_dr(2, regs->dr2);
    write_dr(3, regs->dr3);
    write_dr(6, regs->dr6);
    write_dr(7, dr7);
    
    return 0;
}

/**
 * Set a hardware breakpoint
 */
int debug_set_breakpoint(int index, void* address, 
                          debug_breakpoint_type_t type, 
                          debug_breakpoint_size_t size,
                          bool global) {
    if (index < 0 || index > 3) {
        log_error("DEBUG", "Invalid breakpoint index: %d", index);
        return -1;
    }
    
    if (!address) {
        log_error("DEBUG", "Invalid breakpoint address: NULL");
        return -1;
    }
    
    // Get current DR7 value
    uint32_t dr7 = read_dr(7);
    
    // Calculate bit position for this breakpoint settings
    uint32_t rw_shift = 16 + (index * 4);  // RW bits are at 16,20,24,28
    uint32_t len_shift = 18 + (index * 4); // LEN bits are at 18,22,26,30
    
    // Clear old settings for this breakpoint
    dr7 &= ~(3UL << rw_shift);   // Clear RW bits
    dr7 &= ~(3UL << len_shift);  // Clear LEN bits
    
    // Set local/global enable bit
    if (global) {
        dr7 |= (1UL << (1 + index*2));  // Set global bit (G0-G3)
        dr7 &= ~(1UL << (index*2));     // Clear local bit (L0-L3)
    } else {
        dr7 |= (1UL << (index*2));      // Set local bit (L0-L3)
        dr7 &= ~(1UL << (1 + index*2)); // Clear global bit (G0-G3)
    }
    
    // Set breakpoint type and size
    dr7 |= ((uint32_t)type & 3) << rw_shift;
    dr7 |= ((uint32_t)size & 3) << len_shift;
    
    // Set the address in corresponding debug address register
    switch (index) {
        case 0: write_dr(0, (uint32_t)address); break;
        case 1: write_dr(1, (uint32_t)address); break;
        case 2: write_dr(2, (uint32_t)address); break;
        case 3: write_dr(3, (uint32_t)address); break;
    }
    
    // Update DR7 with new settings
    write_dr(7, dr7);
    
    log_info("DEBUG", "Set breakpoint %d at 0x%08x, type=%d, size=%d, global=%d", 
             index, (uint32_t)address, type, size, global);
    
    return 0;
}

/**
 * Clear a hardware breakpoint
 */
int debug_clear_breakpoint(int index) {
    if (index < 0 || index > 3) {
        log_error("DEBUG", "Invalid breakpoint index: %d", index);
        return -1;
    }
    
    // Get current DR7 value
    uint32_t dr7 = read_dr(7);
    
    // Clear enable bits for this breakpoint
    dr7 &= ~(1UL << (index*2));      // Clear local bit (L0-L3)
    dr7 &= ~(1UL << (1 + index*2));  // Clear global bit (G0-G3)
    
    // Update DR7 with enable bits cleared
    write_dr(7, dr7);
    
    log_info("DEBUG", "Cleared breakpoint %d", index);
    
    return 0;
}

/**
 * Enable single-step mode
 */
void debug_enable_single_step(void) {
    // Set trap flag (bit 8) in EFLAGS
    asm volatile(
        "pushf\n"
        "orl $0x100, (%esp)\n"
        "popf\n"
    );
    
    log_info("DEBUG", "Single-step mode enabled");
}

/**
 * Disable single-step mode
 */
void debug_disable_single_step(void) {
    // Clear trap flag (bit 8) in EFLAGS
    asm volatile(
        "pushf\n"
        "andl $~0x100, (%esp)\n"
        "popf\n"
    );
    
    log_info("DEBUG", "Single-step mode disabled");
}

/**
 * Check if a debug breakpoint was triggered
 */
bool debug_is_breakpoint_hit(debug_registers_t* regs, int* breakpoint_index) {
    if (!regs) return false;
    
    // Check if any of B0-B3 bits are set in DR6
    uint32_t hit_bits = regs->dr6 & 0xF;
    
    if (hit_bits) {
        // Find first bit that's set
        if (breakpoint_index) {
            if (hit_bits & DR6_B0) *breakpoint_index = 0;
            else if (hit_bits & DR6_B1) *breakpoint_index = 1;
            else if (hit_bits & DR6_B2) *breakpoint_index = 2;
            else if (hit_bits & DR6_B3) *breakpoint_index = 3;
        }
        return true;
    }
    
    return false;
}

/**
 * Check if the debug event was a single step
 */
bool debug_is_single_step(debug_registers_t* regs) {
    if (!regs) return false;
    
    // Check if BS (bit 14) is set in DR6
    return (regs->dr6 & DR6_BS) != 0;
}
