#ifndef SYSCALL_WRAPPERS_H
#define SYSCALL_WRAPPERS_H

#include "syscall.h"
#include <stdint.h>

// Maximum values for security validations
#define MAX_PATH_LENGTH 256
#define MAX_ARGS 64
#define MAX_ARG_LENGTH 4096
#define MAX_OPEN_FILES 256
#define MAX_MMAP_SIZE (1UL << 30)  // 1GB maximum mapping size

// File modes for open()
typedef uint32_t mode_t;

// Secure wrappers for syscall handlers
int64_t secure_sys_read(syscall_args_t *args);
int64_t secure_sys_write(syscall_args_t *args);
int64_t secure_sys_open(syscall_args_t *args);
int64_t secure_sys_execve(syscall_args_t *args);
int64_t secure_sys_mmap(syscall_args_t *args);

#endif // SYSCALL_WRAPPERS_H
