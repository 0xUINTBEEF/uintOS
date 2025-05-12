#ifndef SECURITY_SYSCALL_H
#define SECURITY_SYSCALL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "syscall.h"

/**
 * Initialize syscall security features
 */
void syscall_security_init(void);

/**
 * Check if an address belongs to user space
 */
bool is_user_address(const void* addr);

/**
 * Validate a user pointer - checks if the pointer is in user space
 * and if the memory region it points to is accessible
 */
bool validate_user_ptr(const void* ptr, size_t size, uint32_t required_perms);

/**
 * Validate a user string - ensures it's null-terminated within user space
 */
bool validate_user_string(const char* str);

/**
 * Install a stack canary in the current kernel stack frame
 */
void install_stack_canary(uint32_t* canary_location);

/**
 * Verify a stack canary value - if corrupted, trigger kernel panic
 */
void verify_stack_canary(uint32_t* canary_location);

/**
 * Check if a syscall number is valid
 */
bool is_valid_syscall(uint64_t syscall_num);

/**
 * Safely copy memory from user to kernel space
 */
int copy_from_user(void* dest, const void* src, size_t size);

/**
 * Safely copy memory from kernel to user space
 */
int copy_to_user(void* dest, const void* src, size_t size);

#endif // SECURITY_SYSCALL_H
