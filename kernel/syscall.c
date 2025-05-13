#include "syscall.h"
#include "io.h"
#include "irq.h"
#include "task.h"
#include "scheduler.h"
#include "logging/log.h"
#include "../filesystem/vfs/vfs.h"
#include "security.h"
#include "security_syscall.h"
#include "panic.h"
#include "module.h"
#include "memory/vmm.h"

// Error codes for system calls
#define EPERM           1      // Operation not permitted
#define ENOENT          2      // No such file or directory
#define ESRCH           3      // No such process
#define EINTR           4      // Interrupted system call
#define EIO             5      // I/O error
#define ENXIO           6      // No such device or address
#define E2BIG           7      // Argument list too long
#define ENOEXEC         8      // Exec format error
#define EBADF           9      // Bad file number
#define ECHILD          10     // No child processes
#define EAGAIN          11     // Try again
#define ENOMEM          12     // Out of memory
#define EACCES          13     // Permission denied
#define EFAULT          14     // Bad address
#define ENOTBLK         15     // Block device required
#define EBUSY           16     // Device or resource busy
#define EEXIST          17     // File exists
#define EXDEV           18     // Cross-device link
#define ENODEV          19     // No such device
#define ENOTDIR         20     // Not a directory
#define EISDIR          21     // Is a directory
#define EINVAL          22     // Invalid argument
#define ENFILE          23     // File table overflow
#define EMFILE          24     // Too many open files

// Forward declaration of syscall_asm_handler
extern void syscall_asm_handler(void);

// Array of syscall handlers
static syscall_handler_t syscall_handlers[SYS_MAX + 1] = {0};

// Initialize syscall table with handlers
void syscall_init(void) {
    // Register IRQ handler for syscall interrupt (int 0x80)
    register_interrupt_handler(0x80, syscall_asm_handler);
    
    // Register basic syscall handlers
    syscall_register(SYS_EXIT, sys_exit_handler);
    syscall_register(SYS_FORK, sys_fork_handler);
    syscall_register(SYS_READ, sys_read_handler);
    syscall_register(SYS_WRITE, sys_write_handler);
    syscall_register(SYS_OPEN, sys_open_handler);
    syscall_register(SYS_CLOSE, sys_close_handler);
    syscall_register(SYS_WAITPID, sys_waitpid_handler);
    syscall_register(SYS_EXECVE, sys_execve_handler);
    syscall_register(SYS_TIME, sys_time_handler);
    syscall_register(SYS_GETPID, sys_getpid_handler);
    syscall_register(SYS_YIELD, sys_yield_handler);
    syscall_register(SYS_MODULE_LOAD, sys_module_load_handler);    syscall_register(SYS_MODULE_UNLOAD, sys_module_unload_handler);
    syscall_register(SYS_MMAP, sys_mmap_handler);
    syscall_register(SYS_MUNMAP, sys_munmap_handler);
    syscall_register(SYS_ASLR_CONTROL, sys_aslr_control_handler);
    
    // Initialize syscall security
    syscall_security_init();
    
    log_info("KERNEL", "Syscall interface initialized with %d handlers and enhanced security", SYS_MAX);
}

// Register a syscall handler
int syscall_register(uint64_t syscall_num, syscall_handler_t handler) {
    if (syscall_num > SYS_MAX || handler == NULL) {
        return -EINVAL;
    }
    
    syscall_handlers[syscall_num] = handler;
    return 0;
}

// Get a syscall handler
syscall_handler_t syscall_get_handler(uint64_t syscall_num) {
    if (syscall_num > SYS_MAX) {
        return NULL;
    }
    
    return syscall_handlers[syscall_num];
}

#include "syscall_wrappers.h"

// Forward declarations for specific system call handlers
int64_t sys_read_handler(syscall_args_t *args);
int64_t sys_write_handler(syscall_args_t *args);
int64_t sys_open_handler(syscall_args_t *args);
int64_t sys_close_handler(syscall_args_t *args);
int64_t sys_waitpid_handler(syscall_args_t *args);
int64_t sys_execve_handler(syscall_args_t *args);
int64_t sys_time_handler(syscall_args_t *args);
int64_t sys_getpid_handler(syscall_args_t *args);
int64_t sys_yield_handler(syscall_args_t *args);
int64_t sys_module_load_handler(syscall_args_t *args);
int64_t sys_module_unload_handler(syscall_args_t *args);
int64_t sys_mmap_handler(syscall_args_t *args);
int64_t sys_munmap_handler(syscall_args_t *args);

// Handle a syscall
int64_t syscall_handle(uint64_t syscall_num, syscall_args_t* args) {
    // Validate parameters
    if (args == NULL) {
        log_warn("KERNEL", "Syscall %d received NULL args", syscall_num);
        return -EFAULT;
    }
    
    // Security check: Ensure syscall number is valid using whitelist approach
    if (!is_valid_syscall(syscall_num)) {
        log_warn("SECURITY", "Invalid syscall number: %d", syscall_num);
        security_monitor_record_event(
            SEC_EVENT_UNAUTHORIZED_ACCESS,
            2, // Medium severity
            SID_SYSTEM,
            SID_SYSTEM,
            "syscall",
            syscall_num,
            0, // Not successful
            "Invalid syscall number"
        );
        return -EINVAL;
    }
    
    // Special handling for security-critical syscalls that need argument validation
    // Direct calls to secure wrappers for the most security-critical syscalls
    switch(syscall_num) {
        case SYS_READ:
            return secure_sys_read(args);
            
        case SYS_WRITE:
            return secure_sys_write(args);
            
        case SYS_OPEN:
            return secure_sys_open(args);
            
        case SYS_EXECVE:
            return secure_sys_execve(args);
            
        case SYS_MMAP:
            return secure_sys_mmap(args);
            
        default:
            // For other syscalls, follow the regular handler lookup path
            break;
    }
    
    // Get the handler for this syscall
    syscall_handler_t handler = syscall_get_handler(syscall_num);
    
    if (handler == NULL) {
        log_warn("KERNEL", "Unimplemented syscall number: %d", syscall_num);
        return -EINVAL;
    }
    
    // Check permissions before executing syscall
    // Make sure this function is implemented in your security.c file
    if (!security_check_syscall_permission(current_task, syscall_num)) {
        log_warn("KERNEL", "Security violation: task %d not allowed to perform syscall %d", 
               current_task->id, syscall_num);
        return -EPERM; // Permission denied
    }
    
    // Execute syscall handler
    return handler(args);
}

// Entry point for syscalls from assembly interrupt handler
void syscall_entry_handler(uint64_t syscall_num, syscall_args_t* args) {
    int64_t result = syscall_handle(syscall_num, args);
    
    // Return value will be placed in rax by the assembly handler
    return result;
}

// System call implementations

// Exit the current process
int64_t sys_exit_handler(syscall_args_t* args) {
    int status = args->arg1;
    task_exit(status);
    // Should never reach here
    return 0;
}

// Fork the current process
int64_t sys_fork_handler(syscall_args_t* args) {
    return task_fork();
}

// Read from a file descriptor
int64_t sys_read_handler(syscall_args_t* args) {
    int fd = args->arg1;
    void* buf = (void*)args->arg2;
    size_t count = args->arg3;
    
    // Validate parameters
    if (buf == NULL && count > 0) {
        return -EFAULT;
    }
    
    return vfs_read(fd, buf, count);
}

// Write to a file descriptor
int64_t sys_write_handler(syscall_args_t* args) {
    int fd = args->arg1;
    const void* buf = (void*)args->arg2;
    size_t count = args->arg3;
    
    // Validate parameters
    if (buf == NULL && count > 0) {
        return -EFAULT;
    }
    
    return vfs_write(fd, buf, count);
}

// Open a file and return a file descriptor
int64_t sys_open_handler(syscall_args_t* args) {
    const char* pathname = (const char*)args->arg1;
    int flags = args->arg2;
    int mode = args->arg3;
    
    // Validate parameters
    if (pathname == NULL) {
        return -EFAULT;
    }
    
    return vfs_open(pathname, flags, mode);
}

// Close a file descriptor
int64_t sys_close_handler(syscall_args_t* args) {
    int fd = args->arg1;
    
    return vfs_close(fd);
}

// Wait for a child process to change state
int64_t sys_waitpid_handler(syscall_args_t* args) {
    int pid = args->arg1;
    int* status = (int*)args->arg2;
    int options = args->arg3;
    
    return task_waitpid(pid, status, options);
}

// Execute a program
int64_t sys_execve_handler(syscall_args_t* args) {
    const char* pathname = (const char*)args->arg1;
    char* const* argv = (char* const*)args->arg2;
    char* const* envp = (char* const*)args->arg3;
    
    // Validate parameters
    if (pathname == NULL) {
        return -EFAULT;
    }
    
    return task_execve(pathname, argv, envp);
}

// Get current system time
int64_t sys_time_handler(syscall_args_t* args) {
    time_t* tloc = (time_t*)args->arg1;
    
    uint64_t time = get_system_time();
    if (tloc) {
        *tloc = time;
    }
    
    return time;
}

// Get current process ID
int64_t sys_getpid_handler(syscall_args_t* args) {
    return current_task->id;
}

// Yield the CPU to another process
int64_t sys_yield_handler(syscall_args_t* args) {
    scheduler_yield();
    return 0;
}

// Load a kernel module
int64_t sys_module_load_handler(syscall_args_t* args) {
    const char* module_path = (const char*)args->arg1;
    
    // Validate parameters
    if (module_path == NULL) {
        return -EFAULT;
    }
    
    // Only root can load modules
    if (current_task->euid != 0) {
        return -EPERM;
    }
    
    return module_load(module_path);
}

// Unload a kernel module
int64_t sys_module_unload_handler(syscall_args_t* args) {
    const char* module_name = (const char*)args->arg1;
    
    // Validate parameters
    if (module_name == NULL) {
        return -EFAULT;
    }
    
    // Only root can unload modules
    if (current_task->euid != 0) {
        return -EPERM;
    }
    
    return module_unload(module_name);
}

// Map memory
int64_t sys_mmap_handler(syscall_args_t* args) {
    void* addr = (void*)args->arg1;
    size_t length = args->arg2;
    int prot = args->arg3;
    int flags = args->arg4;
    int fd = args->arg5;
    off_t offset = args->arg6;
    
    if (length == 0) {
        return -EINVAL;
    }
    
    return (int64_t)mmap(addr, length, prot, flags, fd, offset);
}

// Unmap memory
int64_t sys_munmap_handler(syscall_args_t* args) {
    void* addr = (void*)args->arg1;
    size_t length = args->arg2;
    
    if (addr == NULL || length == 0) {
        return -EINVAL;
    }
    
    return munmap(addr, length);
}

// Get system time in seconds since epoch
uint64_t get_system_time(void) {
    // Implementation depends on the timer setup
    // For now, just return a value based on ticks since boot
    extern uint64_t timer_get_ticks(void);
    extern uint32_t TIMER_FREQ;
    return (uint64_t)(timer_get_ticks() / TIMER_FREQ);
}