#include "syscall_wrappers.h"
#include "syscall.h"
#include "security_syscall.h"
#include "logging/log.h"
#include "../filesystem/vfs/vfs.h"
#include "memory/vmm.h"
#include <string.h>
#include <stddef.h>

/**
 * This file contains secure wrapper functions for system calls that 
 * validate user arguments before passing them to the actual handlers.
 */

// Secure wrapper for the sys_read system call
int64_t secure_sys_read(syscall_args_t *args) {
    int fd = (int)args->arg1;
    void *buffer = (void*)args->arg2;
    size_t count = args->arg3;
    
    // Validate file descriptor (specific range check would depend on your OS design)
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return -EBADF;
    }
    
    // Validate buffer pointer (must be writable user space memory)
    if (!validate_user_ptr(buffer, count, VM_PERM_WRITE)) {
        log_warn("SYSCALL", "sys_read: Invalid buffer pointer %p", buffer);
        return -EFAULT;
    }
    
    // Create a kernel buffer to safely read data
    char* kernel_buffer = NULL;
    if (count > 0) {
        kernel_buffer = (char*)malloc(count);
        if (!kernel_buffer) {
            return -ENOMEM;
        }
    }
    
    // Call the actual read function
    int64_t bytes_read = vfs_read(fd, kernel_buffer, count);
    
    // On success, copy data back to user buffer
    if (bytes_read > 0) {
        if (copy_to_user(buffer, kernel_buffer, bytes_read) != 0) {
            free(kernel_buffer);
            return -EFAULT;
        }
    }
    
    // Clean up
    if (kernel_buffer) {
        free(kernel_buffer);
    }
    
    return bytes_read;
}

// Secure wrapper for the sys_write system call
int64_t secure_sys_write(syscall_args_t *args) {
    int fd = (int)args->arg1;
    const void *buffer = (const void*)args->arg2;
    size_t count = args->arg3;
    
    // Validate file descriptor
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return -EBADF;
    }
    
    // Validate buffer pointer (must be readable user space memory)
    if (!validate_user_ptr(buffer, count, VM_PERM_READ)) {
        log_warn("SYSCALL", "sys_write: Invalid buffer pointer %p", buffer);
        return -EFAULT;
    }
    
    // Create a kernel buffer to safely copy data from user space
    char* kernel_buffer = NULL;
    if (count > 0) {
        kernel_buffer = (char*)malloc(count);
        if (!kernel_buffer) {
            return -ENOMEM;
        }
        
        if (copy_from_user(kernel_buffer, buffer, count) != 0) {
            free(kernel_buffer);
            return -EFAULT;
        }
    }
    
    // Call the actual write function
    int64_t bytes_written = vfs_write(fd, kernel_buffer, count);
    
    // Clean up
    if (kernel_buffer) {
        free(kernel_buffer);
    }
    
    return bytes_written;
}

// Secure wrapper for the sys_open system call
int64_t secure_sys_open(syscall_args_t *args) {
    const char *pathname = (const char*)args->arg1;
    int flags = (int)args->arg2;
    mode_t mode = (mode_t)args->arg3;
    
    // Validate pathname pointer
    if (!validate_user_string(pathname)) {
        log_warn("SYSCALL", "sys_open: Invalid pathname pointer %p", pathname);
        return -EFAULT;
    }
    
    // Copy pathname to kernel space with safe length limit
    char kernel_pathname[MAX_PATH_LENGTH];
    size_t len = 0;
    
    while (len < MAX_PATH_LENGTH - 1) {
        if (copy_from_user(&kernel_pathname[len], &pathname[len], 1) != 0) {
            return -EFAULT;
        }
        
        if (kernel_pathname[len] == '\0') {
            break;
        }
        
        len++;
    }
    
    // Ensure null termination
    kernel_pathname[MAX_PATH_LENGTH - 1] = '\0';
    
    // Validate flags and mode
    // (Add specific validation logic according to your filesystem implementation)
    
    // Call the actual open function
    return vfs_open(kernel_pathname, flags, mode);
}

// Secure wrapper for the sys_execve system call
int64_t secure_sys_execve(syscall_args_t *args) {
    const char *pathname = (const char*)args->arg1;
    char **argv = (char**)args->arg2;
    char **envp = (char**)args->arg3;
    
    // Validate pathname
    if (!validate_user_string(pathname)) {
        log_warn("SYSCALL", "sys_execve: Invalid pathname pointer %p", pathname);
        return -EFAULT;
    }
    
    // Copy pathname to kernel space
    char kernel_pathname[MAX_PATH_LENGTH];
    size_t len = 0;
    
    while (len < MAX_PATH_LENGTH - 1) {
        if (copy_from_user(&kernel_pathname[len], &pathname[len], 1) != 0) {
            return -EFAULT;
        }
        
        if (kernel_pathname[len] == '\0') {
            break;
        }
        
        len++;
    }
    kernel_pathname[MAX_PATH_LENGTH - 1] = '\0';
    
    // Validate argv array
    if (argv != NULL && !validate_user_ptr(argv, sizeof(char*), VM_PERM_READ)) {
        log_warn("SYSCALL", "sys_execve: Invalid argv pointer %p", argv);
        return -EFAULT;
    }
    
    // Validate envp array
    if (envp != NULL && !validate_user_ptr(envp, sizeof(char*), VM_PERM_READ)) {
        log_warn("SYSCALL", "sys_execve: Invalid envp pointer %p", envp);
        return -EFAULT;
    }
    
    // Count and validate argv strings (with reasonable limit)
    char** kernel_argv = NULL;
    int argv_count = 0;
    
    if (argv != NULL) {
        // First, count the arguments
        char* current_arg;
        while (argv_count < MAX_ARGS) {
            if (copy_from_user(&current_arg, &argv[argv_count], sizeof(char*)) != 0) {
                return -EFAULT;
            }
            
            if (current_arg == NULL) {
                break;
            }
            
            // Validate each argument string
            if (!validate_user_string(current_arg)) {
                log_warn("SYSCALL", "sys_execve: Invalid argv[%d] pointer %p", 
                        argv_count, current_arg);
                return -EFAULT;
            }
            
            argv_count++;
        }
        
        // Allocate kernel memory for the argument pointers
        kernel_argv = (char**)malloc((argv_count + 1) * sizeof(char*));
        if (kernel_argv == NULL) {
            return -ENOMEM;
        }
        
        // Copy each argument string to kernel space
        for (int i = 0; i < argv_count; i++) {
            char* arg;
            if (copy_from_user(&arg, &argv[i], sizeof(char*)) != 0) {
                free(kernel_argv);
                return -EFAULT;
            }
            
            // Calculate string length
            size_t arg_len = 0;
            while (arg_len < MAX_ARG_LENGTH) {
                char c;
                if (copy_from_user(&c, &arg[arg_len], 1) != 0) {
                    free(kernel_argv);
                    return -EFAULT;
                }
                
                if (c == '\0') {
                    break;
                }
                
                arg_len++;
            }
            
            // Allocate kernel memory for the string and copy it
            kernel_argv[i] = (char*)malloc(arg_len + 1);
            if (kernel_argv[i] == NULL) {
                // Free previously allocated strings
                for (int j = 0; j < i; j++) {
                    free(kernel_argv[j]);
                }
                free(kernel_argv);
                return -ENOMEM;
            }
            
            if (copy_from_user(kernel_argv[i], arg, arg_len + 1) != 0) {
                // Free previously allocated strings
                for (int j = 0; j <= i; j++) {
                    free(kernel_argv[j]);
                }
                free(kernel_argv);
                return -EFAULT;
            }
        }
        
        // NULL-terminate the array
        kernel_argv[argv_count] = NULL;
    }
    
    // Similar validation would be performed for envp
    // ... (code similar to argv validation) ...
    
    // Call the actual execve function
    int result = sys_execve_handler(kernel_pathname, kernel_argv, NULL);
    
    // Free allocated memory for argv strings
    if (kernel_argv != NULL) {
        for (int i = 0; i < argv_count; i++) {
            free(kernel_argv[i]);
        }
        free(kernel_argv);
    }
    
    return result;
}

// Secure wrapper for the sys_mmap system call
int64_t secure_sys_mmap(syscall_args_t *args) {
    void *addr = (void*)args->arg1;
    size_t length = args->arg2;
    int prot = (int)args->arg3;
    int flags = (int)args->arg4;
    int fd = (int)args->arg5;
    off_t offset = (off_t)args->arg6;
    
    // Validate address if MAP_FIXED is specified
    if ((flags & MAP_FIXED) && !validate_user_ptr(addr, length, VM_PERM_NONE)) {
        log_warn("SYSCALL", "sys_mmap: Invalid fixed address %p", addr);
        return -EINVAL;
    }
    
    // Ensure length is reasonable
    if (length == 0 || length > MAX_MMAP_SIZE) {
        log_warn("SYSCALL", "sys_mmap: Invalid length %zu", length);
        return -EINVAL;
    }
    
    // Validate file descriptor if not anonymous mapping
    if (!(flags & MAP_ANONYMOUS) && (fd < 0 || fd >= MAX_OPEN_FILES)) {
        return -EBADF;
    }
    
    // Validate protection flags
    if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_NONE)) != 0) {
        log_warn("SYSCALL", "sys_mmap: Invalid protection flags %x", prot);
        return -EINVAL;
    }
    
    // Call the actual mmap function
    return (int64_t)sys_mmap_handler(args);
}
