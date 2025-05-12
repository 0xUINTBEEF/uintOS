#include "security_test.h"
#include "syscall.h"
#include "security_syscall.h"
#include "logging/log.h"
#include "panic.h"
#include <stdint.h>

/**
 * Security test module for validating syscall security features
 */

// Test that validates user/kernel boundary checks
void test_user_kernel_boundary(void) {
    log_info("SECURITY_TEST", "Starting user/kernel boundary test");
    
    // Test user address validation
    void* user_addr = (void*)0x08000000;  // Typically a user space address
    void* kernel_addr = (void*)0xC0100000; // Typically a kernel space address
    
    log_info("SECURITY_TEST", "Testing is_user_address() function");
    
    // These should pass
    if (!is_user_address(user_addr)) {
        log_error("SECURITY_TEST", "FAILED: Valid user address 0x%08x not recognized as user address", 
                 (uintptr_t)user_addr);
    } else {
        log_info("SECURITY_TEST", "PASSED: User address 0x%08x correctly identified", 
                (uintptr_t)user_addr);
    }
    
    // These should fail
    if (is_user_address(kernel_addr)) {
        log_error("SECURITY_TEST", "FAILED: Kernel address 0x%08x incorrectly recognized as user address", 
                 (uintptr_t)kernel_addr);
    } else {
        log_info("SECURITY_TEST", "PASSED: Kernel address 0x%08x correctly rejected", 
                (uintptr_t)kernel_addr);
    }
    
    // Test pointer validation
    log_info("SECURITY_TEST", "Testing validate_user_ptr() function");
    
    // Should pass for valid user pointers
    if (!validate_user_ptr(user_addr, 1024, VM_PERM_READ)) {
        log_error("SECURITY_TEST", "FAILED: Valid user pointer validation failed");
    } else {
        log_info("SECURITY_TEST", "PASSED: Valid user pointer validation succeeded");
    }
    
    // Should fail for kernel pointers
    if (validate_user_ptr(kernel_addr, 1024, VM_PERM_READ)) {
        log_error("SECURITY_TEST", "FAILED: Kernel pointer incorrectly validated as user pointer");
    } else {
        log_info("SECURITY_TEST", "PASSED: Kernel pointer correctly rejected");
    }
    
    // Test boundary conditions
    void* boundary_addr = (void*)(KERNEL_SPACE_START - 500);
    
    // Should fail if buffer crosses into kernel space
    if (validate_user_ptr(boundary_addr, 1024, VM_PERM_READ)) {
        log_error("SECURITY_TEST", "FAILED: Buffer crossing into kernel space was accepted");
    } else {
        log_info("SECURITY_TEST", "PASSED: Buffer crossing boundary was rejected");
    }
    
    log_info("SECURITY_TEST", "User/kernel boundary tests complete");
}

// Test stack canary protection
void test_stack_canary(void) {
    log_info("SECURITY_TEST", "Starting stack canary test");
    
    // Set up a canary on the stack
    uint32_t canary_value;
    install_stack_canary(&canary_value);
    log_info("SECURITY_TEST", "Installed stack canary");
    
    // Verify canary works - this should pass
    verify_stack_canary(&canary_value);
    log_info("SECURITY_TEST", "PASSED: Stack canary verification succeeded");
    
    // Simulate corrupting the canary - uncomment to test panic handling
    // canary_value = 0x12345678;  // This should cause a panic when verified
    // verify_stack_canary(&canary_value);
    
    log_info("SECURITY_TEST", "Stack canary tests complete");
}

// Test syscall number validation
void test_syscall_validation(void) {
    log_info("SECURITY_TEST", "Starting syscall validation test");
    
    // Test valid syscall numbers
    if (!is_valid_syscall(SYS_READ)) {
        log_error("SECURITY_TEST", "FAILED: Valid syscall number %d rejected", SYS_READ);
    } else {
        log_info("SECURITY_TEST", "PASSED: Valid syscall number %d accepted", SYS_READ);
    }
    
    // Test invalid syscall numbers
    uint64_t invalid_syscall = SYS_MAX + 10;
    if (is_valid_syscall(invalid_syscall)) {
        log_error("SECURITY_TEST", "FAILED: Invalid syscall number %lld accepted", invalid_syscall);
    } else {
        log_info("SECURITY_TEST", "PASSED: Invalid syscall number %lld rejected", invalid_syscall);
    }
    
    log_info("SECURITY_TEST", "Syscall validation tests complete");
}

// Run all security tests
void run_security_tests(void) {
    log_info("SECURITY_TEST", "====== Starting security tests ======");
    
    // Run individual tests
    test_user_kernel_boundary();
    test_stack_canary();
    test_syscall_validation();
    
    log_info("SECURITY_TEST", "====== Security tests complete ======");
}
