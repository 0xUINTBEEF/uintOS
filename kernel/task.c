#include "task.h"
#include "scheduler.h"
#include "logging/log.h"
#include "../memory/heap.h"
#include "../hal/include/hal.h"
#include <string.h>

// Assembly functions for context switching
extern void task_context_switch(task_context_t* old_context, task_context_t* new_context);
extern void task_context_start(task_context_t* context);

// Maximum number of tasks supported
#define MAX_TASKS 256

// Global task table
static struct {
    task_t* tasks[MAX_TASKS];
    int current_task;
    int next_task_id;
    int task_count;
} task_manager;

// Initialize the task management system
void initialize_multitasking() {
    log_info("Initializing multitasking subsystem");
    memset(&task_manager, 0, sizeof(task_manager));
    task_manager.current_task = -1;
    task_manager.next_task_id = 1;  // Reserve 0 for kernel
    task_manager.task_count = 0;
    
    // Initialize the scheduler
    scheduler_init();
    
    log_info("Multitasking subsystem initialized");
}

// Set up the initial context for a task
void task_setup_context(task_t* task) {
    if (!task || !task->stack) {
        log_error("Failed to set up task context: invalid task or stack");
        return;
    }
    
    // Create a task context at the top of the stack
    task_context_t* context = (task_context_t*)((char*)task->stack + task->stack_size - sizeof(task_context_t));
    
    // Initialize the context with appropriate values
    memset(context, 0, sizeof(task_context_t));
    context->eip = (uint32_t)task->entry_point;
    context->ebp = (uint32_t)task->stack + task->stack_size - sizeof(task_context_t) - 32; // Some space for local vars
    
    // Save pointer to context in task structure
    task->context = context;
    
    log_debug("Task context set up for task %d (%s)", task->id, task->name);
}

// Switch to a different task
void task_switch_to(task_t* new_task) {
    if (!new_task) {
        log_error("Attempted to switch to NULL task");
        return;
    }
    
    // Get the current task
    task_t* current_task = scheduler_get_current_task();
    
    // If there's no current task or it's the same as the new task, nothing to do
    if (!current_task || current_task == new_task) {
        return;
    }
    
    // Perform the context switch
    task_context_switch(current_task->context, new_task->context);
}

// Create a new task with the given entry point and name
int create_named_task(void (*entry_point)(), const char *name) {
    return scheduler_create_task(entry_point, name, PRIORITY_NORMAL, TASK_FLAG_SYSTEM);
}

// Create a task with a specific priority
int create_task_with_priority(void (*entry_point)(), const char *name, unsigned int priority) {
    return scheduler_create_task(entry_point, name, priority, TASK_FLAG_SYSTEM);
}

// Create a task with security attributes
int create_secure_task(void (*entry_point)(), const char *name, 
                      unsigned int flags, unsigned int privilege_level) {
    int task_id = scheduler_create_task(entry_point, name, PRIORITY_NORMAL, flags);
    
    if (task_id > 0) {
        // Get the task
        task_t* task = scheduler_find_task_by_id(task_id);
        
        // Set privilege level
        if (task) {
            task->privilege_level = privilege_level;
            
            // Create security token for the task
            security_token_t* token = security_create_token(privilege_level);
            if (token) {
                set_task_security_token(task_id, token);
            } else {
                log_warning("Failed to create security token for task %d", task_id);
            }
            
            log_info("Created secure task '%s' (ID: %d, privilege: %d)", 
                    name, task_id, privilege_level);
        }
    }
    
    return task_id;
}

// Set the name of a task
void set_task_name(int task_id, const char *name) {
    task_t* task = scheduler_find_task_by_id(task_id);
    
    if (task && name) {
        strncpy(task->name, name, sizeof(task->name) - 1);
        task->name[sizeof(task->name) - 1] = '\0';
    }
}

// Get the name of a task
const char *get_task_name(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    return task ? task->name : "unknown";
}

// Get the currently executing task's ID
int get_current_task_id() {
    task_t* current = scheduler_get_current_task();
    return current ? current->id : 0;
}

// Get the number of tasks
int get_task_count() {
    scheduler_stats_t stats;
    scheduler_get_stats(&stats);
    return stats.total_tasks;
}

// Get information about a task
int get_task_info(int task_id, task_info_t *info) {
    if (!info) return -1;
    
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) return -1;
    
    // Fill in the task info structure
    info->id = task->id;
    info->state = task->state;
    info->flags = task->flags;
    info->privilege_level = task->privilege_level;
    info->priority = task->priority;
    info->stack_size = task->stack_size;
    strncpy(info->name, task->name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    
    // Check if this is the current task
    task_t* current = scheduler_get_current_task();
    info->is_current = (current && current->id == task_id) ? 1 : 0;
    
    info->parent_id = task->parent_id;
    info->cpu_time_used = task->cpu_time_used;
    info->uptime = hal_get_system_time() - task->creation_time;
    
    // Get security information if available
    if (task->security_token) {
        security_get_sid(task->security_token, &info->user_sid);
    } else {
        memset(&info->user_sid, 0, sizeof(info->user_sid));
    }
    
    return 0;
}

// Set the security token for a task
int set_task_security_token(int task_id, security_token_t* token) {
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) return -1;
    
    task->security_token = token;
    return 0;
}

// Get the security token for a task
security_token_t* get_task_security_token(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    return task ? task->security_token : NULL;
}

// Set security descriptor for a task
int set_task_security_descriptor(int task_id, security_descriptor_t* descriptor) {
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) return -1;
    
    task->security_descriptor = descriptor;
    return 0;
}

// Get security descriptor for a task
security_descriptor_t* get_task_security_descriptor(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    return task ? task->security_descriptor : NULL;
}

// Check if a task has the specified permission
int task_check_permission(int task_id, uint32_t permission) {
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task || !task->security_token) return 0;
    
    return security_check_permission(task->security_token, permission);
}

// Suspend a task
int suspend_task(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) return -1;
    
    // Can't suspend the current task
    task_t* current = scheduler_get_current_task();
    if (current && current->id == task_id) {
        log_warning("Cannot suspend the current task");
        return -1;
    }
    
    // Update task state
    if (task->state == TASK_STATE_READY || task->state == TASK_STATE_RUNNING) {
        task->state = TASK_STATE_SUSPENDED;
        log_debug("Task %d suspended", task_id);
        return 0;
    }
    
    return -1;
}

// Resume a suspended task
int resume_task(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) return -1;
    
    // Resume task if it was suspended
    if (task->state == TASK_STATE_SUSPENDED) {
        task->state = TASK_STATE_READY;
        scheduler_add_task(task);
        log_debug("Task %d resumed", task_id);
        return 0;
    }
    
    return -1;
}

// Terminate a task
int terminate_task(int task_id, int exit_code) {
    return scheduler_terminate_task(task_id, exit_code);
}

// Get a task's exit code
int get_task_exit_code(int task_id, int* exit_code) {
    if (!exit_code) return -1;
    
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task || task->state != TASK_STATE_ZOMBIE) {
        return -1;
    }
    
    *exit_code = task->exit_code;
    return 0;
}

// Wait for a task to terminate
int wait_for_task(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) return -1;
    
    // If task is already terminated, return immediately
    if (task->state == TASK_STATE_ZOMBIE) {
        return 0;
    }
    
    // Otherwise block until task is terminated
    // This is a simple busy wait for now
    // In a real system, we would block the current task
    while (1) {
        task = scheduler_find_task_by_id(task_id);
        if (!task || task->state == TASK_STATE_ZOMBIE) {
            return 0;
        }
        
        // Yield to other tasks
        scheduler_yield();
    }
    
    return 0;
}

// Create a user mode task (runs in Ring 3)
int create_user_mode_task(void (*entry_point)(), const char* name, size_t stack_size) {
    // Create the task with user flags and normal priority
    int task_id = scheduler_create_task(entry_point, name, PRIORITY_NORMAL, TASK_FLAG_USER);
    
    if (task_id > 0) {
        // Get the task
        task_t* task = scheduler_find_task_by_id(task_id);
        
        // Set privilege level to user mode
        if (task) {
            task->privilege_level = TASK_PRIV_USER;
            
            // Allocate a user mode stack if needed
            if (stack_size > 0 && stack_size != task->stack_size) {
                // Free the existing stack
                if (task->stack) {
                    heap_free(task->stack);
                }
                
                // Allocate a new stack
                task->stack = heap_alloc(stack_size);
                task->stack_size = stack_size;
                
                // If stack allocation failed, terminate the task
                if (!task->stack) {
                    scheduler_terminate_task(task_id, -1);
                    log_error("Failed to allocate user stack for task %d", task_id);
                    return -1;
                }
                
                // Set up the new context
                task_setup_context(task);
            }
            
            log_info("Created user mode task '%s' (ID: %d)", name, task_id);
        }
    }
    
    return task_id;
}

// Yield the CPU to another task
void task_yield() {
    scheduler_yield();
}

// Sleep for the specified number of milliseconds
int task_sleep(unsigned int ms) {
    uint64_t start_time = hal_get_system_time();
    uint64_t end_time = start_time + ms;
    
    while (hal_get_system_time() < end_time) {
        task_yield();
    }
    
    return 0;
}

// Set the priority of a task
int task_set_priority(int task_id, unsigned int priority) {
    return scheduler_set_task_priority(task_id, priority);
}

// Get the priority of a task
unsigned int task_get_priority(int task_id) {
    task_t* task = scheduler_find_task_by_id(task_id);
    return task ? task->priority : PRIORITY_NORMAL;
}

// Block a task
int task_block(int task_id) {
    task_t* current = scheduler_get_current_task();
    
    // If blocking the current task
    if (current && current->id == task_id) {
        scheduler_block_current_task();
        return 0;
    } else {
        task_t* task = scheduler_find_task_by_id(task_id);
        if (!task) return -1;
        
        task->state = TASK_STATE_BLOCKED;
        return 0;
    }
}

// Unblock a task
int task_unblock(int task_id) {
    scheduler_unblock_task(task_id);
    return 0;
}