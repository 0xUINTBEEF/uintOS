#include "io.h"
#include "irq.h"
#include "gdt.h"
#include "asm.h"
#include "task.h"
#include "task1.h"
#include "task2.h"
#include "lapic.h"
#include "keyboard.h"
#include "shell.h"
#include "vga.h"
#include "../filesystem/fat12.h"
#include "../memory/paging.h"
#include "../memory/heap.h"
#include "../hal/include/hal.h"
#include "../kernel/graphics/graphics.h"
#include "../kernel/logging/log.h"

// Define system version constants
#define SYSTEM_VERSION "1.0.0"
#define SYSTEM_BUILD_DATE "April 30, 2025"

// Paging structures
#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIRECTORY_ENTRIES 1024

// Global variables
unsigned int page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
unsigned int page_table[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
int hal_initialized = 0;

// Forward declarations
void gdb_stub(void);
void vga_demo(void);
void initialize_system(void);
void display_welcome_message(void);

struct tss initial_task_state = {
  .esp0 = 0x10000,
  .ss0_r = DATA_SELECTOR,
  .ss1_r = DATA_SELECTOR,
  .esp1 = 0x10000,
  .ss2_r = DATA_SELECTOR,
  .eip = 0x0,
  .esp2 = 0x10000,
  .esp = 0x10000,
  .eflags = 0x87,
  .es_r = VIDEO_SELECTOR,
  .cs_r = CODE_SELECTOR,
  .ds_r = DATA_SELECTOR,
  .ss_r = DATA_SELECTOR,
  .fs_r = DATA_SELECTOR,
  .gs_r = DATA_SELECTOR,
};

/**
 * Initialize the paging system
 */
void initialize_paging() {
    log_info("Initializing paging subsystem...");
    
    // Map the first 4MB of memory
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        page_table[i] = (i * PAGE_SIZE) | 3; // Present and writable
    }

    // Point the first entry of the page directory to the page table
    page_directory[0] = ((unsigned int)page_table) | 3; // Present and writable

    // Set all other entries in the page directory to not present
    for (int i = 1; i < PAGE_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // Load the page directory into CR3
    asm volatile("mov %0, %%cr3" : : "r"(page_directory));

    // Enable paging by setting the PG bit in CR0
    unsigned int cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Set the PG bit
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    
    log_info("Paging initialized successfully");
}

/**
 * Provides a breakpoint for debugging with GDB
 */
void gdb_stub() {
    asm volatile("int3"); // Trigger a breakpoint interrupt for GDB
}

/**
 * Display VGA demo showing graphical capabilities
 */
void vga_demo() {
    // Save current color
    uint8_t old_color = vga_current_color;
    
    log_debug("Launching VGA demo...");
    
    // Clear screen with blue background
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_clear_screen();
    
    // Draw a title
    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE));
    vga_write_string_at("uintOS VGA Demo", 30, 1);
    
    // Draw some boxes with different colors
    vga_draw_box(5, 3, 25, 10, vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_draw_window(30, 3, 50, 10, "Info", vga_entry_color(VGA_COLOR_CYAN, VGA_COLOR_BLUE), 
                   vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_CYAN));
    vga_draw_window(55, 3, 75, 10, "Help", vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLUE), 
                   vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_GREEN));
    
    // Add some text inside boxes
    vga_write_string_at("File Explorer", 10, 5);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_write_string_at("Documents", 8, 7);
    vga_write_string_at("Pictures", 8, 8);
    vga_write_string_at("Settings", 8, 9);
    
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_write_string_at("System Info:", 33, 5);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_write_string_at("CPU: 1x86", 33, 6);
    vga_write_string_at("RAM: 16 MB", 33, 7);
    vga_write_string_at("OS: uintOS " SYSTEM_VERSION, 33, 8);
    vga_write_string_at("Date: " SYSTEM_BUILD_DATE, 33, 9);
    
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_write_string_at("Keyboard Shortcuts:", 58, 5);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_write_string_at("F1 - Help", 58, 6);
    vga_write_string_at("F2 - Menu", 58, 7);
    vga_write_string_at("F3 - Search", 58, 8);
    vga_write_string_at("ESC - Exit", 58, 9);
    
    // Draw a progress bar
    vga_draw_horizontal_line(20, 12, 40, vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE));
    vga_draw_horizontal_line(20, 12, 28, vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLUE));
    vga_write_string_at("System Loading... 70%", 28, 14);
    
    // Add a footer
    vga_set_color(vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY));
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga_buffer[24 * VGA_WIDTH + i] = vga_entry(' ', vga_current_color);
    }
    vga_write_string_at("Press any key to continue to shell...", 22, 24);
    
    // Wait for a keypress
    while (!is_key_available()) {
        // Simple animation for the progress bar
        static int progress = 0;
        static int direction = 1;
        
        // Erase old progress indicator
        vga_draw_horizontal_line(20, 12, 40, vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE));
        
        // Update progress
        progress += direction;
        if (progress >= 40) {
            direction = -1;
        } else if (progress <= 0) {
            direction = 1;
        }
        
        // Draw new progress
        vga_draw_horizontal_line(20, 12, progress, vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLUE));
        
        // Small delay
        for (volatile int i = 0; i < 500000; i++);
    }
    
    // Clear keypress
    keyboard_read_key();
    
    // Restore original color
    vga_set_color(old_color);
    vga_clear_screen();
    
    log_debug("VGA demo completed");
}

/**
 * Display welcome message with version information
 */
void display_welcome_message() {
    vga_set_color(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    vga_write_string("uintOS (" SYSTEM_BUILD_DATE ") - Version " SYSTEM_VERSION "\n");
    vga_write_string("-------------------------------------------\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_write_string("Memory, filesystem, task and VGA subsystems initialized\n");
    vga_write_string("Type 'help' for a list of available commands\n\n");
    
    log_info("System initialization completed successfully");
}

/**
 * Initialize all system components
 */
void initialize_system() {
    // Initialize logging first for early diagnostics
    log_init(LOG_LEVEL_INFO);
    log_info("uintOS " SYSTEM_VERSION " starting up...");
    
    // Initialize memory management
    initialize_paging();
    heap_init();
    
    // Initialize Security Subsystem (after memory management, before peripherals)
    log_info("Initializing security subsystem...");
    int security_status = security_init();
    if (security_status != 0) {
        log_error("Security subsystem initialization failed (status: %d)", security_status);
    } else {
        log_info("Security subsystem initialized successfully");
    }
    
    // Initialize Hardware Abstraction Layer
    log_info("Initializing Hardware Abstraction Layer...");
    int hal_status = hal_initialize();
    if (hal_status != 0) {
        // HAL initialization failed, fall back to direct hardware access
        log_error("HAL initialization failed (status: %d), falling back to direct hardware access", hal_status);
        vga_init();
        vga_set_color(vga_entry_color(VGA_COLOR_RED, VGA_COLOR_BLACK));
        vga_write_string("WARNING: HAL initialization failed, falling back to direct hardware access\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        // HAL initialized successfully
        hal_initialized = 1;
        log_info("HAL initialized successfully");
        vga_init();
        vga_set_color(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
        vga_write_string("HAL initialized successfully\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    // Initialize file system
    log_info("Initializing filesystem...");
    int fs_status = fat12_init();
    if (fs_status != 0) {
        log_warn("Filesystem initialization failed (status: %d)", fs_status);
    } else {
        log_info("Filesystem initialized successfully");
    }

    // Initialize hardware and kernel subsystems
    log_info("Initializing GDT and TSS...");
    initialize_gdt(&initial_task_state);
    
    log_info("Initializing interrupt system...");
    uintos_initialize_interrupts();
    
    // Configure and calibrate the system timer for preemptive scheduling
    log_info("Configuring system timer for preemptive scheduling...");
    if (hal_initialized) {
        // Configure the timer using HAL
        hal_timer_info_t timer_info;
        hal_timer_get_info(0, &timer_info); // Get info for the first timer (LAPIC)
        
        hal_timer_calibrate(); // Calibrate the timer frequency
        
        // Configure timer for periodic interrupts at 100Hz (10ms intervals)
        hal_timer_config_t timer_config = {
            .mode = HAL_TIMER_PERIODIC,
            .frequency = 100, // 100Hz = 10ms period
            .vector = 32,     // IRQ0 = Vector 32 (timer)
            .callback = NULL  // We'll handle it via the IRQ handler
        };
        
        int timer_status = hal_timer_configure(0, &timer_config);
        if (timer_status != 0) {
            log_error("Failed to configure timer for preemptive scheduling");
        } else {
            hal_timer_start(0);
            log_info("Preemptive scheduling timer configured successfully (100Hz)");
        }
    } else {
        // Direct hardware access for timer configuration
        log_info("Configuring PIT timer for preemptive scheduling...");
        
        // PIT runs at 1.193182 MHz, divisor = 11932 gives ~100Hz
        uint16_t divisor = 11932; 
        
        // Set PIT mode 3 (square wave)
        outb(0x43, 0x36);
        
        // Set divisor (low byte, then high byte)
        outb(0x40, divisor & 0xFF);
        outb(0x40, (divisor >> 8) & 0xFF);
        
        // Unmask IRQ0 (timer) in PIC
        outb(0x21, inb(0x21) & 0xFE);
        
        log_info("PIT timer configured for preemptive scheduling (100Hz)");
    }
    
    // Initialize task management
    log_info("Initializing multitasking...");
    initialize_multitasking();
    
    // Initialize threading system
    log_info("Initializing threading system...");
    thread_init();
    log_info("Threading system initialized");
    
    // Initialize inter-process communication (IPC)
    log_info("Initializing IPC subsystem...");
    ipc_init();
    log_info("IPC subsystem initialized");
    
    // Initialize keyboard driver with improved handling
    log_info("Initializing keyboard driver...");
    keyboard_init();
    keyboard_flush(); // Clear any pending keys
    
    // Create system tasks with the new named task API
    log_info("Creating system tasks...");
    create_named_task(idle_task, "System Idle");
    create_named_task(counter_task, "Background Counter");
    
    // Initialize shell interface with the enhanced filesystem commands
    log_info("Initializing shell...");
    shell_init();
    
    log_info("System initialization complete");
}

/**
 * Main kernel entry point
 */
void kernel_main() {
    // Initialize all system components
    initialize_system();
    
    // Show VGA demo screen on first boot
    vga_demo();
    
    // Display welcome message
    display_welcome_message();
    
    // Start the shell - this will run in a loop
    log_info("Starting interactive shell");
    shell_run();
    
    // We should never reach here due to the shell's infinite loop
    log_error("Shell terminated unexpectedly!");
    
    // If the shell somehow exits, halt the system
    for (;;) {
        asm("hlt");
    }
}
