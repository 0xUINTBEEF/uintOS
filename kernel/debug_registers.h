#ifndef DEBUG_REGISTERS_H
#define DEBUG_REGISTERS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @file debug_registers.h
 * @brief Hardware debug registers support for uintOS
 *
 * This file provides functionality to interact with CPU hardware debug registers
 * for breakpoint handling and execution tracing.
 */

// Debug register DR7 bit definitions
#define DR7_L0      (1 << 0)   // Local breakpoint 0 enable
#define DR7_G0      (1 << 1)   // Global breakpoint 0 enable
#define DR7_L1      (1 << 2)   // Local breakpoint 1 enable
#define DR7_G1      (1 << 3)   // Global breakpoint 1 enable
#define DR7_L2      (1 << 4)   // Local breakpoint 2 enable
#define DR7_G2      (1 << 5)   // Global breakpoint 2 enable
#define DR7_L3      (1 << 6)   // Local breakpoint 3 enable
#define DR7_G3      (1 << 7)   // Global breakpoint 3 enable
#define DR7_LE      (1 << 8)   // Local exact breakpoint enable
#define DR7_GE      (1 << 9)   // Global exact breakpoint enable
#define DR7_RTM     (1 << 11)  // Restricted Transactional Memory
#define DR7_GD      (1 << 13)  // General detect enable

// Breakpoint types
#define DR7_RW_EXEC    (0 << 16) // Break on execution
#define DR7_RW_WRITE   (1 << 16) // Break on data write
#define DR7_RW_IO      (2 << 16) // Break on IO read/write (not supported on all CPUs)
#define DR7_RW_ACCESS  (3 << 16) // Break on data read/write

// Breakpoint sizes
#define DR7_LEN_1      (0 << 18) // 1 byte
#define DR7_LEN_2      (1 << 18) // 2 bytes
#define DR7_LEN_4      (3 << 18) // 4 bytes
#define DR7_LEN_8      (2 << 18) // 8 bytes (only on 64-bit mode)

// DR6 bit definitions
#define DR6_B0       (1 << 0)   // Breakpoint 0 hit
#define DR6_B1       (1 << 1)   // Breakpoint 1 hit
#define DR6_B2       (1 << 2)   // Breakpoint 2 hit
#define DR6_B3       (1 << 3)   // Breakpoint 3 hit
#define DR6_BD       (1 << 13)  // Debug register access detected
#define DR6_BS       (1 << 14)  // Single step
#define DR6_BT       (1 << 15)  // Task switch

// Breakpoint types enumeration
typedef enum {
    BREAKPOINT_EXECUTION = 0,
    BREAKPOINT_WRITE = 1,
    BREAKPOINT_IO = 2,
    BREAKPOINT_ACCESS = 3
} debug_breakpoint_type_t;

// Breakpoint sizes enumeration
typedef enum {
    BREAKPOINT_SIZE_1 = 0,
    BREAKPOINT_SIZE_2 = 1,
    BREAKPOINT_SIZE_4 = 3,
    BREAKPOINT_SIZE_8 = 2  // Only valid on 64-bit mode
} debug_breakpoint_size_t;

// Debug register state
typedef struct {
    uint32_t dr0;  // Address breakpoint 0
    uint32_t dr1;  // Address breakpoint 1
    uint32_t dr2;  // Address breakpoint 2
    uint32_t dr3;  // Address breakpoint 3
    uint32_t dr6;  // Debug status
    uint32_t dr7;  // Debug control
} debug_registers_t;

/**
 * Initialize debug register support
 * 
 * @return 0 on success, non-zero on failure
 */
int debug_registers_init(void);

/**
 * Get current debug register state
 * 
 * @param regs Pointer to structure to receive debug register values
 */
void debug_registers_get_state(debug_registers_t* regs);

/**
 * Set debug registers state
 * 
 * @param regs Pointer to structure containing debug register values to set
 * @return 0 on success, non-zero on failure
 */
int debug_registers_set_state(const debug_registers_t* regs);

/**
 * Set a hardware breakpoint
 * 
 * @param index Breakpoint number (0-3)
 * @param address Memory address for breakpoint
 * @param type Type of access to break on
 * @param size Size of memory region to watch
 * @param global Whether breakpoint is global (across all tasks) or local
 * @return 0 on success, non-zero on error
 */
int debug_set_breakpoint(int index, void* address, 
                          debug_breakpoint_type_t type, 
                          debug_breakpoint_size_t size,
                          bool global);

/**
 * Clear a hardware breakpoint
 * 
 * @param index Breakpoint number (0-3)
 * @return 0 on success, non-zero on error
 */
int debug_clear_breakpoint(int index);

/**
 * Enable single-step mode
 * 
 * This sets the trap flag in EFLAGS register to enable single-stepping
 */
void debug_enable_single_step(void);

/**
 * Disable single-step mode
 * 
 * This clears the trap flag in EFLAGS register to disable single-stepping
 */
void debug_disable_single_step(void);

/**
 * Check if a debug breakpoint was triggered
 * 
 * @param regs Debug registers state
 * @param breakpoint_index Pointer to receive the index of triggered breakpoint
 * @return true if a breakpoint triggered, false otherwise
 */
bool debug_is_breakpoint_hit(debug_registers_t* regs, int* breakpoint_index);

/**
 * Check if the debug event was a single step
 * 
 * @param regs Debug registers state
 * @return true if single step flag is set
 */
bool debug_is_single_step(debug_registers_t* regs);

#endif /* DEBUG_REGISTERS_H */
