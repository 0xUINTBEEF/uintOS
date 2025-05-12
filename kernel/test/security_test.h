#ifndef SECURITY_TEST_H
#define SECURITY_TEST_H

/**
 * Security Test Module for validating syscall security features
 */

// Test user/kernel boundary validation
void test_user_kernel_boundary(void);

// Test stack canary protection
void test_stack_canary(void);

// Test syscall number validation
void test_syscall_validation(void);

// Run all security tests
void run_security_tests(void);

#endif // SECURITY_TEST_H
