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
    int ready_tasks;               // Number of tasks in READY state
    int blocked_tasks;             // Number of tasks in BLOCKED state
    int sleeping_tasks;            // Number of tasks in SLEEPING state
    int zombie_tasks;              // Number of tasks in ZOMBIE state
} scheduler_stats_t;

// SMP-related scheduler information
typedef struct {
    int cpu_id;                     // CPU ID
    task_t* current_task;           // Currently running task on this CPU
    int is_active;                  // Whether this CPU is active
    uint64_t total_switches;        // Total task switches on this CPU
    uint64_t idle_ticks;            // Idle ticks count
} cpu_scheduler_info_t;

// Waiting reason codes
#define WAIT_REASON_NONE       0    // Not waiting
#define WAIT_REASON_CHILD      1    // Waiting for a child process to exit
#define WAIT_REASON_IO         2    // Waiting for I/O completion
#define WAIT_REASON_MUTEX      3    // Waiting for a mutex
#define WAIT_REASON_SEMAPHORE  4    // Waiting for a semaphore
#define WAIT_REASON_CONDITION  5    // Waiting for a condition variable
#define WAIT_REASON_SLEEP      6    // Sleeping for a specified time
#define WAIT_REASON_EVENT      7    // Waiting for a specific event
#define WAIT_REASON_USER       8    // User-defined waiting reason

// Time constants
#define MILLISECONDS_PER_TICK  10   // Assuming 100Hz timer (10ms per tick)

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

// Sleep the current task for the specified number of milliseconds
int scheduler_sleep(unsigned int milliseconds);

// Wake up a sleeping task before its sleep period has expired
int scheduler_wake_task(int task_id);

// Wait for a child task to terminate
int scheduler_waitpid(int task_id, int* exit_code, int options);

// Setup the initial task context
void task_setup_context(task_t* task);

// Switch to the specified task
void task_switch_to(task_t* task);

// SMP functions for multi-core support

// Initialize scheduler for multiple CPU cores
void scheduler_init_smp(int num_cpus);

// Get the number of active CPU cores
int scheduler_get_cpu_count(void);

// Get the current CPU ID
int scheduler_get_current_cpu(void);

// Migrate a task to a specific CPU
int scheduler_migrate_task(int task_id, int cpu_id);

// Get per-CPU scheduler information
void scheduler_get_cpu_info(int cpu_id, cpu_scheduler_info_t* info);

// Balance tasks across all CPUs (load balancing)
void scheduler_balance_tasks(void);

// Advanced scheduler configuration

// Set the scheduler algorithm
#define SCHEDULER_ALGORITHM_ROUND_ROBIN  0
#define SCHEDULER_ALGORITHM_PRIORITY     1
#define SCHEDULER_ALGORITHM_FAIR_SHARE   2
#define SCHEDULER_ALGORITHM_EDF          3  // Earliest Deadline First
int scheduler_set_algorithm(int algorithm);

// Set the time slice for a specific priority level
void scheduler_set_priority_time_slice(unsigned int priority, unsigned int time_slice_ms);

// Set the quantum for all tasks (base time slice)
void scheduler_set_quantum(unsigned int quantum_ms);

#endif /* SCHEDULER_H */