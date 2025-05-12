#ifndef PANIC_TEST_H
#define PANIC_TEST_H

/**
 * @file panic_test.h
 * @brief Tests for the kernel panic system
 *
 * This file contains functions to test various panic scenarios and ensure
 * the panic system is working correctly.
 */

// Types of panic tests
typedef enum {
    PANIC_TEST_PAGE_FAULT,      // Test page fault handling
    PANIC_TEST_DIVISION_BY_ZERO, // Test division by zero handling
    PANIC_TEST_STACK_OVERFLOW,   // Test stack overflow handling
    PANIC_TEST_ASSERTION,        // Test assertion failure handling
    PANIC_TEST_GENERAL           // Test general panic
} panic_test_type_t;

/**
 * Run panic tests
 * 
 * @param test_type Which type of panic to test
 * 
 * @note This function deliberately causes a kernel panic and should only
 *       be used for testing. The system will need to be rebooted after
 *       running this function.
 */
void run_panic_tests(panic_test_type_t test_type);

#endif // PANIC_TEST_H
