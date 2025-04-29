#ifndef IRQH
#define IRQH

#include "gdt.h"
#include "asm.h"
#include "task.h"
#include "lapic.h"
#include <inttypes.h>

/* --------- IDT Definitions ---------- */
#define UINTOS_IDT_SIZE 256
#define UINTOS_IDT_ENTRY_SIZE UINTOS_GDT_ENTRY_SIZE

typedef descriptor_table uintos_idt_t;

extern uintos_idt_t uintos_interrupt_descriptor_table;

/* --------- Interrupt Gate Types -------- */
#define IDT_TASK_GATE      0x5  // 32-bit task gate
#define IDT_INTERRUPT_GATE 0xE  // 32-bit interrupt gate
#define IDT_TRAP_GATE      0xF  // 32-bit trap gate

/* --------- Interrupt Gate Attributes -------- */
#define IDT_PRESENT        0x80 // Present bit
#define IDT_DPL0           0x00 // Privilege level 0 (kernel)
#define IDT_DPL3           0x60 // Privilege level 3 (user)
#define IDT_STORAGE        0x10 // Storage segment (used for LDT segments)

#define UINTOS_ISEG_ACCESS(access) (access << 8)
/* t = TSS segment selector */
#define uintos_idt_add_gate(t, access, id) \
    add_segment(&uintos_interrupt_descriptor_table, t, 0x0000, UINTOS_ISEG_ACCESS(access), id, 0)

#define uintos_init_irq(irq_name, id) \
    UINTOS_INIT_TASK(irq_name); \
    uintos_idt_add_gate(UINTOS_TASK_SELECTOR(irq_name), 0x85, id)

/* --------- CPU Exception Definitions -------- */
// Intel defined CPU exceptions
#define EXC_DIVIDE_ERROR      0  // Division by zero
#define EXC_DEBUG             1  // Debug exception
#define EXC_NMI               2  // Non-maskable interrupt
#define EXC_BREAKPOINT        3  // Breakpoint
#define EXC_OVERFLOW          4  // Overflow
#define EXC_BOUND_RANGE       5  // BOUND Range Exceeded
#define EXC_INVALID_OPCODE    6  // Invalid opcode
#define EXC_DEVICE_NOT_AVAIL  7  // Device not available
#define EXC_DOUBLE_FAULT      8  // Double fault
#define EXC_COPROC_SEG_OVERR  9  // Coprocessor segment overrun (reserved)
#define EXC_INVALID_TSS       10 // Invalid TSS
#define EXC_SEGMENT_NOT_PRES  11 // Segment not present
#define EXC_STACK_SEGMENT     12 // Stack-segment fault
#define EXC_GENERAL_PROTECT   13 // General protection fault
#define EXC_PAGE_FAULT        14 // Page fault
#define EXC_RESERVED_15       15 // Reserved
#define EXC_FPU_ERROR         16 // x87 FPU error
#define EXC_ALIGNMENT_CHECK   17 // Alignment check
#define EXC_MACHINE_CHECK     18 // Machine check
#define EXC_SIMD_EXCEPTION    19 // SIMD floating-point exception
#define EXC_VIRT_EXCEPTION    20 // Virtualization exception
#define EXC_CONTROL_PROTECT   21 // Control protection exception
#define EXC_RESERVED_22       22 // Reserved
#define EXC_RESERVED_23       23 // Reserved
#define EXC_RESERVED_24       24 // Reserved
#define EXC_RESERVED_25       25 // Reserved
#define EXC_RESERVED_26       26 // Reserved
#define EXC_RESERVED_27       27 // Reserved
#define EXC_RESERVED_28       28 // Reserved
#define EXC_RESERVED_29       29 // Reserved
#define EXC_RESERVED_30       30 // Reserved
#define EXC_RESERVED_31       31 // Reserved

/* --------- PIC Definitions -------- */
// Legacy 8259 PIC definitions
#define PIC1_COMMAND          0x20
#define PIC1_DATA             0x21
#define PIC2_COMMAND          0xA0
#define PIC2_DATA             0xA1

// PIC commands
#define PIC_EOI               0x20 // End of interrupt
#define PIC_READ_IRR          0x0A // Read Interrupt Request Register
#define PIC_READ_ISR          0x0B // Read In-Service Register

// PIC initialization command words
#define ICW1_ICW4             0x01 // ICW4 needed
#define ICW1_SINGLE           0x02 // Single mode
#define ICW1_INTERVAL4        0x04 // Call address interval 4
#define ICW1_LEVEL            0x08 // Level triggered mode
#define ICW1_INIT             0x10 // Initialization command

#define ICW4_8086             0x01 // 8086/88 mode
#define ICW4_AUTO             0x02 // Auto EOI
#define ICW4_BUF_SLAVE        0x08 // Buffered mode/slave
#define ICW4_BUF_MASTER       0x0C // Buffered mode/master
#define ICW4_SFNM             0x10 // Special fully nested mode

// Legacy PIC IRQ definitions
#define IRQ_PIC_TIMER         0
#define IRQ_PIC_KEYBOARD      1
#define IRQ_PIC_CASCADE       2
#define IRQ_PIC_COM2          3
#define IRQ_PIC_COM1          4
#define IRQ_PIC_LPT2          5
#define IRQ_PIC_FLOPPY        6
#define IRQ_PIC_LPT1          7
#define IRQ_PIC_CMOS_RTC      8
#define IRQ_PIC_PS2_MOUSE     12
#define IRQ_PIC_FPU           13
#define IRQ_PIC_ATA_PRIMARY   14
#define IRQ_PIC_ATA_SECONDARY 15

/* --------- APIC Definitions -------- */
// APIC Base MSR (IA32_APIC_BASE MSR)
#define IA32_APIC_BASE_MSR        0x1B
#define APIC_BASE_ADDR_MASK       0xFFFFF000
#define APIC_ENABLE_BIT           0x800
#define APIC_BSP_BIT              0x100

// Local APIC register offsets (from APIC base)
#define LAPIC_ID                  0x020  // Local APIC ID Register
#define LAPIC_VERSION             0x030  // Local APIC Version Register
#define LAPIC_TPR                 0x080  // Task Priority Register
#define LAPIC_APR                 0x090  // Arbitration Priority Register
#define LAPIC_PPR                 0x0A0  // Processor Priority Register
#define LAPIC_EOI                 0x0B0  // EOI Register
#define LAPIC_RRD                 0x0C0  // Remote Read Register
#define LAPIC_LDR                 0x0D0  // Logical Destination Register
#define LAPIC_DFR                 0x0E0  // Destination Format Register
#define LAPIC_SVR                 0x0F0  // Spurious Interrupt Vector Register
#define LAPIC_ISR                 0x100  // In-Service Register
#define LAPIC_TMR                 0x180  // Trigger Mode Register
#define LAPIC_IRR                 0x200  // Interrupt Request Register
#define LAPIC_ESR                 0x280  // Error Status Register
#define LAPIC_ICRL                0x300  // Interrupt Command Register Low
#define LAPIC_ICRH                0x310  // Interrupt Command Register High
#define LAPIC_TIMER               0x320  // LVT Timer Register
#define LAPIC_THERMAL             0x330  // LVT Thermal Sensor Register
#define LAPIC_PERF                0x340  // LVT Performance Counter Register
#define LAPIC_LINT0               0x350  // LVT LINT0 Register
#define LAPIC_LINT1               0x360  // LVT LINT1 Register
#define LAPIC_ERROR               0x370  // LVT Error Register
#define LAPIC_TICR                0x380  // Timer Initial Count Register
#define LAPIC_TCCR                0x390  // Timer Current Count Register
#define LAPIC_TDCR                0x3E0  // Timer Divide Configuration Register

// IO APIC definitions
#define IOAPIC_ID_REG             0x00
#define IOAPIC_VER_REG            0x01
#define IOAPIC_ARB_REG            0x02
#define IOAPIC_REDTBL_BASE        0x10
#define IOAPIC_DEFAULT_BASE       0xFEC00000

/* --------- Open PIC Definitions -------- */
// Open Programmable Interrupt Controller (typically used in PowerPC systems)
#define OPIC_VENDOR_ID            0x00
#define OPIC_FEATURE_REG          0x01
#define OPIC_GLOBAL_CONF_REG      0x02

/* --------- NMI Handling -------- */
// Non-maskable interrupt handling
#define NMI_DISABLE_BIT           0x80
#define NMI_ENABLE_PORT           0x70
#define NMI_REASON_PORT           0x71

// NMI sources
#define NMI_SRC_PARITY            0x01
#define NMI_SRC_IO_CHECK          0x02
#define NMI_SRC_WATCHDOG          0x04
#define NMI_SRC_PCI_SERR          0x08

/* --------- IRQ Routing Structure -------- */
typedef struct {
    uint8_t irq;          // IRQ number
    uint8_t gsi;          // Global System Interrupt number
    uint8_t active_low;   // Polarity: 1 if active low, 0 if active high
    uint8_t level;        // Trigger mode: 1 if level triggered, 0 if edge triggered
} irq_routing_entry_t;

/* --------- Function Type Definitions -------- */
typedef void (*uintos_irq_handler)(uint32_t error_code);
typedef void (*uintos_exception_handler_t)(uint32_t error_code, void* context);
typedef void (*uintos_interrupt_handler_t)(void* context);

/* --------- Public Function Declarations -------- */
// General interrupt initialization
void uintos_initialize_interrupts();

// PIC functions
void pic_init(uint8_t offset1, uint8_t offset2);
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
uint16_t pic_get_irr();
uint16_t pic_get_isr();
void pic_disable();

// APIC functions
int apic_supported();
void apic_init();
void apic_enable();
void apic_disable();
void ioapic_init();
void ioapic_set_irq(uint8_t irq, uint32_t gsi, uint8_t cpu, uint8_t flags);
uint32_t ioapic_read(uint8_t reg);
void ioapic_write(uint8_t reg, uint32_t value);

// NMI functions
void nmi_enable();
void nmi_disable();
uint8_t nmi_get_status();
void nmi_register_handler(void (*handler)(void));

// Exception/IRQ registration
void register_exception_handler(uint8_t exception, uintos_exception_handler_t handler);
void register_interrupt_handler(uint8_t irq_number, void (*handler)());
void register_irq_handler(uint8_t irq, uintos_interrupt_handler_t handler);

// Predefined IRQ handlers
UINTOS_TASK_REGISTER(uintos_irq0);
void uintos_handle_divide_by_zero();

UINTOS_TASK_REGISTER(uintos_irq1);
void uintos_handle_debug_exception();

UINTOS_TASK_REGISTER(uintos_irq2);
void uintos_handle_nmi();

UINTOS_TASK_REGISTER(uintos_irq3);
void uintos_handle_breakpoint();

UINTOS_TASK_REGISTER(uintos_irq4);
void uintos_handle_overflow();

UINTOS_TASK_REGISTER(uintos_irq8);
void uintos_handle_double_fault(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq10);
void uintos_handle_invalid_tss();

UINTOS_TASK_REGISTER(uintos_irq11);
void uintos_handle_segment_not_present(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq13);
void uintos_handle_general_protection();

UINTOS_TASK_REGISTER(uintos_irq14);
void uintos_handle_page_fault(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq20);
void uintos_handle_double_fault_alt(uint32_t error_code);

UINTOS_TASK_REGISTER(uintos_irq32);
void uintos_handle_lapic_timer();

// Interrupt return macro used in handlers
#define UINTOS_INTERRUPT_RETURN() asm volatile("iret");

/* --------- Enhanced IRQ Management Definitions -------- */
#define MAX_IRQ_PRIORITY_LEVELS 16
#define MAX_IRQ_HANDLERS_PER_VECTOR 4

typedef enum {
    IRQ_PRIORITY_HIGHEST = 0,
    IRQ_PRIORITY_HIGH = 4,
    IRQ_PRIORITY_MEDIUM = 8,
    IRQ_PRIORITY_LOW = 12,
    IRQ_PRIORITY_LOWEST = 15
} uintos_irq_priority_t;

typedef enum {
    IRQ_RESULT_HANDLED = 0,       // IRQ was fully handled
    IRQ_RESULT_UNHANDLED = 1,     // IRQ was not handled
    IRQ_RESULT_PASS = 2,          // IRQ was handled but should be passed to next handler
    IRQ_RESULT_ERROR = 3          // Error occurred during handling
} uintos_irq_result_t;

typedef uintos_irq_result_t (*uintos_enhanced_irq_handler_t)(uint32_t irq, void* context);

typedef struct {
    uintos_enhanced_irq_handler_t handler;
    uintos_irq_priority_t priority;
    void* context;
    uint32_t flags;
    const char* name;  // Name/description of handler for debugging
} uintos_irq_handler_entry_t;

/* --------- Enhanced IRQ Function Declarations -------- */
// Enhanced IRQ registration with priorities and chaining
uintos_irq_result_t register_enhanced_irq_handler(uint8_t irq, uintos_enhanced_irq_handler_t handler, 
                                                 uintos_irq_priority_t priority, void* context, 
                                                 uint32_t flags, const char* name);
                                                 
uintos_irq_result_t unregister_irq_handler(uint8_t irq, uintos_enhanced_irq_handler_t handler);

// IRQ statistics and debugging
void irq_get_statistics(uint8_t irq, uint32_t* count, uint32_t* time_spent);
void irq_reset_statistics(uint8_t irq);
void irq_dump_handlers(uint8_t irq);
const char* irq_get_name(uint8_t irq);

// IRQ control functions
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);
uint8_t irq_is_enabled(uint8_t irq);
void irq_mask_all(void);
void irq_unmask_all(void);

// Spurious IRQ handling
void irq_register_spurious_handler(void (*handler)(uint8_t irq));

#endif /* IRQH */
