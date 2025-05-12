#include "security_syscall.h"
#include "security.h"
#include "memory/vmm.h"
#include "panic.h"
#include "logging/log.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Memory address space boundaries
#define KERNEL_SPACE_START 0xC0000000    // 3GB mark (typical for x86 kernels)
#define USER_SPACE_END     0xBFFFFFFF    // Just below kernel space

// System call security constants
#define MAX_SYSCALL_NUMBER SYS_MAX
#define STACK_CANARY_VALUE 0xDEADC0DE
#define MAX_STRING_LENGTH  4096  // Maximum length for user strings

// Global stack canary value - initialize at boot time with a random value
static uint32_t g_stack_canary = STACK_CANARY_VALUE;
static bool g_syscall_security_initialized = false;

/**
 * Initialize syscall security features
 */
void syscall_security_init(void) {
    if (g_syscall_security_initialized) {
        return;
    }

    // TODO: Generate a truly random canary value from a hardware source
    // For now, we use a fixed value
    g_stack_canary = STACK_CANARY_VALUE;
    
    log_info("SECURITY", "Syscall security initialized with stack canary protection");
    g_syscall_security_initialized = true;
}

/**
 * Check if an address belongs to user space
 */
bool is_user_address(const void* addr) {
    uintptr_t address = (uintptr_t)addr;
    
    // Simple canonical address check
    // User addresses must be below the kernel space start
    return address < KERNEL_SPACE_START && address > 0;
}

/**
 * Validate a user pointer - checks if the pointer is in user space
 * and if the memory region it points to is accessible
 */
bool validate_user_ptr(const void* ptr, size_t size, uint32_t required_perms) {
    if (ptr == NULL && size > 0) {
        return false;
    }
    
    // Zero-sized buffer checks pass (though they're generally useless)
    if (size == 0) {
        return true;
    }
    
    // Check for integer overflow in pointer arithmetic
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;
    
    if (end < start) { // Overflow detection
        return false;
    }
    
    // Check if addresses are in user space
    if (!is_user_address(ptr) || !is_user_address((void*)(end - 1))) {
        return false;
    }

    // TODO: Add more detailed permission checking based on page table entries
    // This would check if the pages have the required VM_PERM_READ/WRITE/EXEC permissions
    
    return true;
}

/**
 * Validate a user string - ensures it's null-terminated within user space
 */
bool validate_user_string(const char* str) {
    if (!is_user_address(str)) {
        return false;
    }
    
    // Initialize counters
    size_t length = 0;
    const char* current = str;
    
    // Search for null terminator within reasonable limits
    while (length < MAX_STRING_LENGTH) {
        // Check page by page to avoid unnecessary page faults
        if (!is_user_address(current)) {
            return false;
        }
        
        // Check character by character
        if (*current == '\0') {
            return true;  // Found null terminator
        }
        
        current++;
        length++;
    }
    
    // String too long (likely not properly terminated)
    return false;
}

/**
 * Install a stack canary in the current kernel stack frame
 */
void install_stack_canary(uint32_t* canary_location) {
    *canary_location = g_stack_canary;
}

/**
 * Verify a stack canary value - if corrupted, trigger kernel panic
 */
void verify_stack_canary(uint32_t* canary_location) {
    if (*canary_location != g_stack_canary) {
        // Stack has been corrupted!
        log_error("SECURITY", "Stack corruption detected! Canary value: %08x, expected: %08x",
                 *canary_location, g_stack_canary);
        kernel_panic("Stack smashing detected");
    }
}

/**
 * Check if a syscall number is valid
 */
bool is_valid_syscall(uint64_t syscall_num) {
    return syscall_num > 0 && syscall_num <= MAX_SYSCALL_NUMBER;
}

/**
 * Safely copy memory from user to kernel space
 */
int copy_from_user(void* dest, const void* src, size_t size) {
    // Validate source pointer
    if (!validate_user_ptr(src, size, VM_PERM_READ)) {
        return -1;
    }
    
    // Simple memcpy - in a real implementation, this would need to handle
    // page faults and validate each page
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
    
    return 0;
}

/**
 * Safely copy memory from kernel to user space
 */
int copy_to_user(void* dest, const void* src, size_t size) {
    // Validate destination pointer
    if (!validate_user_ptr(dest, size, VM_PERM_WRITE)) {
        return -1;
    }
    
    // Simple memcpy - in a real implementation, this would need to handle
    // page faults and validate each page
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
    
    return 0;
}
