#include "gdt.h"
#include "asm.h"
#include "task.h"
#include "lapic.h"

#define UINTOS_IDT_SIZE 256
#define UINTOS_IDT_ENTRY_SIZE UINTOS_GDT_ENTRY_SIZE

typedef descriptor_table uintos_idt_t;

extern uintos_idt_t uintos_idt;

#define UINTOS_ISEG_ACCESS(access) (access << 8)
/* t = TSS segment selector */
#define uintos_idt_add_gate(t, access, id)                         \
    add_segment(&uintos_idt, t, 0x0000, UINTOS_ISEG_ACCESS(access), id, 0)

#define uintos_init_irq(irq_name, id)                         \
    INIT_TASK(irq_name);                            \
    uintos_idt_add_gate(TASK_SELECTOR(irq_name), 0x85, id)

typedef void (*uintos_irq_handler)(uint32_t error_code);
void uintos_setup_irq();

TASK_REGISTER(uintos_irq0);
void uintos_divide_by_zero_handler();

TASK_REGISTER(uintos_irq1);
void uintos_debug_exception();

TASK_REGISTER(uintos_irq8);
void uintos_double_fault_handler(uint32_t error_code);

TASK_REGISTER(uintos_irq10);
void uintos_invalid_tss_handler();

TASK_REGISTER(uintos_irq11);
void uintos_segment_not_present(uint32_t error_code);

TASK_REGISTER(uintos_irq13);
void uintos_general_protection_exception();

TASK_REGISTER(uintos_irq20);
void uintos_double_fault_handler2(uint32_t error_code);

TASK_REGISTER(uintos_irq32);
void uintos_lapic_timer_handler();
