#ifndef UINTOS_PANIC_H
#define UINTOS_PANIC_H

#include <inttypes.h>
#include <stdbool.h>

/**
 * @file panic.h
 * @brief Kernel panic handling for uintOS
 *
 * This file contains declarations for kernel panic functionality,
 * which provides a mechanism to handle fatal system errors.
 */

// Types of kernel panics
typedef enum {
    PANIC_GENERAL,           // General unspecified error
    PANIC_MEMORY_CORRUPTION, // Memory is corrupted
    PANIC_PAGE_FAULT,        // Unhandled page fault
    PANIC_DOUBLE_FAULT,      // Double fault
    PANIC_STACK_OVERFLOW,    // Stack overflow
    PANIC_DIVISION_BY_ZERO,  // Division by zero
    PANIC_ASSERTION_FAILED,  // Assertion failed
    PANIC_UNEXPECTED_IRQ,    // Unexpected interrupt
    PANIC_HARDWARE_FAILURE,  // Hardware failure
    PANIC_DRIVER_ERROR,      // Driver error
    PANIC_FS_ERROR,          // File system error
    PANIC_SECURITY_VIOLATION, // Security violation (unauthorized access, etc.)
    PANIC_DEADLOCK_DETECTED,  // Deadlock in synchronization primitives
    PANIC_STACK_SMASHING,     // Stack corruption detected
    PANIC_KERNEL_BOUNDS,      // Kernel memory bounds violation
    PANIC_CRITICAL_RESOURCE   // Critical resource error
} panic_type_t;

/**
 * Initiate a kernel panic
 *
 * @param type The type of panic
 * @param file Source file where panic was triggered
 * @param line Line number where panic was triggered
 * @param func Function where panic was triggered
 * @param fmt Format string for panic message
 * @param ... Additional arguments for format string
 *
 * This function never returns.
 */
void kernel_panic(panic_type_t type, const char* file, int line, 
                 const char* func, const char* fmt, ...) 
                 __attribute__((noreturn));

/**
 * Kernel panic handler for assertion failures
 *
 * @param file Source file where assertion failed
 * @param line Line number where assertion failed
 * @param func Function where assertion failed
 * @param expr The expression that failed
 *
 * This function never returns.
 */
void kernel_assert_failed(const char* file, int line, 
                         const char* func, const char* expr) 
                         __attribute__((noreturn));

/**
 * Check if the system is currently in a panic state
 *
 * @return true if the system is in a panic state, false otherwise
 */
bool is_panicking(void);

/**
 * Register a custom panic callback function
 *
 * @param callback Function to call during panic processing
 * @param context Context pointer to pass to callback
 * @return 0 on success, -1 if registration failed
 */
int register_panic_callback(void (*callback)(void* context), void* context);

// Convenience macro to panic with the current file, line and function info
#define PANIC(type, fmt, ...) \
    kernel_panic(type, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

// Assertion macros
#define ASSERT(expr) \
    do { if (!(expr)) kernel_assert_failed(__FILE__, __LINE__, __func__, #expr); } while(0)

#define ASSERT_MSG(expr, fmt, ...) \
    do { if (!(expr)) kernel_panic(PANIC_ASSERTION_FAILED, __FILE__, __LINE__, __func__, \
                                   "Assertion '" #expr "' failed: " fmt, ##__VA_ARGS__); } while(0)

#endif /* UINTOS_PANIC_H */