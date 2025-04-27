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

// Paging structures
#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIRECTORY_ENTRIES 1024

unsigned int page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
unsigned int page_table[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

void initialize_paging() {
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
}

void kernel_entry();

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

void kernel_entry() {
  initialize_gdt(&initial_task_state);
  initialize_irq();
  set_data_segment(0x10);
  set_stack_segment(0x10);
  set_graphics_segment(0x28);
  
  // Initialize our new components
  keyboard_init();
  heap_init();
  
  // Welcome message
  display_character('W', 15);
  display_character('e', 15);
  display_character('l', 15);
  display_character('c', 15);
  display_character('o', 15);
  display_character('m', 15);
  display_character('e', 15);
  display_character(' ', 15);
  display_character('t', 15);
  display_character('o', 15);
  display_character(' ', 15);
  display_character('u', 15);
  display_character('i', 15);
  display_character('n', 15);
  display_character('t', 15);
  display_character('O', 15);
  display_character('S', 15);
  display_character('\n', 15);

  // Enable LAPIC timer with periodic mode for task switching
  uintos_lapic_enable_timer(UINTOS_TIMER_PERIODIC, 0x100, UINTOS_TIMER_DIV_128, 32);

  // Start the shell (this will run in a loop)
  shell_init();
  shell_run();

  // We should never reach here due to the shell's infinite loop
  START_TASK(task1);
  EXECUTE_TASK(task1);
}

void gdb_stub() {
    asm volatile("int3"); // Trigger a breakpoint interrupt for GDB
}

// VGA demo function to show graphical capabilities
void vga_demo() {
    // Save current color
    uint8_t old_color = vga_current_color;
    
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
    vga_write_string_at("OS: uintOS v1.0", 33, 8);
    vga_write_string_at("Date: April 2025", 33, 9);
    
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
}

void kernel_main() {
    // Initialize memory management
    initialize_paging();
    
    // Initialize heap memory management
    heap_init();

    // Initialize VGA before anything else that displays text
    vga_init();
    
    // Display a nice VGA demo
    vga_demo();
    
    // Initialize file system
    fat12_init();

    // Initialize hardware and kernel subsystems
    initialize_gdt(&initial_task_state);
    uintos_initialize_interrupts();
    initialize_multitasking();
    
    // Initialize keyboard driver with improved handling
    keyboard_init();
    keyboard_flush(); // Clear any pending keys
    
    // Create system tasks with the new named task API
    create_named_task(idle_task, "System Idle");
    create_named_task(counter_task, "Background Counter");
    
    // Initialize shell interface with the enhanced filesystem commands
    shell_init();

    // Print welcome message using VGA functions
    vga_set_color(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    vga_write_string("uintOS (April 2025) - Enhanced Kernel with VGA Support\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_write_string("Memory, filesystem, task and VGA subsystems initialized\n");
    vga_write_string("Type 'help' for a list of available commands\n\n");
    
    // Start the shell - this will run in a loop
    shell_run();
}
