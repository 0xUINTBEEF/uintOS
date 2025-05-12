#include "exception_handlers.h"
#include "panic.h"
#include "irq.h"
#include "logging/log.h"
#include "task.h"
#include <stdint.h>
#include <string.h>

// Array of exception handlers
exception_handler_t exception_handlers[32] = {0};

/**
 * @file exception_handlers.c
 * @brief CPU exception handlers for uintOS
 *
 * This file implements handlers for CPU exceptions, which are integrated
 * with the kernel panic system for proper error reporting and system
 * protection.
 */

// Exception names for better diagnostics
static const char* exception_names[] = {
    "Division By Zero",              // 0
    "Debug",                         // 1
    "Non-maskable Interrupt",        // 2
    "Breakpoint",                    // 3
    "Overflow",                      // 4
    "Bound Range Exceeded",          // 5
    "Invalid Opcode",                // 6
    "Device Not Available",          // 7
    "Double Fault",                  // 8
    "Coprocessor Segment Overrun",   // 9
    "Invalid TSS",                   // 10
    "Segment Not Present",           // 11
    "Stack-Segment Fault",           // 12
    "General Protection Fault",      // 13
    "Page Fault",                    // 14
    "Reserved",                      // 15
    "x87 Floating-Point Exception",  // 16
    "Alignment Check",               // 17
    "Machine Check",                 // 18
    "SIMD Floating-Point Exception", // 19
    "Virtualization Exception",      // 20
    "Control Protection Exception",  // 21
    "Reserved",                      // 22
    "Reserved",                      // 23
    "Reserved",                      // 24
    "Reserved",                      // 25
    "Reserved",                      // 26
    "Reserved",                      // 27
    "Reserved",                      // 28
    "Reserved",                      // 29
    "Reserved",                      // 30
    "Reserved"                       // 31
};

/**
 * Helper function to get exception name
 */
const char* exception_get_name(uint32_t vector) {
    if (vector < sizeof(exception_names) / sizeof(exception_names[0])) {
        return exception_names[vector];
    }
    return "Unknown Exception";
}

/**
 * Register an exception handler
 */
void register_exception_handler(uint32_t vector, exception_handler_t handler) {
    if (vector >= 32) {
        log_error("EXCEPTION", "Cannot register handler for vector %d (out of range)", vector);
        return;
    }
    
    exception_handlers[vector] = handler;
}

/**
 * Initialize exception handlers
 */
void exception_init(void) {
    log_info("KERNEL", "Initializing CPU exception handlers");
    
    // Register handlers for each processor exception
    register_exception_handler(0, div_zero_handler);
    register_exception_handler(1, debug_handler);
    register_exception_handler(2, nmi_handler);
    register_exception_handler(3, breakpoint_handler);
    register_exception_handler(4, overflow_handler);
    register_exception_handler(5, bound_range_handler);
    register_exception_handler(6, invalid_opcode_handler);
    register_exception_handler(7, device_not_available_handler);
    register_exception_handler(8, double_fault_handler);
    register_exception_handler(9, coproc_segment_overrun_handler);
    register_exception_handler(10, invalid_tss_handler);
    register_exception_handler(11, segment_not_present_handler);
    register_exception_handler(12, stack_segment_fault_handler);
    register_exception_handler(13, general_protection_handler);
    register_exception_handler(14, page_fault_handler);
    // 15 is reserved
    register_exception_handler(16, fpu_exception_handler);
    register_exception_handler(17, alignment_check_handler);
    register_exception_handler(18, machine_check_handler);
    register_exception_handler(19, simd_exception_handler);
    register_exception_handler(20, virtualization_exception_handler);
    
    log_info("KERNEL", "CPU exception handlers initialized");
}

/**
 * Generic exception handler template
 */
static void generic_exception_handler(
    uint32_t vector,
    panic_type_t panic_type,
    interrupt_frame_t* frame,
    uint32_t error_code
) {
    // Log the exception details
    log_error("EXCEPTION", 
              "%s (Vector %d, Error 0x%08x) at CS:EIP=0x%04x:0x%08x", 
              exception_names[vector], 
              vector,
              error_code, 
              frame->cs,
              frame->eip);

    // Get task information if available
    const char* task_name = "Unknown";
    uint32_t task_id = 0;
    
    task_t* current_task = get_current_task();
    if (current_task) {
        task_name = current_task->name;
        task_id = current_task->id;
    }

    // Trigger kernel panic with detailed information
    PANIC(panic_type, "CPU Exception: %s (Vector %d, Error 0x%08x) in task %s (%d)",
          exception_names[vector], vector, error_code, task_name, task_id);
}

/**
 * Division by zero handler
 */
void div_zero_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(0, PANIC_DIVISION_BY_ZERO, frame, error_code);
}

/**
 * Debug exception handler
 */
void debug_handler(interrupt_frame_t* frame, uint32_t error_code) {
    // Debug exceptions can be non-fatal in most cases
    log_warning("EXCEPTION", 
              "Debug Exception at CS:EIP=0x%04x:0x%08x", 
              frame->cs,
              frame->eip);
    
    // Get debug register state to analyze what triggered the exception
    debug_registers_t debug_regs;
    debug_registers_get_state(&debug_regs);
    
    // Analyze debug event type by examining DR6
    printf("\n==== DEBUG EXCEPTION DETAILS ====\n");
    printf("Instruction pointer: 0x%08x\n", frame->eip);
    
    // Print debug register values
    printf("DR0: 0x%08x  DR1: 0x%08x\n", debug_regs.dr0, debug_regs.dr1);
    printf("DR2: 0x%08x  DR3: 0x%08x\n", debug_regs.dr2, debug_regs.dr3);
    printf("DR6: 0x%08x  DR7: 0x%08x\n", debug_regs.dr6, debug_regs.dr7);
    
    int breakpoint_index = -1;
    bool handled = false;
    
    // Check if this was a hardware breakpoint
    if (debug_is_breakpoint_hit(&debug_regs, &breakpoint_index)) {
        log_info("DEBUG", "Hardware breakpoint %d triggered at EIP=0x%08x", 
                 breakpoint_index, frame->eip);
        printf("Hardware breakpoint %d triggered\n", breakpoint_index);
        
        // Determine breakpoint type
        uint32_t rw_bits = (debug_regs.dr7 >> (16 + breakpoint_index*4)) & 3;
        uint32_t len_bits = (debug_regs.dr7 >> (18 + breakpoint_index*4)) & 3;
        
        const char* type_str = "unknown";
        switch (rw_bits) {
            case 0: type_str = "execution"; break;
            case 1: type_str = "data write"; break;
            case 2: type_str = "I/O access"; break;
            case 3: type_str = "data read/write"; break;
        }
        
        uint32_t size = 1;
        switch (len_bits) {
            case 0: size = 1; break;
            case 1: size = 2; break;
            case 2: size = 8; break; // Only in 64-bit mode
            case 3: size = 4; break;
        }
        
        printf("Type: %s breakpoint, Size: %d bytes\n", type_str, size);
        
        // Clear the status flag in DR6 to acknowledge the breakpoint
        debug_regs.dr6 &= ~(1 << breakpoint_index);
        debug_registers_set_state(&debug_regs);
        handled = true;
    }
    
    // Check if this was a single-step exception    if (debug_is_single_step(&debug_regs)) {
        log_info("DEBUG", "Single-step at EIP=0x%08x", frame->eip);
        printf("Single-step trap\n");
        
        // Clear the BS flag in DR6
        debug_regs.dr6 &= ~DR6_BS;
        debug_registers_set_state(&debug_regs);
        handled = true;
    }
    
    // Check for other debug events
    if (debug_regs.dr6 & DR6_BD) {
        log_info("DEBUG", "Debug register access detected at EIP=0x%08x", frame->eip);
        printf("Debug register access detected\n");
        
        // Clear the BD flag
        debug_regs.dr6 &= ~DR6_BD;
        debug_registers_set_state(&debug_regs);
        handled = true;
    }
    
    if (debug_regs.dr6 & DR6_BT) {
        log_info("DEBUG", "Task switch debug event at EIP=0x%08x", frame->eip);
        printf("Task switch debug event\n");
        
        // Clear the BT flag
        debug_regs.dr6 &= ~DR6_BT;
        debug_registers_set_state(&debug_regs);
        handled = true;
    }
    
    // General debug exception if nothing specific was detected
    if (!handled) {
        log_info("DEBUG", "General debug exception at EIP=0x%08x", frame->eip);
        printf("General debug exception\n");
        
        // Reset DR6 state
        debug_regs.dr6 = 0;
        debug_registers_set_state(&debug_regs);
    }
    
    printf("================================\n\n");
    
    // No panic - just log and continue
    // The debug exception is handled and will resume execution
}

/**
 * Non-maskable interrupt handler
 */
void nmi_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(2, PANIC_HARDWARE_FAILURE, frame, error_code);
}

/**
 * Breakpoint handler
 */
void breakpoint_handler(interrupt_frame_t* frame, uint32_t error_code) {
    // Breakpoints are often non-fatal
    log_info("EXCEPTION", 
            "Breakpoint at CS:EIP=0x%04x:0x%08x", 
            frame->cs,
            frame->eip);
    
    // No panic - just log and continue
    // This allows the use of int3 for debugging
}

/**
 * Overflow handler
 */
void overflow_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(4, PANIC_GENERAL, frame, error_code);
}

/**
 * Bound range exceeded handler
 */
void bound_range_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(5, PANIC_KERNEL_BOUNDS, frame, error_code);
}

/**
 * Invalid opcode handler
 */
void invalid_opcode_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(6, PANIC_GENERAL, frame, error_code);
}

/**
 * Device not available handler
 */
void device_not_available_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(7, PANIC_HARDWARE_FAILURE, frame, error_code);
}

/**
 * Double fault handler - very serious error
 */
void double_fault_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(8, PANIC_DOUBLE_FAULT, frame, error_code);
}

/**
 * Coprocessor segment overrun handler
 */
void coproc_segment_overrun_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(9, PANIC_GENERAL, frame, error_code);
}

/**
 * Invalid TSS handler
 */
void invalid_tss_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(10, PANIC_GENERAL, frame, error_code);
}

/**
 * Segment not present handler
 */
void segment_not_present_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(11, PANIC_GENERAL, frame, error_code);
}

/**
 * Stack segment fault handler
 */
void stack_segment_fault_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(12, PANIC_STACK_OVERFLOW, frame, error_code);
}

/**
 * General protection fault handler
 */
void general_protection_handler(interrupt_frame_t* frame, uint32_t error_code) {
    // Extract valuable information from error code
    uint8_t table = (error_code & 0x6) >> 1;
    uint16_t index = (error_code & 0xFFF8) >> 3;
    
    char table_type[16] = "Unknown";
    switch (table) {
        case 0: strcpy(table_type, "GDT"); break;
        case 1: strcpy(table_type, "IDT"); break;
        case 2: strcpy(table_type, "LDT"); break;
        case 3: strcpy(table_type, "IDT"); break;
    }
    
    log_error("EXCEPTION", "General Protection Fault at CS:EIP=0x%04x:0x%08x, Table: %s, Index: %d", 
             frame->cs, frame->eip, table_type, index);

    // Get task information
    const char* task_name = "Unknown";
    uint32_t task_id = 0;
    
    task_t* current_task = get_current_task();
    if (current_task) {
        task_name = current_task->name;
        task_id = current_task->id;
    }

    PANIC(PANIC_GENERAL, "General Protection Fault in task %s (%d), %s Index %d, Error 0x%08x",
          task_name, task_id, table_type, index, error_code);
}

/**
 * Page fault handler
 */
void page_fault_handler(interrupt_frame_t* frame, uint32_t error_code) {
    // Get the address that caused the page fault
    uint32_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r" (fault_addr));
    
    // Extract information from error code
    bool present = error_code & 0x1;
    bool write = error_code & 0x2;
    bool user = error_code & 0x4;
    bool reserved = error_code & 0x8;
    bool instruction = error_code & 0x10;
    
    // Generate descriptive error message
    char error_desc[128];
    snprintf(error_desc, sizeof(error_desc),
             "Page %s, %s, %s mode, %s, %s fetch",
             present ? "protection violation" : "not present",
             write ? "write" : "read",
             user ? "user" : "supervisor",
             reserved ? "reserved bit violation" : "not reserved violation",
             instruction ? "instruction" : "data");
    
    log_error("EXCEPTION", 
             "Page Fault at address 0x%08x, CS:EIP=0x%04x:0x%08x, %s", 
             fault_addr, frame->cs, frame->eip, error_desc);

    // Get task information
    const char* task_name = "Unknown";
    uint32_t task_id = 0;
    
    task_t* current_task = get_current_task();
    if (current_task) {
        task_name = current_task->name;
        task_id = current_task->id;
    }

    PANIC(PANIC_PAGE_FAULT, 
          "Page fault accessing 0x%08x in task %s (%d), %s",
          fault_addr, task_name, task_id, error_desc);
}

/**
 * Floating point exception handler
 */
void fpu_exception_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(16, PANIC_GENERAL, frame, error_code);
}

/**
 * Alignment check handler
 */
void alignment_check_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(17, PANIC_GENERAL, frame, error_code);
}

/**
 * Machine check handler
 */
void machine_check_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(18, PANIC_HARDWARE_FAILURE, frame, error_code);
}

/**
 * SIMD floating point exception handler
 */
void simd_exception_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(19, PANIC_GENERAL, frame, error_code);
}

/**
 * Virtualization exception handler
 */
void virtualization_exception_handler(interrupt_frame_t* frame, uint32_t error_code) {
    generic_exception_handler(20, PANIC_GENERAL, frame, error_code);
}
