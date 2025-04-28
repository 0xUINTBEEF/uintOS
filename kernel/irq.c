#include "irq.h"
#include "task.h"
#include "lapic.h"
#include "io.h"

/* --------- IDT Implementation ---------- */
static segment_descriptor uintos_interrupt_gates[UINTOS_IDT_SIZE];

uintos_idt_t uintos_interrupt_descriptor_table = {
    .base = uintos_interrupt_gates,
    .size = UINTOS_DESCRIPTOR_SIZE * UINTOS_IDT_SIZE,
    .next_id = 0
};

/* --------- Handler Tables ---------- */
// Array of function pointers for dynamically registered interrupt handlers
static void (*interrupt_handlers[256])() = {0};
static uintos_exception_handler_t exception_handlers[32] = {0};
static uintos_interrupt_handler_t irq_handlers[224] = {0};
static void (*nmi_handler_ptr)(void) = NULL;

/* --------- APIC Base Address ---------- */
static uint32_t ioapic_address = IOAPIC_DEFAULT_BASE;
static uint32_t apic_base_addr = 0;

/* --------- PIC Implementation ---------- */
/**
 * Initialize the legacy 8259 Programmable Interrupt Controller
 * 
 * @param offset1: Vector offset for master PIC
 * @param offset2: Vector offset for slave PIC
 */
void pic_init(uint8_t offset1, uint8_t offset2) {
    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
    
    // ICW1: Start initialization sequence in cascade mode
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    
    // ICW2: Set vector offsets
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);
    
    // ICW3: Tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC1_DATA, 4);
    // ICW3: Tell Slave PIC its cascade identity (0000 0010)
    outb(PIC2_DATA, 2);
    
    // ICW4: Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    
    // Restore saved masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/**
 * Send End-of-Interrupt signal to the PIC for the specified IRQ
 * 
 * @param irq: The IRQ number that has been serviced
 */
void pic_send_eoi(uint8_t irq) {
    // If it's a slave PIC IRQ, send EOI to both master and slave
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    
    // Always send EOI to master PIC
    outb(PIC1_COMMAND, PIC_EOI);
}

/**
 * Mask (disable) the specified IRQ
 * 
 * @param irq: The IRQ to mask
 */
void pic_set_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/**
 * Unmask (enable) the specified IRQ
 * 
 * @param irq: The IRQ to unmask
 */
void pic_clear_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

/**
 * Read the Interrupt Request Register (IRR) from the PICs
 * 
 * @return Combined IRR values from both PICs
 */
uint16_t pic_get_irr() {
    // Tell PICs we want to read the IRR
    outb(PIC1_COMMAND, PIC_READ_IRR);
    outb(PIC2_COMMAND, PIC_READ_IRR);
    
    // Read and combine the values
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/**
 * Read the In-Service Register (ISR) from the PICs
 * 
 * @return Combined ISR values from both PICs
 */
uint16_t pic_get_isr() {
    // Tell PICs we want to read the ISR
    outb(PIC1_COMMAND, PIC_READ_ISR);
    outb(PIC2_COMMAND, PIC_READ_ISR);
    
    // Read and combine the values
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/**
 * Disable the legacy PICs by masking all IRQs
 */
void pic_disable() {
    // Mask all interrupts
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/* --------- APIC Implementation ---------- */
/**
 * Check if the CPU supports APIC
 * 
 * @return 1 if APIC is supported, 0 otherwise
 */
int apic_supported() {
    uint32_t eax, edx;
    
    // Use CPUID to check APIC support
    asm volatile("cpuid" : "=a"(eax), "=d"(edx) : "a"(1) : "ebx", "ecx");
    
    // Bit 9 of EDX indicates APIC support
    return (edx & (1 << 9)) != 0;
}

/**
 * Enable the Local APIC
 */
void apic_enable() {
    uint32_t low, high;
    
    // Read the APIC base MSR
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(IA32_APIC_BASE_MSR));
    
    // Set the enable bit
    low |= APIC_ENABLE_BIT;
    
    // Write back the MSR
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(IA32_APIC_BASE_MSR));
    
    // Store the APIC base address
    apic_base_addr = low & APIC_BASE_ADDR_MASK;
    
    // Enable the APIC by setting bit 8 in the Spurious Interrupt Vector Register
    uint32_t svr = *(uint32_t*)(apic_base_addr + LAPIC_SVR);
    svr |= 0x100; // Set bit 8 to enable
    *(uint32_t*)(apic_base_addr + LAPIC_SVR) = svr;
}

/**
 * Disable the Local APIC
 */
void apic_disable() {
    uint32_t low, high;
    
    // Read the APIC base MSR
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(IA32_APIC_BASE_MSR));
    
    // Clear the enable bit
    low &= ~APIC_ENABLE_BIT;
    
    // Write back the MSR
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(IA32_APIC_BASE_MSR));
}

/**
 * Initialize the APIC
 */
void apic_init() {
    // First check if APIC is supported
    if (!apic_supported()) {
        return; // APIC not supported
    }
    
    // Disable legacy PIC
    pic_disable();
    
    // Enable APIC
    apic_enable();
    
    // Set Task Priority Register to accept all interrupts
    *(uint32_t*)(apic_base_addr + LAPIC_TPR) = 0;
}

/**
 * Read from IO APIC register
 * 
 * @param reg: The register to read
 * @return The value read from the register
 */
uint32_t ioapic_read(uint8_t reg) {
    *(uint32_t*)(ioapic_address) = reg;
    return *(uint32_t*)(ioapic_address + 0x10);
}

/**
 * Write to IO APIC register
 * 
 * @param reg: The register to write to
 * @param value: The value to write
 */
void ioapic_write(uint8_t reg, uint32_t value) {
    *(uint32_t*)(ioapic_address) = reg;
    *(uint32_t*)(ioapic_address + 0x10) = value;
}

/**
 * Initialize the IO APIC
 */
void ioapic_init() {
    // This is a simplified implementation. In a real OS, you would
    // discover IO APICs through ACPI tables.
    
    // Set up some basic IRQs
    // By default, route IRQ 0-15 to vectors 32-47
    for (int i = 0; i < 16; i++) {
        ioapic_set_irq(i, i, 0, 0);
    }
}

/**
 * Configure an IRQ in the IO APIC
 * 
 * @param irq: The IRQ number
 * @param gsi: The Global System Interrupt number
 * @param cpu: The CPU to route the interrupt to
 * @param flags: Flags for delivery mode, polarity, etc.
 */
void ioapic_set_irq(uint8_t irq, uint32_t gsi, uint8_t cpu, uint8_t flags) {
    uint32_t low = 32 + irq; // Vector number starting at 32
    uint32_t high = cpu << 24; // Destination field in high dword
    
    // Set up low dword of redirection entry
    // 0-7: Vector
    // 8-10: Delivery mode (000: fixed)
    // 11: Destination mode (0: physical)
    // 12: Delivery status (0: idle)
    // 13: Polarity (0: active high)
    // 14: Remote IRR (0: not used)
    // 15: Trigger mode (0: edge, 1: level)
    // 16: Mask (0: enabled)
    
    // Apply flags
    if (flags & 1) low |= (1 << 15); // Level triggered
    if (flags & 2) low |= (1 << 13); // Active low
    
    // Write to IO APIC redirection table
    ioapic_write(IOAPIC_REDTBL_BASE + gsi * 2, low);
    ioapic_write(IOAPIC_REDTBL_BASE + gsi * 2 + 1, high);
}

/* --------- NMI Implementation ---------- */
/**
 * Enable NMI
 */
void nmi_enable() {
    // Clear the NMI disable bit
    outb(NMI_ENABLE_PORT, inb(NMI_ENABLE_PORT) & ~NMI_DISABLE_BIT);
}

/**
 * Disable NMI
 */
void nmi_disable() {
    // Set the NMI disable bit
    outb(NMI_ENABLE_PORT, inb(NMI_ENABLE_PORT) | NMI_DISABLE_BIT);
}

/**
 * Get NMI status
 * 
 * @return Status byte from NMI controller
 */
uint8_t nmi_get_status() {
    return inb(NMI_REASON_PORT);
}

/**
 * Register a handler for NMI
 * 
 * @param handler: Function pointer to NMI handler
 */
void nmi_register_handler(void (*handler)(void)) {
    nmi_handler_ptr = handler;
}

/* --------- Handler Registration ---------- */
/**
 * Register an exception handler
 * 
 * @param exception: Exception number (0-31)
 * @param handler: Handler function pointer
 */
void register_exception_handler(uint8_t exception, uintos_exception_handler_t handler) {
    if (exception < 32) {
        exception_handlers[exception] = handler;
    }
}

/**
 * Register a general interrupt handler
 * 
 * @param irq_number: The IRQ number
 * @param handler: Handler function pointer
 */
void register_interrupt_handler(uint8_t irq_number, void (*handler)()) {
    if (irq_number < 256) {
        interrupt_handlers[irq_number] = handler;
    }
}

/**
 * Register an IRQ handler
 * 
 * @param irq: The IRQ number (0-223)
 * @param handler: Handler function pointer
 */
void register_irq_handler(uint8_t irq, uintos_interrupt_handler_t handler) {
    if (irq < 224) {
        irq_handlers[irq] = handler;
    }
}

/* --------- IRQ Initialization ---------- */
/**
 * Initialize the interrupt system
 */
void uintos_initialize_interrupts() {
    // Initialize predefined IRQ handlers
    uintos_init_irq(uintos_irq0, EXC_DIVIDE_ERROR);
    uintos_init_irq(uintos_irq1, EXC_DEBUG);
    uintos_init_irq(uintos_irq2, EXC_NMI);
    uintos_init_irq(uintos_irq3, EXC_BREAKPOINT);
    uintos_init_irq(uintos_irq4, EXC_OVERFLOW);
    uintos_init_irq(uintos_irq8, EXC_DOUBLE_FAULT);
    uintos_init_irq(uintos_irq10, EXC_INVALID_TSS);
    uintos_init_irq(uintos_irq11, EXC_SEGMENT_NOT_PRES);
    uintos_init_irq(uintos_irq13, EXC_GENERAL_PROTECT);
    uintos_init_irq(uintos_irq14, EXC_PAGE_FAULT);
    uintos_init_irq(uintos_irq32, 32); // Timer IRQ

    // Initialize PIC
    pic_init(0x20, 0x28);  // Map IRQs 0-7 to vectors 0x20-0x27, IRQs 8-15 to 0x28-0x2F
    
    // If APIC is supported, initialize it
    if (apic_supported()) {
        apic_init();
        ioapic_init();
    }
    
    // Enable NMI
    nmi_enable();
    
    // Load the IDT
    UINTOS_LOAD_IDT(uintos_interrupt_descriptor_table);
}

/* --------- IRQ Handlers ---------- */
UINTOS_TASK_START(uintos_irq0, uintos_handle_divide_by_zero);
void uintos_handle_divide_by_zero() {
    // Handle divide by zero error
    if (exception_handlers[EXC_DIVIDE_ERROR]) {
        exception_handlers[EXC_DIVIDE_ERROR](0, NULL);
    } else {
        // Default handler: Log the divide by zero error and display a proper error message
        uint8_t old_color = vga_current_color;
        
        // Use red on black for the error message
        vga_set_color(vga_entry_color(VGA_COLOR_RED, VGA_COLOR_BLACK));
        
        // Display a descriptive error message
        vga_write_string("\nCPU EXCEPTION: Divide By Zero Error\n");
        
        // Get current instruction pointer from the stack
        uint32_t eip = 0;
        asm volatile("mov %0, [ebp + 8]" : "=r"(eip) : : "memory");
        
        // Convert EIP to string format for display
        char addr_str[16];
        for (int i = 0; i < 8; i++) {
            uint8_t nibble = (eip >> ((7-i) * 4)) & 0xF;
            addr_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        addr_str[8] = 0;
        
        // Display error information
        vga_write_string("Instruction address: 0x");
        vga_write_string(addr_str);
        vga_write_string("\n");
        
        // Possibly dump registers or other diagnostic information here
        vga_write_string("System halted - CPU cannot continue execution\n");
        
        // Restore the original color
        vga_set_color(old_color);
        
        // In a production OS we would halt the current process/thread
        // or potentially kernel panic if this happens in kernel mode
        while (1) {
            // Halt the CPU until an interrupt occurs
            asm volatile("hlt");
        }
    }
    
    // Return from the interrupt without recursion
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq0);

UINTOS_TASK_START(uintos_irq1, uintos_handle_debug_exception);
void uintos_handle_debug_exception() {
    // Handle debug exception
    if (exception_handlers[EXC_DEBUG]) {
        exception_handlers[EXC_DEBUG](0, NULL);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq1);

UINTOS_TASK_START(uintos_irq2, uintos_handle_nmi);
void uintos_handle_nmi() {
    // Handle Non-Maskable Interrupt
    uint8_t status = nmi_get_status();
    
    // Call the registered NMI handler if available
    if (nmi_handler_ptr != NULL) {
        nmi_handler_ptr();
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq2);

UINTOS_TASK_START(uintos_irq3, uintos_handle_breakpoint);
void uintos_handle_breakpoint() {
    // Handle breakpoint exception
    if (exception_handlers[EXC_BREAKPOINT]) {
        exception_handlers[EXC_BREAKPOINT](0, NULL);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq3);

UINTOS_TASK_START(uintos_irq4, uintos_handle_overflow);
void uintos_handle_overflow() {
    // Handle overflow exception
    if (exception_handlers[EXC_OVERFLOW]) {
        exception_handlers[EXC_OVERFLOW](0, NULL);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq4);

UINTOS_TASK_START(uintos_irq8, uintos_handle_double_fault);
void uintos_handle_double_fault(uint32_t error_code) {
    // Handle double fault with the error code
    // This is a serious error condition that indicates another fault occurred 
    // during handling of a previous fault
    
    if (exception_handlers[EXC_DOUBLE_FAULT]) {
        exception_handlers[EXC_DOUBLE_FAULT](error_code, NULL);
    }
    
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
    if (exception_handlers[EXC_INVALID_TSS]) {
        exception_handlers[EXC_INVALID_TSS](error_code, NULL);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq10);

UINTOS_TASK_START(uintos_irq11, uintos_handle_segment_not_present);
void uintos_handle_segment_not_present(uint32_t error_code) {
    // Handle segment not present error with the provided error code
    if (exception_handlers[EXC_SEGMENT_NOT_PRES]) {
        exception_handlers[EXC_SEGMENT_NOT_PRES](error_code, NULL);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq11);

UINTOS_TASK_START(uintos_irq13, uintos_handle_general_protection);
void uintos_handle_general_protection() {
    // Get error code using safer approach
    uint32_t error_code;
    asm volatile("mov %0, [ebp + 4]" : "=r"(error_code) : : "memory");
    
    // Handle general protection fault
    if (exception_handlers[EXC_GENERAL_PROTECT]) {
        exception_handlers[EXC_GENERAL_PROTECT](error_code, NULL);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq13);

UINTOS_TASK_START(uintos_irq14, uintos_handle_page_fault);
void uintos_handle_page_fault(uint32_t error_code) {
    // Get the faulting address from CR2 register
    uint32_t faulting_address;
    asm volatile("mov %0, cr2" : "=r"(faulting_address));
    
    // Handle page fault
    if (exception_handlers[EXC_PAGE_FAULT]) {
        // Pass faulting address in context
        exception_handlers[EXC_PAGE_FAULT](error_code, (void*)faulting_address);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq14);

UINTOS_TASK_START(uintos_irq32, uintos_handle_lapic_timer);
int uintos_active_task_id = 1;
void uintos_handle_lapic_timer() {
    // Simple round-robin task switching between two tasks
    uintos_active_task_id = (uintos_active_task_id == 1) ? 2 : 1;

    // Acknowledge the interrupt
    uintos_lapic_isr_complete();
    
    // Call registered timer handler if available
    if (irq_handlers[0]) {
        irq_handlers[0](NULL);
    }
    
    // Return from the interrupt handler
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq32);

UINTOS_TASK_START(uintos_irq20, uintos_handle_double_fault_alt);
void uintos_handle_double_fault_alt(uint32_t error_code) {
    // Alternative double fault handler with improved diagnostics and error handling
    // A double fault is a critical error that occurs when the CPU fails to invoke
    // an exception handler for a prior exception
    
    uint8_t old_color = vga_current_color;
    
    // Use bright red on black for critical error message
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    
    // Display detailed error information
    vga_write_string("\nCRITICAL ERROR: Double Fault Exception (Alt Handler)\n");
    vga_write_string("Error code: ");
    
    // Convert error code to string
    char err_str[9];
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (error_code >> ((7-i) * 4)) & 0xF;
        err_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    err_str[8] = 0;
    vga_write_string(err_str);
    vga_write_string("\n");
    
    // Get instruction pointer from stack
    uint32_t eip = 0;
    asm volatile("mov %0, [ebp + 8]" : "=r"(eip) : : "memory");
    
    // Convert EIP to string
    char addr_str[9];
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (eip >> ((7-i) * 4)) & 0xF;
        addr_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    addr_str[8] = 0;
    
    vga_write_string("Instruction address: 0x");
    vga_write_string(addr_str);
    vga_write_string("\n");
    
    // Display explanation
    vga_write_string("\nA double fault indicates that the system encountered a serious error\n");
    vga_write_string("while attempting to handle another exception. This typically means\n");
    vga_write_string("the system is in an unstable state and cannot continue execution.\n");
    
    // Dump some system state
    vga_write_string("\nSYSTEM HALTED\n");
    
    // Restore color
    vga_set_color(old_color);
    
    // Disable interrupts and halt the CPU
    asm volatile("cli");  // Clear interrupt flag
    while (1) {
        asm volatile("hlt");
    }
    
    // Note: This code is never reached due to the infinite loop above
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq20);
