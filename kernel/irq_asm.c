#include "irq_asm.h"
#include "irq.h"
#include "exception_handlers.h"

/**
 * @file irq_asm.c
 * @brief Assembly stub for IRQ and exception handlers
 *
 * This file provides the assembly stubs that dispatch exceptions
 * and interrupts to the proper C handlers.
 */

// External handler functions
extern void irq_common_handler(uint32_t int_no, uint32_t error_code);
extern void exception_common_handler(interrupt_frame_t* frame);

// Array of function pointers for exception handlers
extern exception_handler_t exception_handlers[32];

// Assembly stubs for CPU exceptions (0-31)
void isr0();
void isr1();
void isr2();
void isr3();
void isr4();
void isr5();
void isr6();
void isr7();
void isr8();
void isr9();
void isr10();
void isr11();
void isr12();
void isr13();
void isr14();
void isr15();
void isr16();
void isr17();
void isr18();
void isr19();
void isr20();
void isr21();
void isr22();
void isr23();
void isr24();
void isr25();
void isr26();
void isr27();
void isr28();
void isr29();
void isr30();
void isr31();

// IRQ stubs (32-47)
void irq0();
void irq1();
void irq2();
void irq3();
void irq4();
void irq5();
void irq6();
void irq7();
void irq8();
void irq9();
void irq10();
void irq11();
void irq12();
void irq13();
void irq14();
void irq15();

/**
 * Install all ISRs and IRQs in the IDT
 */
void irq_asm_install(void) {
    // Set up CPU exception handlers (ISRs 0-31)
    idt_set_gate(0, (uint32_t)isr0, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(1, (uint32_t)isr1, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(2, (uint32_t)isr2, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(3, (uint32_t)isr3, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(4, (uint32_t)isr4, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(5, (uint32_t)isr5, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(6, (uint32_t)isr6, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(7, (uint32_t)isr7, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(8, (uint32_t)isr8, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(9, (uint32_t)isr9, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(10, (uint32_t)isr10, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(11, (uint32_t)isr11, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(12, (uint32_t)isr12, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(13, (uint32_t)isr13, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(14, (uint32_t)isr14, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(15, (uint32_t)isr15, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(16, (uint32_t)isr16, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(17, (uint32_t)isr17, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(18, (uint32_t)isr18, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(19, (uint32_t)isr19, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(20, (uint32_t)isr20, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(21, (uint32_t)isr21, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(22, (uint32_t)isr22, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(23, (uint32_t)isr23, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(24, (uint32_t)isr24, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(25, (uint32_t)isr25, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(26, (uint32_t)isr26, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(27, (uint32_t)isr27, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(28, (uint32_t)isr28, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(29, (uint32_t)isr29, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(30, (uint32_t)isr30, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(31, (uint32_t)isr31, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    
    // Set up hardware IRQ handlers (mapped to ISRs 32-47)
    idt_set_gate(32, (uint32_t)irq0, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(33, (uint32_t)irq1, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(34, (uint32_t)irq2, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(35, (uint32_t)irq3, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(36, (uint32_t)irq4, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(37, (uint32_t)irq5, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(38, (uint32_t)irq6, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(39, (uint32_t)irq7, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(40, (uint32_t)irq8, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(41, (uint32_t)irq9, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(42, (uint32_t)irq10, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(43, (uint32_t)irq11, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(44, (uint32_t)irq12, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(45, (uint32_t)irq13, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(46, (uint32_t)irq14, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
    idt_set_gate(47, (uint32_t)irq15, CODE_SELECTOR, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_32BIT | IDT_FLAG_INTR);
}

/**
 * Common handler for CPU exceptions
 * This is called by the assembly stubs in irq.s
 */
void exception_common_handler(interrupt_frame_t* frame) {
    // If we have a registered handler for this exception, call it
    if (exception_handlers[frame->int_no] != NULL) {
        exception_handlers[frame->int_no](frame, frame->error_code);
    } else {
        // No handler registered, trigger a kernel panic
        PANIC(PANIC_UNEXPECTED_IRQ, 
              "Unhandled CPU Exception %d (%s) at 0x%08x",
              frame->int_no, 
              exception_get_name(frame->int_no),
              frame->eip);
    }
}
