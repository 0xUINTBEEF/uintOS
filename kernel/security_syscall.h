#ifndef SECURITY_SYSCALL_H
#define SECURITY_SYSCALL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "syscall.h"

/**
 * ASLR control operations
 */
#define ASLR_OP_GET_STATUS      0  // Get current ASLR status
#define ASLR_OP_SET_STATUS      1  // Enable/disable ASLR
#define ASLR_OP_GET_ENTROPY     2  // Get current entropy bits
#define ASLR_OP_SET_ENTROPY     3  // Set entropy bits
#define ASLR_OP_GET_REGIONS     4  // Get which regions are randomized
#define ASLR_OP_SET_REGIONS     5  // Set which regions to randomize

/**
 * ASLR control system call 
 * 
 * @param operation One of the ASLR_OP_* values
 * @param arg Operation-specific argument
 * @return Operation result, or -1 on error
 */
int sys_aslr_control(int operation, uint32_t arg);

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
