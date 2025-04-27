#include "irq.h"
#include "task.h"
#include "lapic.h"

static segment_descriptor uintos_interrupt_gates[UINTOS_IDT_SIZE];

uintos_idt_t uintos_interrupt_descriptor_table = {
    .base = uintos_interrupt_gates,
    .size = UINTOS_DESCRIPTOR_SIZE * UINTOS_IDT_SIZE,
    .next_id = 0
};

// Array of function pointers for dynamically registered interrupt handlers
static void (*interrupt_handlers[256])() = {0};

// Register a custom interrupt handler for a specific IRQ number
void register_interrupt_handler(uint8_t irq_number, void (*handler)()) {
    if (irq_number < 256) {
        interrupt_handlers[irq_number] = handler;
    }
}

void uintos_initialize_interrupts() {
    uintos_init_irq(uintos_irq0, 0);
    uintos_init_irq(uintos_irq1, 1);
    uintos_init_irq(uintos_irq8, 8);
    uintos_init_irq(uintos_irq10, 10);
    uintos_init_irq(uintos_irq11, 11);
    uintos_init_irq(uintos_irq13, 13);
    uintos_init_irq(uintos_irq32, 32);

    UINTOS_LOAD_IDT(uintos_interrupt_descriptor_table);
}

UINTOS_TASK_START(uintos_irq0, uintos_handle_divide_by_zero);
void uintos_handle_divide_by_zero() {
    // Log the divide by zero error
    // In a real OS, we would print an error message or take appropriate action
    
    // Return from the interrupt without recursion
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq0);

UINTOS_TASK_START(uintos_irq1, uintos_handle_debug_exception);
void uintos_handle_debug_exception() {
    // Handle debug exception
    // Removed unsafe interrupt triggering that could cause cascading failures
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq1);

UINTOS_TASK_START(uintos_irq8, uintos_handle_double_fault);
void uintos_handle_double_fault(uint32_t error_code) {
    // Handle double fault with the error code
    // This is a serious error condition that indicates another fault occurred 
    // during handling of a previous fault
    
    // In a production OS, we might log this and halt the system
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq8);

UINTOS_TASK_START(uintos_irq10, uintos_handle_invalid_tss);
void uintos_handle_invalid_tss() {
    // Get error code using safer approach
    uint32_t error_code;
    asm volatile("mov %0, [ebp + 4]" : "=r"(error_code) : : "memory");
    
    // Handle the invalid TSS error
    // In a real OS, we would log this and take appropriate action
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq10);

UINTOS_TASK_START(uintos_irq11, uintos_handle_segment_not_present);
void uintos_handle_segment_not_present(uint32_t error_code) {
    // Handle segment not present error with the provided error code
    
    // In a production OS, we might log this and take recovery action
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq11);

UINTOS_TASK_START(uintos_irq13, uintos_handle_general_protection);
void uintos_handle_general_protection() {
    // Get error code using safer approach
    uint32_t error_code;
    asm volatile("mov %0, [ebp + 4]" : "=r"(error_code) : : "memory");
    
    // Handle general protection fault
    // In a real OS, we would log this and take appropriate action

    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq13);

UINTOS_TASK_START(uintos_irq32, uintos_handle_lapic_timer);
int uintos_active_task_id = 1;
void uintos_handle_lapic_timer() {
    // Simple round-robin task switching between two tasks
    uintos_active_task_id = (uintos_active_task_id == 1) ? 2 : 1;

    // Acknowledge the interrupt
    uintos_lapic_isr_complete();
    
    // Return from the interrupt handler
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq32);

UINTOS_TASK_START(uintos_irq20, uintos_handle_double_fault_alt);
void uintos_handle_double_fault_alt(uint32_t error_code) {
    // Alternative double fault handler
    // Having multiple handlers for the same fault type is generally not recommended,
    // but this might be used for specific debugging or testing purposes
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq20);
