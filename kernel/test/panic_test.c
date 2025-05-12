#include "panic_test.h"
#include "panic.h"
#include "logging/log.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file panic_test.c
 * @brief Tests for the kernel panic system
 *
 * This file contains functions to test various panic scenarios and ensure
 * the panic system is working correctly.
 */

// Deliberately cause a page fault
static void test_page_fault(void) {
    log_info("PANIC_TEST", "Testing page fault handler");
    
    // Attempt to write to a non-mapped page
    volatile uint32_t *bad_ptr = (uint32_t*)0xA0000000;
    *bad_ptr = 0xDEADBEEF;
    
    // Should never reach here
    log_error("PANIC_TEST", "Page fault test failed - execution continued after bad memory access");
}

// Deliberately cause a division by zero
static void test_division_by_zero(void) {
    log_info("PANIC_TEST", "Testing division by zero handler");
    
    // Divide by zero
    volatile int a = 10;
    volatile int b = 0;
    volatile int c = a / b;
    
    // Should never reach here
    log_error("PANIC_TEST", "Division by zero test failed - execution continued after division by zero");
}

// Test stack overflow handling
static void stack_overflow_recursion(int depth) {
    char large_buffer[1024]; // Allocate a large buffer on the stack
    
    // Fill buffer to prevent optimization
    for (int i = 0; i < 1024; i++) {
        large_buffer[i] = (char)i;
    }
    
    if (depth % 10 == 0) {
        log_debug("PANIC_TEST", "Stack depth: %d bytes", depth * 1024);
    }
    
    // Recurse until we overflow the stack
    stack_overflow_recursion(depth + 1);
}

static void test_stack_overflow(void) {
    log_info("PANIC_TEST", "Testing stack overflow handler");
    stack_overflow_recursion(0);
    
    // Should never reach here
    log_error("PANIC_TEST", "Stack overflow test failed - execution continued after stack overflow");
}

// Test assertion failure
static void test_assertion_failure(void) {
    log_info("PANIC_TEST", "Testing assertion failure");
    
    // Trigger an assertion failure
    ASSERT(1 == 2);
    
    // Should never reach here
    log_error("PANIC_TEST", "Assertion test failed - execution continued after assertion failure");
}

// Deliberately trigger a general panic
static void test_general_panic(void) {
    log_info("PANIC_TEST", "Testing general panic");
    
    // Trigger a general panic
    PANIC(PANIC_GENERAL, "This is a test panic message");
    
    // Should never reach here
    log_error("PANIC_TEST", "General panic test failed - execution continued after panic");
}

// Run all panic tests
void run_panic_tests(panic_test_type_t test_type) {
    log_info("PANIC_TEST", "Starting panic system tests - system will intentionally crash");
    
    switch (test_type) {
        case PANIC_TEST_PAGE_FAULT:
            test_page_fault();
            break;
            
        case PANIC_TEST_DIVISION_BY_ZERO:
            test_division_by_zero();
            break;
            
        case PANIC_TEST_STACK_OVERFLOW:
            test_stack_overflow();
            break;
            
        case PANIC_TEST_ASSERTION:
            test_assertion_failure();
            break;
            
        case PANIC_TEST_GENERAL:
            test_general_panic();
            break;
            
        default:
            log_error("PANIC_TEST", "Unknown test type: %d", test_type);
            break;
    }
    
    // Should never reach here
    log_error("PANIC_TEST", "Panic test failed - execution continued after panic condition");
}
