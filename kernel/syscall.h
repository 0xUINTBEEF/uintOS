#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>
#include <stddef.h>

// System call numbers
#define SYS_EXIT            1
#define SYS_FORK            2
#define SYS_READ            3
#define SYS_WRITE           4
#define SYS_OPEN            5
#define SYS_CLOSE           6
#define SYS_WAITPID         7
#define SYS_EXECVE          8
#define SYS_CHDIR           9
#define SYS_TIME            10
#define SYS_MMAP            11
#define SYS_MUNMAP          12
#define SYS_BRK             13
#define SYS_GETPID          14
#define SYS_GETPPID         15
#define SYS_MKDIR           16
#define SYS_RMDIR           17
#define SYS_UNLINK          18
#define SYS_YIELD           19
#define SYS_MODULE_LOAD     20
#define SYS_MODULE_UNLOAD   21
#define SYS_ASLR_CONTROL    22

// Maximum supported syscall number
#define SYS_MAX             50

// mmap constants
#define PROT_NONE           0x00
#define PROT_READ           0x01
#define PROT_WRITE          0x02
#define PROT_EXEC           0x04

#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20

#define MAP_FAILED          ((void *)-1)

// System call arguments structure
typedef struct {
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
} syscall_args_t;

// System call handler function type
typedef int64_t (*syscall_handler_t)(syscall_args_t*);

// System call interface functions
void syscall_init(void);
int syscall_register(uint64_t syscall_num, syscall_handler_t handler);
syscall_handler_t syscall_get_handler(uint64_t syscall_num);
int64_t syscall_handle(uint64_t syscall_num, syscall_args_t* args);
void syscall_entry_handler(uint64_t syscall_num, syscall_args_t* args);

// Function to get system time in seconds
uint64_t get_system_time(void);

// ASLR control function
int sys_aslr_control(int operation, uint32_t arg);

// Helper macro to make a system call from user space
// This will be used in the user-space libc implementation
#define SYSCALL0(num) \
    ({ \
        int64_t result; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num) \
                     : "memory", "cc"); \
        result; \
    })

#define SYSCALL1(num, arg1) \
    ({ \
        int64_t result; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num), "b" (arg1) \
                     : "memory", "cc"); \
        result; \
    })

#define SYSCALL2(num, arg1, arg2) \
    ({ \
        int64_t result; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num), "b" (arg1), "c" (arg2) \
                     : "memory", "cc"); \
        result; \
    })

#define SYSCALL3(num, arg1, arg2, arg3) \
    ({ \
        int64_t result; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) \
                     : "memory", "cc"); \
        result; \
    })

#define SYSCALL4(num, arg1, arg2, arg3, arg4) \
    ({ \
        int64_t result; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4) \
                     : "memory", "cc"); \
        result; \
    })

#define SYSCALL5(num, arg1, arg2, arg3, arg4, arg5) \
    ({ \
        int64_t result; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3), \
                       "S" (arg4), "D" (arg5) \
                     : "memory", "cc"); \
        result; \
    })

#define SYSCALL6(num, arg1, arg2, arg3, arg4, arg5, arg6) \
    ({ \
        int64_t result; \
        register uint64_t r8 asm("r8") = arg6; \
        asm volatile ("int $0x80" \
                     : "=a" (result) \
                     : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3), \
                       "S" (arg4), "D" (arg5), "r" (r8) \
                     : "memory", "cc"); \
        result; \
    })

#endif /* _SYSCALL_H */