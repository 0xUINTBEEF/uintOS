#include "task_demo.h"
#include "vga.h"
#include "task.h"
#include "scheduler.h"
#include "preempt.h"
#include "logging/log.h"
#include <stdbool.h>

// Flag to control if the demo is running
static bool demo_running = false;

// Counter for progress display
static volatile uint64_t task1_counter = 0;
static volatile uint64_t task2_counter = 0;

/**
 * First demo task, counts and updates a progress counter
 */
static void demo_task1() {
    log_info("Demo Task 1 starting");
    
    int row = 2;
    int col = 5;
    
    // Display initial message
    vga_clear_screen();
    vga_write_string_at("Preemptive Multitasking Demo", 0, 0);
    vga_write_string_at("================================", 1, 0);
    vga_write_string_at("Task 1 Counter: 0", row, col);
    vga_write_string_at("Task 2 Counter: 0", row + 1, col);
    
    // Add status info
    if (is_preemption_enabled()) {
        vga_write_string_at("Preemption: ENABLED", row + 3, col);
    } else {
        vga_write_string_at("Preemption: DISABLED", row + 3, col);
    }
    
    vga_write_string_at("Press any key to stop demo", row + 5, col);
    
    // Show visual progress bar
    vga_write_string_at("[                    ]", row, col + 25);
    vga_write_string_at("[                    ]", row + 1, col + 25);
    
    // Run the task loop
    while (demo_running) {
        task1_counter++;
        
        // Update counter display periodically to reduce screen flicker
        if ((task1_counter % 10000) == 0) {
            char buffer[32];
            int_to_string(task1_counter, buffer);
            vga_write_string_at("                    ", row, col + 15); // Clear previous count
            vga_write_string_at(buffer, row, col + 15);
            
            // Update progress bar
            int progress = (task1_counter / 10000) % 20;
            vga_write_char_at('=', row, col + 26 + progress);
        }
    }
    
    log_info("Demo Task 1 finished");
}

/**
 * Second demo task, counts at a different rate
 */
static void demo_task2() {
    log_info("Demo Task 2 starting");
    
    int row = 3;
    int col = 5;
    
    // Run the task loop
    while (demo_running) {
        // Add some additional work to make this task slower
        for (volatile int i = 0; i < 100; i++) {
            task2_counter++;
        }
        
        // Update counter display periodically
        if ((task2_counter % 10000) == 0) {
            char buffer[32];
            int_to_string(task2_counter, buffer);
            vga_write_string_at("                    ", row, col + 15); // Clear previous count
            vga_write_string_at(buffer, row, col + 15);
            
            // Update progress bar - use a different character to distinguish from task1
            int progress = (task2_counter / 10000) % 20;
            vga_write_char_at('#', row, col + 26 + progress);
            
            // Add CPU time metrics
            char cpu_time[10];
            vga_write_string_at("Task ratio: ", row + 4, col);
            if (task1_counter > 0) {
                int ratio = (task2_counter * 100) / task1_counter;
                int_to_string(ratio, cpu_time);
                vga_write_string_at("    ", row + 4, col + 12);
                vga_write_string_at(cpu_time, row + 4, col + 12);
                vga_write_string_at("%", row + 4, col + 12 + strlen(cpu_time));
            }
        }
    }
    
    log_info("Demo Task 2 finished");
}

/**
 * Helper function for int to string conversion
 */
static void int_to_string(uint64_t value, char *str) {
    // Handle zero case
    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    
    // Find string length by repeatedly dividing by 10
    int length = 0;
    uint64_t temp = value;
    while (temp > 0) {
        temp /= 10;
        length++;
    }
    
    // Add null terminator
    str[length] = '\0';
    
    // Fill in the digits from right to left
    int i = length - 1;
    while (value > 0) {
        str[i--] = '0' + (value % 10);
        value /= 10;
    }
}

/**
 * Start the multitasking demo with two competing tasks
 */
void start_multitasking_demo() {
    log_info("Starting multitasking demo");
    
    // Reset statistics
    reset_preemption_stats();
    
    // Reset counters
    task1_counter = 0;
    task2_counter = 0;
    demo_running = true;
    
    // Create demo tasks
    int task1_id = create_named_task(demo_task1, "Demo Task 1");
    int task2_id = create_named_task(demo_task2, "Demo Task 2");
    
    log_debug("Created demo tasks with IDs %d and %d", task1_id, task2_id);
    
    // Wait for a keypress
    while (!keyboard_is_key_pressed()) {
        task_yield();
    }
    
    // Clean up
    demo_running = false;
    keyboard_read_key(); // Clear the keypress
    
    // Give tasks time to exit
    for (volatile int i = 0; i < 1000000; i++) {
        task_yield();
    }
    
    // Display preemption statistics
    vga_clear_screen();
    vga_write_string_at("Multitasking Demo Results", 0, 0);
    vga_write_string_at("========================", 1, 0);

    char buffer[64];
    
    // Get preemption stats
    uint64_t involuntary = 0, voluntary = 0, timer_ints = 0, disabled_time = 0;
    get_preemption_stats(&involuntary, &voluntary, &timer_ints, &disabled_time);
    
    vga_write_string_at("Task 1 Counter:", 3, 2);
    int_to_string(task1_counter, buffer);
    vga_write_string_at(buffer, 3, 18);
    
    vga_write_string_at("Task 2 Counter:", 4, 2);
    int_to_string(task2_counter, buffer);
    vga_write_string_at(buffer, 4, 18);
    
    vga_write_string_at("Preemption was:", 6, 2);
    if (is_preemption_enabled()) {
        vga_write_string_at("ENABLED", 6, 18);
    } else {
        vga_write_string_at("DISABLED", 6, 18);
    }
    
    vga_write_string_at("Timer interrupts:", 8, 2);
    int_to_string(timer_ints, buffer);
    vga_write_string_at(buffer, 8, 18);
    
    vga_write_string_at("Involuntary switches:", 9, 2);
    int_to_string(involuntary, buffer);
    vga_write_string_at(buffer, 9, 23);
    
    vga_write_string_at("Voluntary switches:", 10, 2);
    int_to_string(voluntary, buffer);
    vga_write_string_at(buffer, 10, 21);
    
    vga_write_string_at("Press any key to continue...", 15, 2);
    
    // Wait for a keypress
    while (!keyboard_is_key_pressed()) {
        task_yield();
    }
    keyboard_read_key(); // Clear the keypress
    
    log_info("Multitasking demo complete");
    
    vga_clear_screen();
}
