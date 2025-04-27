#include "io.h"
#include "irq.h"
#include "gdt.h"
#include "asm.h"
#include "task.h"
#include "task1.h"
#include "lapic.h"
#include "keyboard.h"
#include "shell.h"
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
  enable_lapic_timer(TIMER_PERIODIC, 0x100, TIMER_DIV_128, 32);

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

void kernel_main() {
    // Initialize memory management
    paging_init();

    // Initialize heap memory management
    heap_init();

    // Initialize file system
    fat12_init();

    // Initialize hardware and kernel subsystems
    initialize_gdt();
    initialize_idt();
    initialize_paging();
    
    // Initialize keyboard driver
    keyboard_init();
    
    // Initialize shell interface
    shell_init();

    // Trigger GDB stub for debugging if needed
    // gdb_stub();
    
    // Start the shell
    shell_run();
}
