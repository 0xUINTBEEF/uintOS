#include "irq.h"
#include "task.h"
#include "lapic.h"

static segment_descriptor uintos_interrupt_gates[UINTOS_IDT_SIZE];

uintos_idt_t uintos_interrupt_descriptor_table = {
    .base = uintos_interrupt_gates,
    .size = UINTOS_DESCRIPTOR_SIZE * UINTOS_IDT_SIZE,
    .next_id = 0
};

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
    UINTOS_INTERRUPT_RETURN();
    uintos_handle_divide_by_zero();
}
UINTOS_TASK_END(uintos_irq0);

UINTOS_TASK_START(uintos_irq1, uintos_handle_debug_exception);
void uintos_handle_debug_exception() {
    asm("int 8");
}
UINTOS_TASK_END(uintos_irq1);

UINTOS_TASK_START(uintos_irq8, uintos_handle_double_fault);
void uintos_handle_double_fault(uint32_t error_code) {
    int fault_code = error_code;
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq8);

UINTOS_TASK_START(uintos_irq10, uintos_handle_invalid_tss);
void uintos_handle_invalid_tss() {
    asm("mov eax, [ebp + 4]");
    int error_code = 0;
    asm("mov %0, eax":"=r"(error_code)::);
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq10);

UINTOS_TASK_START(uintos_irq11, uintos_handle_segment_not_present);
void uintos_handle_segment_not_present(uint32_t error_code) {
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq11);

UINTOS_TASK_START(uintos_irq13, uintos_handle_general_protection);
void uintos_handle_general_protection() {
_start:
    asm("mov eax, [ebp + 4]");
    int error_code = 0;
    asm("mov %0, eax":"=r"(error_code)::);

    UINTOS_INTERRUPT_RETURN();
    asm("mov ebp, esp");
    asm("sub ebp, 4");
    goto _start;
}
UINTOS_TASK_END(uintos_irq13);

UINTOS_TASK_START(uintos_irq32, uintos_handle_lapic_timer);
int uintos_active_task_id = 1;
void uintos_handle_lapic_timer() {
_start:
    if (uintos_active_task_id == 1)
        uintos_active_task_id = 2;
    else
        uintos_active_task_id = 1;

    uintos_lapic_isr_complete();
    UINTOS_INTERRUPT_RETURN();
    asm("mov ebp, esp");
    asm("sub ebp, 4");
    goto _start;
}
UINTOS_TASK_END(uintos_irq32);

UINTOS_TASK_START(uintos_irq20, uintos_handle_double_fault_alt);
void uintos_handle_double_fault_alt(uint32_t error_code) {
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq20);
