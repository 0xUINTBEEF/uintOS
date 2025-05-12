#ifndef EXCEPTION_HANDLERS_H
#define EXCEPTION_HANDLERS_H

#include <stdint.h>

/**
 * @file exception_handlers.h
 * @brief CPU exception handlers for uintOS
 *
 * This file defines handlers for CPU exceptions, which are integrated
 * with the kernel panic system for proper error reporting.
 */

// Interrupt frame structure (matches the stack layout from interrupt handlers)
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t int_no;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t ss;
} __attribute__((packed)) interrupt_frame_t;

// Exception handler function prototype
typedef void (*exception_handler_t)(interrupt_frame_t* frame, uint32_t error_code);

// Register an exception handler with the IDT
void register_exception_handler(uint32_t vector, exception_handler_t handler);

// Initialize exception handlers
void exception_init(void);

// Helper function to get exception name
const char* exception_get_name(uint32_t vector);

// Exception handler functions
void div_zero_handler(interrupt_frame_t* frame, uint32_t error_code);
void debug_handler(interrupt_frame_t* frame, uint32_t error_code);
void nmi_handler(interrupt_frame_t* frame, uint32_t error_code);
void breakpoint_handler(interrupt_frame_t* frame, uint32_t error_code);
void overflow_handler(interrupt_frame_t* frame, uint32_t error_code);
void bound_range_handler(interrupt_frame_t* frame, uint32_t error_code);
void invalid_opcode_handler(interrupt_frame_t* frame, uint32_t error_code);
void device_not_available_handler(interrupt_frame_t* frame, uint32_t error_code);
void double_fault_handler(interrupt_frame_t* frame, uint32_t error_code);
void coproc_segment_overrun_handler(interrupt_frame_t* frame, uint32_t error_code);
void invalid_tss_handler(interrupt_frame_t* frame, uint32_t error_code);
void segment_not_present_handler(interrupt_frame_t* frame, uint32_t error_code);
void stack_segment_fault_handler(interrupt_frame_t* frame, uint32_t error_code);
void general_protection_handler(interrupt_frame_t* frame, uint32_t error_code);
void page_fault_handler(interrupt_frame_t* frame, uint32_t error_code);
void fpu_exception_handler(interrupt_frame_t* frame, uint32_t error_code);
void alignment_check_handler(interrupt_frame_t* frame, uint32_t error_code);
void machine_check_handler(interrupt_frame_t* frame, uint32_t error_code);
void simd_exception_handler(interrupt_frame_t* frame, uint32_t error_code);
void virtualization_exception_handler(interrupt_frame_t* frame, uint32_t error_code);

#endif /* EXCEPTION_HANDLERS_H */
