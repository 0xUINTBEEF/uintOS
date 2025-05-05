#include "irq.h"
#include "task.h"
#include "lapic.h"
#include "io.h"
#include "logging/log.h" // Include the new logging system

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
    // Initialize the logging system first for better debugging
    log_init(LOG_LEVEL_INFO, LOG_DEST_SCREEN | LOG_DEST_MEMORY, LOG_FORMAT_LEVEL | LOG_FORMAT_SOURCE);
    log_info("IRQ", "Initializing interrupt system");
    
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
    log_info("IRQ", "Initializing PIC with vectors 0x20-0x2F");
    pic_init(0x20, 0x28);  // Map IRQs 0-7 to vectors 0x20-0x27, IRQs 8-15 to 0x28-0x2F
    
    // If APIC is supported, initialize it
    if (apic_supported()) {
        log_info("IRQ", "APIC supported, initializing APIC");
        apic_init();
        ioapic_init();
    } else {
        log_info("IRQ", "APIC not supported, using legacy PIC");
    }
    
    // Enable NMI
    nmi_enable();
    log_debug("IRQ", "NMI enabled");
    
    // Load the IDT
    log_info("IRQ", "Loading Interrupt Descriptor Table");
    UINTOS_LOAD_IDT(uintos_interrupt_descriptor_table);
    
    log_info("IRQ", "Interrupt system initialization complete");
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
    } else {
        // Use the kernel panic system for double faults
        // Include panic.h at the top of the file
        #include "panic.h"
        
        // Trigger kernel panic with double fault type
        PANIC(PANIC_DOUBLE_FAULT, 
              "A double fault has occurred (error code: 0x%x). "
              "This indicates that the system encountered a serious error while "
              "attempting to handle another exception.", 
              error_code);
        
        // This code is never reached due to the panic
    }
    
    // In a production OS, we would halt the system
    // This is also never reached due to the above PANIC call
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
    // Enhanced error handling with detailed diagnostics
    uint32_t selector_index = error_code & 0xFFF8;  // Extract the selector index (bits 3-15)
    uint8_t table = (error_code & 0x0006) >> 1;     // Extract table indicator (bits 1-2)
    uint8_t external = error_code & 0x0001;         // Extract external bit
    
    // If a registered handler exists, call it with the error code
    if (exception_handlers[EXC_SEGMENT_NOT_PRES]) {
        exception_handlers[EXC_SEGMENT_NOT_PRES](error_code, NULL);
    } else {
        // Default handler with detailed diagnostic information
        uint8_t old_color = vga_current_color;
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        
        vga_write_string("\nCPU EXCEPTION: Segment Not Present\n");
        vga_write_string("Error Code: 0x");
        
        // Convert error code to hex string
        char err_str[9];
        for (int i = 0; i < 8; i++) {
            uint8_t nibble = (error_code >> ((7-i) * 4)) & 0xF;
            err_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        err_str[8] = 0;
        vga_write_string(err_str);
        vga_write_string("\n");
        
        // Display specific details about the error
        vga_write_string("Selector Index: ");
        char index_str[5];
        for (int i = 0; i < 4; i++) {
            uint8_t nibble = (selector_index >> ((3-i) * 4)) & 0xF;
            index_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        index_str[4] = 0;
        vga_write_string(index_str);
        vga_write_string("\n");
        
        vga_write_string("Table: ");
        switch(table) {
            case 0: vga_write_string("GDT"); break;
            case 2: vga_write_string("LDT"); break;
            case 1: case 3: vga_write_string("IDT"); break;
        }
        vga_write_string("\n");
        
        vga_write_string("Source: ");
        vga_write_string(external ? "External program" : "Processor");
        vga_write_string("\n");
        
        // Restore color
        vga_set_color(old_color);
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq11);

UINTOS_TASK_START(uintos_irq13, uintos_handle_general_protection);
void uintos_handle_general_protection() {
    // Get error code from the stack
    uint32_t error_code;
    asm volatile("mov %0, [esp + 4]" : "=r"(error_code) : : "memory");
    
    // Handle general protection fault with detailed diagnostics
    if (exception_handlers[EXC_GENERAL_PROTECT]) {
        exception_handlers[EXC_GENERAL_PROTECT](error_code, NULL);
    } else {
        // Default handler with comprehensive diagnostic information
        uint8_t old_color = vga_current_color;
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        
        vga_write_string("\nCPU EXCEPTION: General Protection Fault\n");
        vga_write_string("Error Code: 0x");
        
        // Convert error code to hex string
        char err_str[9];
        for (int i = 0; i < 8; i++) {
            uint8_t nibble = (error_code >> ((7-i) * 4)) & 0xF;
            err_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        err_str[8] = 0;
        vga_write_string(err_str);
        vga_write_string("\n");
        
        // Extract the segment selector index from the error code
        uint32_t selector_index = error_code & 0xFFF8;
        uint8_t table = (error_code & 0x0006) >> 1;
        uint8_t external = error_code & 0x0001;
        
        if (error_code != 0) {
            // Display information about the selector that caused the fault
            vga_write_string("Selector Index: ");
            char index_str[5];
            for (int i = 0; i < 4; i++) {
                uint8_t nibble = (selector_index >> ((3-i) * 4)) & 0xF;
                index_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
            }
            index_str[4] = 0;
            vga_write_string(index_str);
            vga_write_string("\n");
            
            vga_write_string("Table: ");
            switch(table) {
                case 0: vga_write_string("GDT"); break;
                case 2: vga_write_string("LDT"); break;
                case 1: case 3: vga_write_string("IDT"); break;
            }
            vga_write_string("\n");
            
            vga_write_string("Source: ");
            vga_write_string(external ? "External program" : "Processor");
            vga_write_string("\n");
        }
        
        // Get instruction pointer and code segment from the stack
        uint32_t eip, cs;
        asm volatile(
            "mov %0, [esp + 8]\n\t"  // EIP is at esp+8
            "mov %1, [esp + 12]"     // CS is at esp+12
            : "=r"(eip), "=r"(cs)
            : : "memory"
        );
        
        // Display fault information
        vga_write_string("Fault Address: 0x");
        char eip_str[9];
        for (int i = 0; i < 8; i++) {
            uint8_t nibble = (eip >> ((7-i) * 4)) & 0xF;
            eip_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        eip_str[8] = 0;
        vga_write_string(eip_str);
        
        vga_write_string(" in segment 0x");
        char cs_str[5];
        for (int i = 0; i < 4; i++) {
            uint8_t nibble = (cs >> ((3-i) * 4)) & 0xF;
            cs_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        }
        cs_str[4] = 0;
        vga_write_string(cs_str);
        vga_write_string("\n");
        
        vga_write_string("\nPossible causes:\n");
        vga_write_string("- Segment limit exceeded\n");
        vga_write_string("- Executing privileged instruction in user mode\n");
        vga_write_string("- Writing to read-only memory\n");
        vga_write_string("- Null pointer dereference\n");
        
        // Restore color
        vga_set_color(old_color);
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
    } else {
        // Include panic.h
        #include "panic.h"
        
        // Determine if this is a critical page fault that should cause a kernel panic
        int is_critical = 0;
        char fault_details[256] = {0};
        
        // Null pointer dereference
        if (faulting_address < 0x1000) {
            is_critical = 1;
            strcat(fault_details, "Null pointer dereference");
        } 
        // Kernel address space violation
        else if ((error_code & 0x04) && (faulting_address >= 0xC0000000)) { 
            is_critical = 1;
            strcat(fault_details, "User mode access to kernel memory");
        }
        // Reserved bits violation
        else if (error_code & 0x08) {
            is_critical = 1;
            strcat(fault_details, "Reserved bits set in page table");
        }
        // Stack overflow detection (simplified)
        else if ((error_code & 0x02) && (faulting_address >= 0xBFFFF000)) {
            is_critical = 1;
            strcat(fault_details, "Possible stack overflow");
        }
        
        // Build detailed error description
        strcat(fault_details, is_critical ? ". " : " or ");
        
        if (error_code & 0x01) {
            strcat(fault_details, "Page protection violation");
        } else {
            strcat(fault_details, "Non-present page");
        }
        
        strcat(fault_details, ", ");
        if (error_code & 0x02) {
            strcat(fault_details, "write operation");
        } else {
            strcat(fault_details, "read operation");
        }
        
        if (error_code & 0x10) {
            strcat(fault_details, ", during instruction fetch");
        }
        
        // For critical page faults, trigger kernel panic
        if (is_critical) {
            // Get instruction pointer from the stack
            uint32_t eip;
            asm volatile("mov %0, [esp + 8]" : "=r"(eip) : : "memory");
            
            PANIC(PANIC_PAGE_FAULT, 
                  "Fatal page fault at address 0x%08x (EIP: 0x%08x). %s", 
                  faulting_address, eip, fault_details);
            // Never returns
        } else {
            // Enhanced default handler with detailed diagnostic information
            uint8_t old_color = vga_current_color;
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            
            vga_write_string("\nCPU EXCEPTION: Page Fault\n");
            
            // Convert faulting address to string
            char addr_str[9];
            for (int i = 0; i < 8; i++) {
                uint8_t nibble = (faulting_address >> ((7-i) * 4)) & 0xF;
                addr_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
            }
            addr_str[8] = 0;
            
            vga_write_string("Faulting Address: 0x");
            vga_write_string(addr_str);
            vga_write_string("\n");
            
            // Display fault details
            vga_write_string("Fault Details: ");
            vga_write_string(fault_details);
            vga_write_string("\n");
            
            // Get instruction pointer and code segment from the stack
            uint32_t eip, cs;
            asm volatile(
                "mov %0, [esp + 8]\n\t"  // EIP is at esp+8
                "mov %1, [esp + 12]"     // CS is at esp+12
                : "=r"(eip), "=r"(cs)
                : : "memory"
            );
            
            // Display code address info
            vga_write_string("Code Location: 0x");
            char eip_str[9];
            for (int i = 0; i < 8; i++) {
                uint8_t nibble = (eip >> ((7-i) * 4)) & 0xF;
                eip_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
            }
            eip_str[8] = 0;
            vga_write_string(eip_str);
            
            vga_write_string(" in segment 0x");
            char cs_str[5];
            for (int i = 0; i < 4; i++) {
                uint8_t nibble = (cs >> ((3-i) * 4)) & 0xF;
                cs_str[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
            }
            cs_str[4] = 0;
            vga_write_string(cs_str);
            vga_write_string("\n");
            
            // Restore color
            vga_set_color(old_color);
        }
    }
    
    UINTOS_INTERRUPT_RETURN();
}
UINTOS_TASK_END(uintos_irq14);

UINTOS_TASK_START(uintos_irq32, uintos_handle_lapic_timer);
int uintos_active_task_id = 1;
void uintos_handle_lapic_timer() {
    // Preemptive task scheduling - this will be called on every timer tick
    switch_task();

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

/* --------- Enhanced IRQ Management Implementation ---------- */

// IRQ handler table for the enhanced handlers
static uintos_irq_handler_entry_t enhanced_irq_handlers[256][MAX_IRQ_HANDLERS_PER_VECTOR];

// IRQ statistics tracking
static uint32_t irq_statistics_count[256] = {0};
static uint32_t irq_statistics_time[256] = {0};
static uint8_t irq_enabled_status[256] = {0};

// Spurious IRQ handler
static void (*spurious_irq_handler)(uint8_t irq) = NULL;

// IRQ names for debugging
static const char* irq_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", 
    "Overflow", "BOUND Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Segment Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Exception",
    "Virtualization Exception", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

/**
 * Register an enhanced IRQ handler with priority and context
 */
uintos_irq_result_t register_enhanced_irq_handler(uint8_t irq, uintos_enhanced_irq_handler_t handler, 
                                                uintos_irq_priority_t priority, void* context, 
                                                uint32_t flags, const char* name) {
    // Ensure IRQ number and priority are valid
    if (irq >= 256 || priority >= MAX_IRQ_PRIORITY_LEVELS) {
        log_error("IRQ", "Failed to register handler: invalid IRQ %d or priority %d", irq, priority);
        return IRQ_RESULT_ERROR;
    }
    
    // Find an empty slot or a lower priority slot
    int slot = -1;
    for (int i = 0; i < MAX_IRQ_HANDLERS_PER_VECTOR; i++) {
        if (enhanced_irq_handlers[irq][i].handler == NULL) {
            // Found an empty slot
            slot = i;
            break;
        }
        else if (enhanced_irq_handlers[irq][i].priority > priority) {
            // Found a lower priority handler, shift everything down
            for (int j = MAX_IRQ_HANDLERS_PER_VECTOR - 1; j > i; j--) {
                enhanced_irq_handlers[irq][j] = enhanced_irq_handlers[irq][j-1];
            }
            slot = i;
            break;
        }
    }
    
    // If no slot found, return error
    if (slot == -1) {
        log_error("IRQ", "Failed to register handler: no available slots for IRQ %d", irq);
        return IRQ_RESULT_ERROR;
    }
    
    // Register the handler
    enhanced_irq_handlers[irq][slot].handler = handler;
    enhanced_irq_handlers[irq][slot].priority = priority;
    enhanced_irq_handlers[irq][slot].context = context;
    enhanced_irq_handlers[irq][slot].flags = flags;
    enhanced_irq_handlers[irq][slot].name = name;
    
    log_debug("IRQ", "Registered handler '%s' for IRQ %d with priority %d", 
             name ? name : "unnamed", irq, priority);
    
    // Enable the IRQ if not already enabled
    if (!irq_enabled_status[irq]) {
        irq_enable(irq);
    }
    
    return IRQ_RESULT_HANDLED;
}

/**
 * Unregister an IRQ handler
 */
uintos_irq_result_t unregister_irq_handler(uint8_t irq, uintos_enhanced_irq_handler_t handler) {
    // Ensure IRQ number is valid
    if (irq >= 256) {
        return IRQ_RESULT_ERROR;
    }
    
    // Find the handler
    int found = 0;
    for (int i = 0; i < MAX_IRQ_HANDLERS_PER_VECTOR; i++) {
        if (enhanced_irq_handlers[irq][i].handler == handler) {
            // Found the handler, remove it
            found = 1;
            
            // Shift all handlers up
            for (int j = i; j < MAX_IRQ_HANDLERS_PER_VECTOR - 1; j++) {
                enhanced_irq_handlers[irq][j] = enhanced_irq_handlers[irq][j+1];
            }
            
            // Clear the last slot
            enhanced_irq_handlers[irq][MAX_IRQ_HANDLERS_PER_VECTOR - 1].handler = NULL;
            enhanced_irq_handlers[irq][MAX_IRQ_HANDLERS_PER_VECTOR - 1].priority = 0;
            enhanced_irq_handlers[irq][MAX_IRQ_HANDLERS_PER_VECTOR - 1].context = NULL;
            enhanced_irq_handlers[irq][MAX_IRQ_HANDLERS_PER_VECTOR - 1].flags = 0;
            enhanced_irq_handlers[irq][MAX_IRQ_HANDLERS_PER_VECTOR - 1].name = NULL;
            
            break;
        }
    }
    
    // If all handlers are gone, disable the IRQ
    int all_gone = 1;
    for (int i = 0; i < MAX_IRQ_HANDLERS_PER_VECTOR; i++) {
        if (enhanced_irq_handlers[irq][i].handler != NULL) {
            all_gone = 0;
            break;
        }
    }
    
    if (all_gone) {
        irq_disable(irq);
    }
    
    return found ? IRQ_RESULT_HANDLED : IRQ_RESULT_UNHANDLED;
}

/**
 * Get statistics for an IRQ
 */
void irq_get_statistics(uint8_t irq, uint32_t* count, uint32_t* time_spent) {
    if (irq < 256) {
        if (count) *count = irq_statistics_count[irq];
        if (time_spent) *time_spent = irq_statistics_time[irq];
    }
}

/**
 * Reset statistics for an IRQ
 */
void irq_reset_statistics(uint8_t irq) {
    if (irq < 256) {
        irq_statistics_count[irq] = 0;
        irq_statistics_time[irq] = 0;
    }
}

/**
 * Dump information about all handlers registered for an IRQ
 */
void irq_dump_handlers(uint8_t irq) {
    if (irq >= 256) {
        return;
    }
    
    uint8_t old_color = vga_current_color;
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    
    vga_write_string("IRQ ");
    
    // Convert IRQ to string
    char irq_str[4];
    if (irq < 10) {
        irq_str[0] = '0' + irq;
        irq_str[1] = 0;
    } else if (irq < 100) {
        irq_str[0] = '0' + (irq / 10);
        irq_str[1] = '0' + (irq % 10);
        irq_str[2] = 0;
    } else {
        irq_str[0] = '0' + (irq / 100);
        irq_str[1] = '0' + ((irq / 10) % 10);
        irq_str[2] = '0' + (irq % 10);
        irq_str[3] = 0;
    }
    vga_write_string(irq_str);
    
    vga_write_string(" (");
    
    // Show name if available
    const char* name = irq_get_name(irq);
    if (name) {
        vga_write_string(name);
    } else {
        vga_write_string("Unknown");
    }
    
    vga_write_string(") handlers:\n");
    
    // Display all registered handlers
    int handlers_found = 0;
    for (int i = 0; i < MAX_IRQ_HANDLERS_PER_VECTOR; i++) {
        if (enhanced_irq_handlers[irq][i].handler != NULL) {
            handlers_found = 1;
            
            // Convert priority to string
            char prio_str[3];
            uint8_t priority = enhanced_irq_handlers[irq][i].priority;
            prio_str[0] = '0' + (priority / 10);
            prio_str[1] = '0' + (priority % 10);
            prio_str[2] = 0;
            
            vga_write_string(" - Priority: ");
            vga_write_string(prio_str);
            
            vga_write_string(", Handler: ");
            if (enhanced_irq_handlers[irq][i].name) {
                vga_write_string(enhanced_irq_handlers[irq][i].name);
            } else {
                vga_write_string("<unnamed>");
            }
            vga_write_string("\n");
        }
    }
    
    if (!handlers_found) {
        vga_write_string(" No handlers registered\n");
    }
    
    // Display statistics
    vga_write_string(" Statistics: Count=");
    
    // Convert count to string
    char count_str[11];
    uint32_t count = irq_statistics_count[irq];
    int count_idx = 0;
    do {
        count_str[10 - count_idx] = '0' + (count % 10);
        count /= 10;
        count_idx++;
    } while (count > 0);
    
    // Fill the rest with spaces
    for (int i = 0; i < 10 - count_idx; i++) {
        count_str[i] = ' ';
    }
    count_str[10] = '\0';
    
    vga_write_string(count_str);
    
    vga_write_string(", Status: ");
    vga_write_string(irq_is_enabled(irq) ? "Enabled" : "Disabled");
    vga_write_string("\n");
    
    // Restore color
    vga_set_color(old_color);
}

/**
 * Get the name of an IRQ
 */
const char* irq_get_name(uint8_t irq) {
    if (irq < 32) {
        return irq_names[irq];
    }
    
    // Special cases for common hardware IRQs
    switch (irq) {
        case 32: return "Timer";
        case 33: return "Keyboard";
        case 34: return "Cascade";
        case 35: return "COM2";
        case 36: return "COM1";
        case 37: return "LPT2";
        case 38: return "Floppy";
        case 39: return "LPT1";
        case 40: return "CMOS RTC";
        case 44: return "PS/2 Mouse";
        case 45: return "FPU";
        case 46: return "ATA Primary";
        case 47: return "ATA Secondary";
        default: return NULL;
    }
}

/**
 * Enable an IRQ
 */
void irq_enable(uint8_t irq) {
    if (irq >= 256) {
        return;
    }
    
    // Mark as enabled
    irq_enabled_status[irq] = 1;
    
    // Handle hardware-specific enabling
    if (apic_supported()) {
        // If using APIC, handle via IOAPIC
        if (irq >= 32 && irq < 48) {
            // Convert IRQ 32-47 to 0-15 for legacy PIC IRQs
            uint8_t pic_irq = irq - 32;
            pic_clear_mask(pic_irq);
        }
    } else {
        // Legacy PIC
        if (irq >= 32 && irq < 48) {
            pic_clear_mask(irq - 32);
        }
    }
}

/**
 * Disable an IRQ
 */
void irq_disable(uint8_t irq) {
    if (irq >= 256) {
        return;
    }
    
    // Mark as disabled
    irq_enabled_status[irq] = 0;
    
    // Handle hardware-specific disabling
    if (apic_supported()) {
        // If using APIC, handle via IOAPIC
        if (irq >= 32 && irq < 48) {
            // Convert IRQ 32-47 to 0-15 for legacy PIC IRQs
            uint8_t pic_irq = irq - 32;
            pic_set_mask(pic_irq);
        }
    } else {
        // Legacy PIC
        if (irq >= 32 && irq < 48) {
            pic_set_mask(irq - 32);
        }
    }
}

/**
 * Check if an IRQ is enabled
 */
uint8_t irq_is_enabled(uint8_t irq) {
    if (irq >= 256) {
        return 0;
    }
    return irq_enabled_status[irq];
}

/**
 * Mask all IRQs
 */
void irq_mask_all(void) {
    // Disable all interrupts on PIC
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    // Update status flags
    for (int i = 32; i < 48; i++) {
        irq_enabled_status[i] = 0;
    }
}

/**
 * Unmask all IRQs
 */
void irq_unmask_all(void) {
    // Enable all interrupts on PIC
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
    
    // Update status flags
    for (int i = 32; i < 48; i++) {
        irq_enabled_status[i] = 1;
    }
}

/**
 * Register a spurious IRQ handler
 */
void irq_register_spurious_handler(void (*handler)(uint8_t irq)) {
    spurious_irq_handler = handler;
}

/**
 * Generic IRQ dispatcher for enhanced handlers
 * This function is called by the assembly interrupt handlers to dispatch to the appropriate C handler
 */
void irq_dispatch_enhanced(uint8_t irq) {
    uint32_t start_time = 0;
    
    // Increment the count for this IRQ
    irq_statistics_count[irq]++;
    
    // Log at trace level - only visible when debugging
    log_trace("IRQ", "Dispatching IRQ %d (%s)", irq, irq_get_name(irq) ? irq_get_name(irq) : "Unknown");
    
    // TODO: Implement timing mechanism for tracking time spent in handlers
    // start_time = get_system_ticks();
    
    // Call all registered handlers in priority order
    int handled = 0;
    for (int i = 0; i < MAX_IRQ_HANDLERS_PER_VECTOR; i++) {
        if (enhanced_irq_handlers[irq][i].handler != NULL) {
            const char* handler_name = enhanced_irq_handlers[irq][i].name ? 
                                      enhanced_irq_handlers[irq][i].name : "unnamed";
            
            log_trace("IRQ", "Executing handler '%s' for IRQ %d", handler_name, irq);
            
            uintos_irq_result_t result = enhanced_irq_handlers[irq][i].handler(irq, enhanced_irq_handlers[irq][i].context);
            
            if (result == IRQ_RESULT_HANDLED) {
                log_trace("IRQ", "Handler '%s' fully handled IRQ %d", handler_name, irq);
                handled = 1;
                break;  // Stop processing more handlers
            }
            else if (result == IRQ_RESULT_ERROR) {
                log_warning("IRQ", "Handler '%s' returned error for IRQ %d", handler_name, irq);
                // Log error but continue with next handler
            }
            else if (result == IRQ_RESULT_PASS) {
                log_trace("IRQ", "Handler '%s' passed IRQ %d to next handler", handler_name, irq);
            }
            // For IRQ_RESULT_UNHANDLED, continue with next handler
        }
    }
    
    // If not handled and we have a spurious handler, call it
    if (!handled) {
        if (spurious_irq_handler != NULL) {
            log_debug("IRQ", "IRQ %d not handled by any registered handler, calling spurious handler", irq);
            spurious_irq_handler(irq);
        } else {
            log_warning("IRQ", "Unhandled IRQ %d (%s)", irq, irq_get_name(irq) ? irq_get_name(irq) : "Unknown");
        }
    }
    
    // Send EOI as needed
    if (irq >= 32 && irq < 48) {
        // Hardware IRQ (legacy PIC IRQs are mapped to vectors 32-47)
        if (apic_supported()) {
            // Write to APIC EOI register
            *(uint32_t*)(apic_base_addr + LAPIC_EOI) = 0;
            log_trace("IRQ", "Sent EOI to APIC for IRQ %d", irq);
        } else {
            // Legacy PIC
            pic_send_eoi(irq - 32);
            log_trace("IRQ", "Sent EOI to PIC for IRQ %d", irq);
        }
    }
    
    // TODO: Update time spent statistic
    // irq_statistics_time[irq] += get_system_ticks() - start_time;
}
