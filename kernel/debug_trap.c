/**
 * @file debug_trap.c
 * @brief Debug traps for testing hardware debugging
 * 
 * This file provides functions to trigger various debug traps
 * for testing and demonstration of the hardware debug features.
 */

#include "debug_registers.h"
#include "logging/log.h"
#include <stdio.h>

/**
 * Debug trap function that can be called to test debug exceptions
 * 
 * @param trap_type Type of trap to trigger
 * @return 0 on success, non-zero on error
 */
int debug_trigger_trap(int trap_type) {
    log_info("DEBUG", "Triggering debug trap type %d", trap_type);
    
    switch(trap_type) {
        case 0: // Software breakpoint (INT3)
            log_info("DEBUG", "Triggering INT3 trap");
            // Use inline assembly to insert an INT3 instruction
            __asm__ volatile("int3");
            break;
            
        case 1: // Single-step trap
            log_info("DEBUG", "Enabling single-step trap");
            // Enable single-step mode
            debug_enable_single_step();
            // The next instruction will trigger a debug exception
            __asm__ volatile("nop"); // Do something trivial
            break;
            
        case 2: { // Deliberate write to a memory breakpoint
            log_info("DEBUG", "Triggering write to memory breakpoint");
            
            // Create a test variable
            static volatile int test_var = 0;
            
            // Set a hardware breakpoint on it
            log_info("DEBUG", "Setting breakpoint on variable at %p", &test_var);
            debug_set_breakpoint(0, (void*)&test_var, BREAKPOINT_WRITE, BREAKPOINT_SIZE_4, true);
            
            // Trigger the breakpoint by writing to the variable
            log_info("DEBUG", "Writing to watched memory...");
            test_var = 42;
            
            // Clear the breakpoint
            debug_clear_breakpoint(0);
            break;
        }
            
        case 3: { // Memory access breakpoint
            log_info("DEBUG", "Triggering memory access breakpoint");
            
            // Create a test variable
            static volatile int test_var = 0;
            
            // Set a hardware breakpoint on it
            log_info("DEBUG", "Setting breakpoint on variable at %p", &test_var);
            debug_set_breakpoint(0, (void*)&test_var, BREAKPOINT_ACCESS, BREAKPOINT_SIZE_4, true);
            
            // Trigger the breakpoint by accessing the variable
            log_info("DEBUG", "Accessing watched memory...");
            int dummy = test_var;
            log_info("DEBUG", "Memory value: %d", dummy);
            
            // Clear the breakpoint
            debug_clear_breakpoint(0);
            break;
        }
            
        case 4: { // Execution breakpoint
            log_info("DEBUG", "Triggering execution breakpoint");
            
            // Set a breakpoint on a function we're about to call
            static void target_function(void) {
                log_info("DEBUG", "Target function called");
                // Just a simple function for demonstration
                __asm__ volatile("nop");
            }
            
            // Set hardware breakpoint on the function
            log_info("DEBUG", "Setting execution breakpoint at %p", target_function);
            debug_set_breakpoint(0, (void*)target_function, BREAKPOINT_EXECUTION, BREAKPOINT_SIZE_1, true);
            
            // Call the function to trigger the breakpoint
            target_function();
            
            // Clear the breakpoint
            debug_clear_breakpoint(0);
            break;
        }
            
        default:
            log_error("DEBUG", "Unknown trap type %d", trap_type);
            return -1;
    }
    
    log_info("DEBUG", "Debug trap completed");
    return 0;
}

/**
 * Debug trap shell command
 */
void cmd_debug_trap(int argc, char *argv[]) {
    log_debug("SHELL", "Executing debug trap command");
    
    // Check arguments
    if (argc < 2) {
        printf("Usage: debug_trap <trap_type>\n");
        printf("Trap types:\n");
        printf("  0 - Software breakpoint (INT3)\n");
        printf("  1 - Single-step trap\n");
        printf("  2 - Memory write breakpoint\n");
        printf("  3 - Memory access breakpoint\n");
        printf("  4 - Execution breakpoint\n");
        return;
    }
    
    // Parse trap type
    int trap_type = atoi(argv[1]);
    if (trap_type < 0 || trap_type > 4) {
        printf("Invalid trap type. Must be between 0 and 4.\n");
        return;
    }
    
    printf("Triggering debug trap type %d...\n", trap_type);
    
    // Trigger the trap
    int result = debug_trigger_trap(trap_type);
    
    if (result == 0) {
        printf("Debug trap completed successfully.\n");
    } else {
        printf("Error triggering debug trap.\n");
    }
}
