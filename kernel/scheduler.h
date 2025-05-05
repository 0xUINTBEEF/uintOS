#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "task.h"
#include <stdint.h>

// Default stack size for new tasks
#define DEFAULT_STACK_SIZE 8192

// Task priority levels
#define PRIORITY_HIGHEST      0
#define PRIORITY_HIGH         8
#define PRIORITY_NORMAL      16
#define PRIORITY_LOW         24
#define PRIORITY_LOWEST      31
#define PRIORITY_IDLE        31

// Scheduler statistics structure
typedef struct {
    int total_tasks;               // Total number of tasks in the system
    uint64_t total_task_switches;  // Total number of task switches
    uint64_t total_ticks;          // Total scheduler ticks
    int current_task_id;           // ID of the currently running task
} scheduler_stats_t;

// Initialize the priority scheduler
void scheduler_init(void);

// Register the idle task (will run when no other tasks are ready)
void scheduler_register_idle_task(task_t* idle_task);

// Create a new task with the specified attributes
int scheduler_create_task(void (*entry_point)(), const char* name, 
                         unsigned int priority, unsigned int flags);

// Schedule the next task to run
void scheduler_schedule(void);

// Process a timer tick in the scheduler
void scheduler_tick(void);

// Enable/disable preemptive scheduling
void scheduler_enable_preemption(void);
void scheduler_disable_preemption(void);

// Get the currently running task
task_t* scheduler_get_current_task(void);

// Voluntarily yield the CPU to another task
void scheduler_yield(void);

// Block the currently running task
void scheduler_block_current_task(void);

// Unblock a previously blocked task
void scheduler_unblock_task(int task_id);

// Find a task by its ID
task_t* scheduler_find_task_by_id(int task_id);

// Get scheduler statistics
void scheduler_get_stats(scheduler_stats_t* stats);

// Change the priority of a task
int scheduler_set_task_priority(int task_id, unsigned int priority);

// Terminate a task with the specified exit code
int scheduler_terminate_task(int task_id, int exit_code);

// Setup the initial task context
void task_setup_context(task_t* task);

// Switch to the specified task
void task_switch_to(task_t* task);

#endif /* SCHEDULER_H */