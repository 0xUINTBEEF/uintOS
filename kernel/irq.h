#include "gdt.h"
#include "asm.h"
#include "task.h"
#include "lapic.h"
#include <inttypes.h>

#define UINTOS_IDT_SIZE 256
#define UINTOS_IDT_ENTRY_SIZE UINTOS_GDT_ENTRY_SIZE

typedef descriptor_table uintos_idt_t;

extern uintos_idt_t uintos_interrupt_descriptor_table;

#define UINTOS_ISEG_ACCESS(access) (access << 8)
/* t = TSS segment selector */
#define uintos_idt_add_gate(t, access, id) \
    add_segment(&uintos_interrupt_descriptor_table, t, 0x0000, UINTOS_ISEG_ACCESS(access), id, 0)

#define uintos_init_irq(irq_name, id) \
    UINTOS_INIT_TASK(irq_name); \
    uintos_idt_add_gate(UINTOS_TASK_SELECTOR(irq_name), 0x85, id)

typedef void (*uintos_irq_handler)(uint32_t error_code);
void uintos_initialize_interrupts();

// Register IRQ handlers with the UINTOS_ prefix for consistency
UINTOS_TASK_REGISTER(uintos_irq0);
void uintos_handle_divide_by_zero();

UINTOS_TASK_REGISTER(uintos_irq1);
void uintos_handle_debug_exception();

UINTOS_TASK_REGISTER(uintos_irq8);
void uintos_handle_double_fault(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq10);
void uintos_handle_invalid_tss();

UINTOS_TASK_REGISTER(uintos_irq11);
void uintos_handle_segment_not_present(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq13);
void uintos_handle_general_protection();

UINTOS_TASK_REGISTER(uintos_irq20);
void uintos_handle_double_fault_alt(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq32);
void uintos_handle_lapic_timer();

// Add a generic IRQ handler registration function for future extensions
void register_interrupt_handler(uint8_t irq_number, void (*handler)());

// Interrupt return macro used in handlers
#define UINTOS_INTERRUPT_RETURN() asm volatile("iret");
