#include "panic.h"
#include "vga.h"
#include "io.h"
#include "logging/log.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/**
 * @file panic.c
 * @brief Kernel panic implementation for uintOS
 *
 * This file implements kernel panic handling for fatal system errors.
 */

// Stack trace depth for panic reports
#define MAX_STACK_TRACE_DEPTH 16

// Maximum number of panic callbacks
#define MAX_PANIC_CALLBACKS 8

// Static variables
static bool panic_in_progress = false;
static char panic_message_buffer[512];

// Callback system for panic notifications
typedef struct {
    void (*func)(void* context);
    void* context;
    bool used;
} panic_callback_t;

static panic_callback_t panic_callbacks[MAX_PANIC_CALLBACKS] = {0};

// Convert panic type to string
static const char* get_panic_type_string(panic_type_t type) {
    switch (type) {
        case PANIC_GENERAL:           return "GENERAL ERROR";
        case PANIC_MEMORY_CORRUPTION: return "MEMORY CORRUPTION";
        case PANIC_PAGE_FAULT:        return "PAGE FAULT";
        case PANIC_DOUBLE_FAULT:      return "DOUBLE FAULT";
        case PANIC_STACK_OVERFLOW:    return "STACK OVERFLOW";
        case PANIC_DIVISION_BY_ZERO:  return "DIVISION BY ZERO";
        case PANIC_ASSERTION_FAILED:  return "ASSERTION FAILED";
        case PANIC_UNEXPECTED_IRQ:    return "UNEXPECTED INTERRUPT";
        case PANIC_HARDWARE_FAILURE:  return "HARDWARE FAILURE";
        case PANIC_DRIVER_ERROR:      return "DRIVER ERROR";
        case PANIC_FS_ERROR:          return "FILE SYSTEM ERROR";
        case PANIC_SECURITY_VIOLATION: return "SECURITY VIOLATION";
        case PANIC_DEADLOCK_DETECTED:  return "DEADLOCK DETECTED";
        case PANIC_STACK_SMASHING:     return "STACK SMASHING DETECTED";
        case PANIC_KERNEL_BOUNDS:      return "KERNEL BOUNDS VIOLATION";
        case PANIC_CRITICAL_RESOURCE:  return "CRITICAL RESOURCE ERROR";
        default:                      return "UNKNOWN ERROR";
    }
}

/**
 * Print a hexadecimal number with specified width
 */
static void print_hex(uint32_t value, int width) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[16] = {0};
    
    for (int i = width - 1; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    
    vga_write_string(buffer);
}

/**
 * Collect register values for diagnostics
 */
static void capture_registers(uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx,
                             uint32_t* esi, uint32_t* edi, uint32_t* ebp, uint32_t* esp) {
    asm volatile(
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        "mov %%esi, %4\n"
        "mov %%edi, %5\n"
        "mov %%ebp, %6\n"
        "mov %%esp, %7\n"
        : "=m"(*eax), "=m"(*ebx), "=m"(*ecx), "=m"(*edx),
          "=m"(*esi), "=m"(*edi), "=m"(*ebp), "=m"(*esp)
        :
        : "memory"
    );
    
    // Adjust ESP to account for our function call
    *esp += 20; 
}

/**
 * Dump memory content around a specific address
 */
static void dump_memory(void* address, size_t bytes_before, size_t bytes_after) {
    uint8_t* start = (uint8_t*)address - bytes_before;
    uint8_t* end = (uint8_t*)address + bytes_after;
    
    // Align to 16-byte boundary for cleaner output
    start = (uint8_t*)((uintptr_t)start & ~0xF);
    
    vga_write_string("\nMemory dump around 0x");
    print_hex((uint32_t)address, 8);
    vga_write_string(":\n");
    
    // For safety, avoid excessively large dumps and potentially invalid addresses
    size_t total_bytes = (uintptr_t)end - (uintptr_t)start;
    if (total_bytes > 512) {
        total_bytes = 512;
        end = start + total_bytes;
    }
    
    // Check pointer validity - very basic check
    if ((uintptr_t)start < 0x1000 || (uintptr_t)end > 0xFFFFFFFF) {
        vga_write_string("Invalid memory address range - cannot dump\n");
        return;
    }
    
    char ascii_buffer[17] = {0};
    
    // Display memory in hex and ASCII format, 16 bytes per line
    for (uint8_t* ptr = start; ptr < end; ptr += 16) {
        // Print address
        vga_write_string("0x");
        print_hex((uint32_t)ptr, 8);
        vga_write_string(": ");
        
        // Print hex values
        for (int i = 0; i < 16; i++) {
            if (ptr + i < end) {
                uint8_t value = ptr[i];
                print_hex(value, 2);
                vga_write_string(" ");
                
                // Store printable ASCII character or '.' for non-printable
                ascii_buffer[i] = (value >= 32 && value <= 126) ? value : '.';
            } else {
                vga_write_string("   ");
                ascii_buffer[i] = ' ';
            }
        }
        
        // Print ASCII representation
        ascii_buffer[16] = 0;
        vga_write_string(" | ");
        vga_write_string(ascii_buffer);
        vga_write_string("\n");
    }
}

/**
 * Generate a simple stack trace
 */
static void generate_stack_trace(uint32_t ebp) {
    uint32_t* frame_ptr = (uint32_t*)ebp;
    uint32_t* saved_eip;
    
    vga_write_string("\nStack trace:\n");
    
    for (int i = 0; i < MAX_STACK_TRACE_DEPTH && frame_ptr != NULL && (uint32_t)frame_ptr >= 0x1000; i++) {
        saved_eip = (uint32_t*)(frame_ptr[1]);
        if (saved_eip == NULL) break;
        
        vga_write_string("[");
        print_hex(i, 2);
        vga_write_string("] 0x");
        print_hex((uint32_t)saved_eip, 8);
        vga_write_string("\n");
        
        // Memory dump around the EIP for additional context
        if (i == 0) {
            dump_memory(saved_eip, 16, 32);
        }
        
        frame_ptr = (uint32_t*)(frame_ptr[0]);
    }
}

/**
 * Flush all log buffers to ensure logs are written
 */
static void flush_logs(void) {
    // Dump log buffer to screen if not already being displayed
    log_dump_buffer();
}

/**
 * Notify all registered panic callbacks
 */
static void notify_panic_callbacks(void) {
    for (int i = 0; i < MAX_PANIC_CALLBACKS; i++) {
        if (panic_callbacks[i].used && panic_callbacks[i].func != NULL) {
            panic_callbacks[i].func(panic_callbacks[i].context);
        }
    }
}

/**
 * Display panic information on screen
 */
static void display_panic_info(panic_type_t type, const char* file, int line, 
                              const char* func, const char* message) {
    // Set color to bright white on red background for high visibility
    uint8_t old_color = vga_current_color;
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    
    // Clear screen to make panic message more visible
    vga_clear_screen();
    
    // Display panic header and system halted message
    vga_write_string("\n\n");
    vga_write_string("*************************************\n");
    vga_write_string("*        KERNEL PANIC               *\n");
    vga_write_string("*************************************\n\n");
    
    vga_write_string("System halted: ");
    vga_write_string(get_panic_type_string(type));
    vga_write_string("\n\n");
    
    // Display panic message
    vga_write_string("Error: ");
    vga_write_string(message);
    vga_write_string("\n\n");
    
    // Display source information
    vga_write_string("Location: ");
    vga_write_string(file);
    vga_write_string(":");
    
    char line_str[16];
    snprintf(line_str, sizeof(line_str), "%d", line);
    vga_write_string(line_str);
    
    vga_write_string(" in function ");
    vga_write_string(func);
    vga_write_string("\n\n");
    
    // Capture register state for debugging
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    capture_registers(&eax, &ebx, &ecx, &edx, &esi, &edi, &ebp, &esp);
    
    vga_write_string("Register dump:\n");
    
    vga_write_string("EAX: 0x"); print_hex(eax, 8);
    vga_write_string("  EBX: 0x"); print_hex(ebx, 8);
    vga_write_string("\n");
    
    vga_write_string("ECX: 0x"); print_hex(ecx, 8);
    vga_write_string("  EDX: 0x"); print_hex(edx, 8);
    vga_write_string("\n");
    
    vga_write_string("ESI: 0x"); print_hex(esi, 8);
    vga_write_string("  EDI: 0x"); print_hex(edi, 8);
    vga_write_string("\n");
    
    vga_write_string("EBP: 0x"); print_hex(ebp, 8);
    vga_write_string("  ESP: 0x"); print_hex(esp, 8);
    vga_write_string("\n");
    
    // Generate stack trace if possible
    generate_stack_trace(ebp);
    
    // Display reboot instructions
    vga_write_string("\n\n");
    vga_write_string("The system has been halted to prevent damage.\n");
    vga_write_string("Please reboot the system.\n");
    
    // Restore color
    vga_set_color(old_color);
}

/**
 * Get basic system state information
 */
static void get_system_state(char* buffer, size_t buffer_size) {
    extern uint64_t uptime_ticks; // Assuming this exists in your kernel
    
    snprintf(buffer, buffer_size, 
             "Uptime: %llds, Last Task: %s, Mode: %s",
             uptime_ticks / 1000,  // Convert to seconds
             "Unknown",            // Replace with actual last task info
             "Unknown");           // Replace with actual kernel mode info
}

/**
 * Record panic crash dump for post-mortem analysis
 */
static void record_crash_dump(panic_type_t type, const char* file, int line,
                             const char* func, const char* message,
                             interrupt_frame_t* frame) {
    // Create a crash dump file with all relevant system information
    extern bool crash_dump_create(
        panic_type_t type, 
        const char* file, 
        int line, 
        const char* func, 
        const char* message,
        interrupt_frame_t* frame
    );

    if (crash_dump_create(type, file, line, func, message, frame)) {
        log_info("PANIC", "Created crash dump for post-mortem analysis");
    } else {
        log_error("PANIC", "Failed to create crash dump");
    }
}

/**
 * Initiate a kernel panic
 */
void kernel_panic(panic_type_t type, const char* file, int line, 
                 const char* func, const char* fmt, ...) {
    // Prevent recursive panics
    if (panic_in_progress) {
        // We're already panicking, just return to the original panic handler
        for(;;) {
            asm volatile("hlt");
        }
    }
    
    // Set panic flag to prevent recursive panics
    panic_in_progress = true;
    
    // Disable interrupts to prevent any further processing
    asm volatile("cli");
    
    // Format the panic message
    va_list args;
    va_start(args, fmt);
    vsnprintf(panic_message_buffer, sizeof(panic_message_buffer), fmt, args);
    va_end(args);
    
    // Log panic message first (might not succeed if system is badly corrupted)
    log_emergency("PANIC", "%s: %s (at %s:%d in %s)", 
                 get_panic_type_string(type), panic_message_buffer, file, line, func);
    
    // Flush all logs
    flush_logs();
    
    // Capture current register state for the crash dump
    interrupt_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    
    // Get EIP and ESP using the frame pointer
    uint32_t ebp;
    asm volatile("mov %%ebp, %0" : "=r"(ebp));
    
    // If we have a valid frame pointer, get the caller's info
    if (ebp != 0) {
        uint32_t* ebp_ptr = (uint32_t*)ebp;
        frame.eip = ebp_ptr[1];  // Return address is at ebp+4
        frame.esp = ebp + 8;     // Estimated stack pointer
        
        // Get EFLAGS (simplified - actual flags will be approximated)
        uint32_t eflags;
        asm volatile("pushf; pop %0" : "=r"(eflags));
        frame.eflags = eflags;
        
        // Get segment registers
        asm volatile("mov %%cs, %0" : "=r"(frame.cs));
        asm volatile("mov %%ss, %0" : "=r"(frame.ss));
    }
    
    // Get other general purpose registers
    asm volatile(
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        "mov %%esi, %4\n"
        "mov %%edi, %5\n"
        "mov %%ebp, %6\n"
        : "=m"(frame.eax), "=m"(frame.ebx), "=m"(frame.ecx), "=m"(frame.edx),
          "=m"(frame.esi), "=m"(frame.edi), "=m"(frame.ebp)
        :
        : "memory"
    );
    
    // Try to record crash dump information for post-mortem analysis
    // This must happen before callbacks, since callbacks might corrupt more state
    record_crash_dump(type, file, line, func, panic_message_buffer, &frame);
    
    // Notify any registered callbacks
    notify_panic_callbacks();
    
    // Display panic information on screen
    display_panic_info(type, file, line, func, panic_message_buffer);
    
    // System halt - never returns
    for(;;) {
        asm volatile("hlt");
    }
}

/**
 * Kernel panic handler for assertion failures
 */
void kernel_assert_failed(const char* file, int line, const char* func, const char* expr) {
    kernel_panic(PANIC_ASSERTION_FAILED, file, line, func, "Assertion failed: %s", expr);
}

/**
 * Check if the system is currently in a panic state
 */
bool is_panicking(void) {
    return panic_in_progress;
}

/**
 * Register a custom panic callback function
 */
int register_panic_callback(void (*callback)(void* context), void* context) {
    if (callback == NULL) {
        return -1;
    }
    
    // Find an empty slot
    for (int i = 0; i < MAX_PANIC_CALLBACKS; i++) {
        if (!panic_callbacks[i].used) {
            panic_callbacks[i].func = callback;
            panic_callbacks[i].context = context;
            panic_callbacks[i].used = true;
            return 0;
        }
    }
    
    // No empty slots found
    return -1;
}