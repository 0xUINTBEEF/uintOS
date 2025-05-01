#include "task.h"
#include "io.h"
#include "security.h"
#include "../memory/heap.h"
#include "../memory/paging.h"
#include "logging/log.h"
#include <string.h>

#define MAX_TASKS 256

// Enhanced task structure with security support
typedef struct task {
    unsigned int esp;
    unsigned int ebp;
    unsigned int eip;
    unsigned int state;              // Task state (unused, ready, running, etc.)
    unsigned int flags;              // Task flags
    unsigned int privilege_level;    // Privilege level (0-3)
    unsigned int stack_size;         // Stack size
    void* stack;                     // Task stack
    void* entry_point;               // Entry point
    uint32_t cr3;                    // Page directory physical address
    security_token_t* security_token;// Security token
    security_descriptor_t* security_descriptor; // Security descriptor
    int exit_code;                   // Task exit code
    int parent_id;                   // Parent task ID
    char name[64];                   // Task name
    unsigned int stack_data[1024];   // Stack (for backward compatibility)
} secure_task_t;

static secure_task_t tasks[MAX_TASKS];
static int current_task = -1;
static int num_tasks = 0;
static unsigned int task_switching_enabled = 0;

// Get the task's name (returns the actual name, not a pointer)
const char* get_task_name(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS) {
        return "Unknown";
    }
    
    if (tasks[task_id].state == TASK_STATE_UNUSED) {
        return "Unused";
    }
    
    if (tasks[task_id].name[0] == '\0') {
        return "Unnamed";
    }
    
    return tasks[task_id].name;
}

// Set a task's name for better identification
void set_task_name(int task_id, const char* name) {
    if (task_id < 0 || task_id >= MAX_TASKS || !name || tasks[task_id].state == TASK_STATE_UNUSED) {
        return;
    }
    
    // Copy name with bounds checking
    int i = 0;
    while (name[i] && i < 63) {
        tasks[task_id].name[i] = name[i];
        i++;
    }
    tasks[task_id].name[i] = '\0';
}

// Get the number of tasks in the system
int get_task_count() {
    return num_tasks;
}

// Get the ID of the currently running task
int get_current_task_id() {
    return current_task;
}

// Get information about a specific task
int get_task_info(int task_id, task_info_t* info) {
    if (task_id < 0 || task_id >= MAX_TASKS || !info || tasks[task_id].state == TASK_STATE_UNUSED) {
        return 0; // Invalid parameters
    }
    
    secure_task_t* task = &tasks[task_id];
    
    info->id = task_id;
    info->state = task->state;
    info->flags = task->flags;
    info->privilege_level = task->privilege_level;
    info->stack_size = task->stack_size;
    strncpy(info->name, task->name, 63);
    info->name[63] = '\0';
    info->is_current = (task_id == current_task);
    info->parent_id = task->parent_id;
    
    // Copy security info if available
    if (task->security_token) {
        info->user_sid = task->security_token->user;
    } else {
        // Default SID
        info->user_sid.authority = 0;
        info->user_sid.id = 0;
    }
    
    return 1; // Success
}

// Create a task with a name (basic version, for backward compatibility)
int create_named_task(void (*entry_point)(), const char* name) {
    // By default, create a kernel task with full privileges
    return create_secure_task(entry_point, name, TASK_FLAG_KERNEL, TASK_PRIV_KERNEL);
}

// Create a task with security attributes
int create_secure_task(void (*entry_point)(), const char* name, unsigned int flags, unsigned int privilege_level) {
    if (num_tasks >= MAX_TASKS) {
        log_error("TASK", "Task limit reached");
        return -1;
    }

    int task_id = num_tasks;
    secure_task_t* new_task = &tasks[task_id];
    
    // Clear task structure
    memset(new_task, 0, sizeof(secure_task_t));
    
    // Initialize task state
    new_task->eip = (unsigned int)entry_point;
    new_task->esp = (unsigned int)&new_task->stack_data[1023];
    new_task->ebp = new_task->esp;
    new_task->state = TASK_STATE_READY;
    new_task->flags = flags;
    new_task->privilege_level = privilege_level;
    new_task->stack_size = sizeof(new_task->stack_data);
    new_task->entry_point = entry_point;
    new_task->parent_id = current_task; // Set current task as parent
    
    // Set the task name if provided
    if (name) {
        strncpy(new_task->name, name, 63);
        new_task->name[63] = '\0';
    } else {
        strcpy(new_task->name, "Task");
    }
    
    // Create security token based on privilege level
    if (flags & TASK_FLAG_KERNEL) {
        new_task->security_token = security_create_token(0, NULL, 0, PRIV_LEVEL_KERNEL);
    } else if (flags & TASK_FLAG_DRIVER) {
        new_task->security_token = security_create_token(1, NULL, 0, PRIV_LEVEL_DRIVER);
    } else if (flags & TASK_FLAG_SYSTEM) {
        new_task->security_token = security_create_token(2, NULL, 0, PRIV_LEVEL_SYSTEM);
    } else if (flags & TASK_FLAG_USER) {
        new_task->security_token = security_create_token(100, NULL, 0, PRIV_LEVEL_USER);
    } else {
        // Default to kernel token
        new_task->security_token = security_create_token(0, NULL, 0, PRIV_LEVEL_KERNEL);
    }
    
    // Create a security descriptor for the task
    if (new_task->security_token) {
        new_task->security_descriptor = security_create_descriptor(
            new_task->security_token->user, 
            new_task->security_token->user  // Use same SID for group
        );
        
        // Add ACEs for this task
        if (new_task->security_descriptor) {
            // Allow full access for the owner
            security_add_ace(
                new_task->security_descriptor,
                ACE_TYPE_ACCESS_ALLOWED,
                ACE_FLAG_OBJECT_INHERIT,
                PERM_ALL,
                new_task->security_token->user
            );
            
            // Allow admin access
            security_sid_t admin_sid = {0, 0}; // System admin SID
            security_add_ace(
                new_task->security_descriptor,
                ACE_TYPE_ACCESS_ALLOWED,
                ACE_FLAG_OBJECT_INHERIT,
                PERM_ALL,
                admin_sid
            );
        }
    }
    
    // Set up memory isolation if requested
    if (flags & TASK_FLAG_ISOLATED) {
        // Create a new address space for the task
        uint32_t page_dir = paging_create_address_space(1); // Allow kernel access
        if (page_dir != 0) {
            new_task->cr3 = page_dir;
        }
    }
    
    num_tasks++;
    
    // If this is the first task, set it as current
    if (current_task == -1) {
        current_task = 0;
    }
    
    log_debug("TASK", "Created task %d (%s) with flags 0x%x and privilege level %d", 
             task_id, new_task->name, flags, privilege_level);
    
    return task_id;
}

// Create a user-mode task with proper isolation
int create_user_mode_task(void (*entry_point)(), const char* name, size_t stack_size) {
    // Create a task with user mode flags
    int task_id = create_secure_task(entry_point, name, 
                                   TASK_FLAG_USER | TASK_FLAG_ISOLATED, 
                                   TASK_PRIV_USER);
    if (task_id < 0) {
        return -1;
    }
    
    secure_task_t* task = &tasks[task_id];
    
    // Allocate a new stack in user space
    if (stack_size == 0) {
        stack_size = 0x4000; // Default 16K
    }
    
    // Round up to page boundary
    stack_size = (stack_size + 0xFFF) & ~0xFFF;
    
    // Allocate user-mode stack at a fixed address for now
    // In a real OS, this would be dynamic
    void* user_stack = (void*)0x80000000;
    
    // Map user stack pages with proper protection
    if (paging_map_user_memory(user_stack, stack_size, 1, 0) != 0) {
        // Failed to map stack pages
        log_error("TASK", "Failed to map user stack for task %d", task_id);
        task->state = TASK_STATE_UNUSED;
        num_tasks--;
        return -2;
    }
    
    // Allocate and map code pages
    void* code_addr = (void*)0x40000000; // User code area
    size_t code_size = 0x10000; // 64K for code
    
    if (paging_map_user_memory(code_addr, code_size, 0, 1) != 0) {
        // Failed to map code pages
        log_error("TASK", "Failed to map user code area for task %d", task_id);
        // Should clean up stack pages too
        task->state = TASK_STATE_UNUSED;
        num_tasks--;
        return -3;
    }
    
    // Update task structure
    task->stack = (void*)((uintptr_t)user_stack + stack_size); // Stack grows down
    task->stack_size = stack_size;
    task->esp = (unsigned int)task->stack;
    task->ebp = task->esp;
    
    log_info("TASK", "Created user-mode task %d (%s) with %d bytes stack at %p", 
            task_id, name, stack_size, user_stack);
    
    return task_id;
}

// Set a task's security token
int set_task_security_token(int task_id, security_token_t* token) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED || !token) {
        return -1;
    }
    
    // Check if we have permission to change the token
    if (!security_check_permission(PERM_CHANGE_PRIVILEGE)) {
        log_error("SECURITY", "Permission denied: Cannot change task %d security token", task_id);
        return -2;
    }
    
    // Free old token if it exists
    if (tasks[task_id].security_token) {
        security_free_token(tasks[task_id].security_token);
    }
    
    tasks[task_id].security_token = token;
    return 0;
}

// Get a task's security token
security_token_t* get_task_security_token(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return NULL;
    }
    
    return tasks[task_id].security_token;
}

// Set a task's security descriptor
int set_task_security_descriptor(int task_id, security_descriptor_t* descriptor) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED || !descriptor) {
        return -1;
    }
    
    // Check if we have permission to change security
    if (!security_check_permission(PERM_MODIFY_SECURITY)) {
        log_error("SECURITY", "Permission denied: Cannot change task %d security descriptor", task_id);
        return -2;
    }
    
    // Free old descriptor if it exists
    if (tasks[task_id].security_descriptor) {
        security_free_descriptor(tasks[task_id].security_descriptor);
    }
    
    tasks[task_id].security_descriptor = descriptor;
    return 0;
}

// Get a task's security descriptor
security_descriptor_t* get_task_security_descriptor(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return NULL;
    }
    
    return tasks[task_id].security_descriptor;
}

// Check if a task has a specific permission
int task_check_permission(int task_id, uint32_t permission) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return 0;
    }
    
    // Get the task's security token
    security_token_t* token = tasks[task_id].security_token;
    if (!token) {
        return 0;
    }
    
    // Kernel tasks have all permissions
    if (tasks[task_id].privilege_level == TASK_PRIV_KERNEL) {
        return 1;
    }
    
    // Check permission against the token
    return (token->privileges & permission) == permission;
}

// Check if the current task has access to another task
int check_access_to_task(int task_id, uint32_t desired_access) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return 0;
    }
    
    if (current_task < 0 || current_task >= MAX_TASKS) {
        return 0;
    }
    
    // Get security tokens
    security_token_t* current_token = tasks[current_task].security_token;
    security_descriptor_t* target_descriptor = tasks[task_id].security_descriptor;
    
    if (!current_token || !target_descriptor) {
        return 0;
    }
    
    // Check access
    return security_check_access(target_descriptor, current_token, desired_access);
}

// Suspend a task
int suspend_task(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return -1;
    }
    
    // Check permission to manage tasks
    if (!security_check_permission(PERM_CHANGE_PRIVILEGE)) {
        return -2;
    }
    
    // Don't suspend current task
    if (task_id == current_task) {
        return -3;
    }
    
    // Only suspend active tasks
    if (tasks[task_id].state == TASK_STATE_READY || tasks[task_id].state == TASK_STATE_RUNNING) {
        tasks[task_id].state = TASK_STATE_SUSPENDED;
        log_debug("TASK", "Suspended task %d (%s)", task_id, tasks[task_id].name);
        return 0;
    }
    
    return -4;
}

// Resume a suspended task
int resume_task(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return -1;
    }
    
    // Check permission to manage tasks
    if (!security_check_permission(PERM_CHANGE_PRIVILEGE)) {
        return -2;
    }
    
    // Only resume suspended tasks
    if (tasks[task_id].state == TASK_STATE_SUSPENDED) {
        tasks[task_id].state = TASK_STATE_READY;
        log_debug("TASK", "Resumed task %d (%s)", task_id, tasks[task_id].name);
        return 0;
    }
    
    return -3;
}

// Terminate a task
int terminate_task(int task_id, int exit_code) {
    if (task_id < 0 || task_id >= MAX_TASKS || tasks[task_id].state == TASK_STATE_UNUSED) {
        return -1;
    }
    
    // Check permission
    if (task_id != current_task && !security_check_permission(PERM_CHANGE_PRIVILEGE)) {
        return -2;
    }
    
    secure_task_t* task = &tasks[task_id];
    
    // Set as zombie unless it's detached
    if (task->flags & TASK_FLAG_DETACHED) {
        task->state = TASK_STATE_UNUSED;
    } else {
        task->state = TASK_STATE_ZOMBIE;
        task->exit_code = exit_code;
    }
    
    // Clean up resources
    if (task->security_token) {
        security_free_token(task->security_token);
        task->security_token = NULL;
    }
    
    if (task->security_descriptor) {
        security_free_descriptor(task->security_descriptor);
        task->security_descriptor = NULL;
    }
    
    // If task has its own address space, free it
    if (task->cr3 != 0) {
        // Would need an additional function to clean up page directory
    }
    
    log_info("TASK", "Terminated task %d (%s) with exit code %d", 
             task_id, task->name, exit_code);
    
    // If terminating the current task, force a task switch
    if (task_id == current_task) {
        current_task = -1; // Will cause next task to be selected
        switch_task();
    }
    
    return 0;
}

// Get a task's exit code (for parent task)
int get_task_exit_code(int task_id, int* exit_code) {
    if (task_id < 0 || task_id >= MAX_TASKS || !exit_code) {
        return -1;
    }
    
    secure_task_t* task = &tasks[task_id];
    
    // Check if task is a zombie (terminated but not yet cleaned up)
    if (task->state != TASK_STATE_ZOMBIE) {
        return -2; // Task not terminated
    }
    
    // Check if caller is the parent
    if (current_task != task->parent_id && !security_check_permission(PERM_CHANGE_PRIVILEGE)) {
        return -3; // Not parent task and no admin privileges
    }
    
    *exit_code = task->exit_code;
    
    // Clean up the zombie task
    task->state = TASK_STATE_UNUSED;
    
    return 0;
}

// Wait for a task to terminate
int wait_for_task(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS) {
        return -1;
    }
    
    // Check if caller is the parent or has admin privileges
    if (current_task != tasks[task_id].parent_id && !security_check_permission(PERM_CHANGE_PRIVILEGE)) {
        return -2;
    }
    
    // Simple busy waiting loop - in a real OS would block the calling task
    while (tasks[task_id].state != TASK_STATE_ZOMBIE && tasks[task_id].state != TASK_STATE_UNUSED) {
        // Yield to other tasks
        switch_task();
    }
    
    // Get the exit code
    int exit_code = 0;
    if (tasks[task_id].state == TASK_STATE_ZOMBIE) {
        exit_code = tasks[task_id].exit_code;
        tasks[task_id].state = TASK_STATE_UNUSED; // Clean up zombie
    }
    
    return exit_code;
}

// Create a basic task (for backward compatibility)
void create_task(void (*entry_point)()) {
    create_named_task(entry_point, "Task");
}

// Task switching code
void switch_task() {
    // Safety check: don't switch if task switching is disabled or we have 1 or 0 tasks
    if (!task_switching_enabled || num_tasks <= 1) {
        return;
    }

    // If no task is running, select the first ready one
    if (current_task == -1) {
        for (int i = 0; i < num_tasks; i++) {
            if (tasks[i].state == TASK_STATE_READY) {
                current_task = i;
                break;
            }
        }
        
        // If still no task, return
        if (current_task == -1) {
            return;
        }
        
        secure_task_t* next = &tasks[current_task];
        next->state = TASK_STATE_RUNNING;
        
        // Switch to task's address space if it has one
        if (next->cr3 != 0) {
            paging_switch_address_space(next->cr3);
        }
        
        // Switch to task's security token
        if (next->security_token) {
            security_set_current_token(next->security_token);
        }
        
        asm volatile("mov %0, %%esp" : : "r"(next->esp) : "memory");
        asm volatile("mov %0, %%ebp" : : "r"(next->ebp) : "memory");
        asm volatile("jmp *%0" : : "r"(next->eip) : "memory");
        return; // Will never reach here
    }
    
    // Find the next available task
    int next_task = (current_task + 1) % num_tasks;
    int checked = 0;
    
    while (checked < num_tasks) {
        if (tasks[next_task].state == TASK_STATE_READY) {
            break;
        }
        next_task = (next_task + 1) % num_tasks;
        checked++;
    }
    
    // If no other tasks are ready, continue with current task
    if (next_task == current_task || tasks[next_task].state != TASK_STATE_READY) {
        return;
    }

    secure_task_t* current = &tasks[current_task];
    secure_task_t* next = &tasks[next_task];

    // Save current task state
    if (current_task >= 0 && current_task < num_tasks && current->state == TASK_STATE_RUNNING) {
        current->state = TASK_STATE_READY;
        asm volatile("mov %%esp, %0" : "=r"(current->esp) : : "memory");
        asm volatile("mov %%ebp, %0" : "=r"(current->ebp) : : "memory");
    }

    // Switch to next task
    current_task = next_task;
    next->state = TASK_STATE_RUNNING;
    
    // Switch to task's address space if it has one
    if (next->cr3 != 0) {
        paging_switch_address_space(next->cr3);
    }
    
    // Switch to task's security token
    if (next->security_token) {
        security_set_current_token(next->security_token);
    }
    
    asm volatile("mov %0, %%esp" : : "r"(next->esp) : "memory");
    asm volatile("mov %0, %%ebp" : : "r"(next->ebp) : "memory");
    asm volatile("jmp *%0" : : "r"(next->eip) : "memory");
}

void initialize_multitasking() {
    // Initialize all tasks as unused
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_STATE_UNUSED;
        tasks[i].security_token = NULL;
        tasks[i].security_descriptor = NULL;
        tasks[i].name[0] = '\0';
    }
    
    current_task = -1;
    num_tasks = 0;
    task_switching_enabled = 1;  // Enable task switching by default
    
    log_info("TASK", "Task management system initialized");
}

// Helper function to enable/disable task switching
void set_task_switching(unsigned int enabled) {
    task_switching_enabled = enabled;
    
    if (enabled) {
        log_debug("TASK", "Task switching enabled");
    } else {
        log_debug("TASK", "Task switching disabled");
    }
}