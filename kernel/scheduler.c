#include "scheduler.h"
#include "task.h"
#include "sync.h"
#include "logging/log.h"
#include "../memory/heap.h"
#include <string.h>

// Scheduler configuration
#define MAX_TASKS 256
#define MAX_PRIORITY 32
#define TIME_SLICE_BASE 10  // Base time slice in milliseconds
#define TIME_SLICE_PRIORITY_FACTOR 2  // Additional ms per priority level

// Task queue for each priority level
typedef struct {
    task_t* tasks[MAX_TASKS];
    int head;
    int tail;
    int count;
} task_queue_t;

// Global scheduler state
static struct {
    task_queue_t ready_queues[MAX_PRIORITY];  // Ready queues for each priority
    task_t* current_task;                     // Currently running task
    task_t* idle_task;                        // Idle task (runs when nothing else can)
    spinlock_t lock;                          // Scheduler lock
    int preemption_enabled;                   // Whether preemption is enabled
    uint64_t total_switches;                  // Total number of task switches
    uint64_t scheduler_ticks;                 // Total scheduler ticks since boot
    int next_task_id;                         // Next task ID to assign
} scheduler;

// Initialize the scheduler
void scheduler_init(void) {
    log_info("Initializing priority-based scheduler");
    
    // Initialize scheduler data structures
    memset(&scheduler, 0, sizeof(scheduler));
    scheduler.preemption_enabled = 0;  // Start with preemption disabled
    scheduler.next_task_id = 1;        // Start task IDs at 1
    spinlock_init(&scheduler.lock);
    
    // Initialize ready queues
    for (int i = 0; i < MAX_PRIORITY; i++) {
        scheduler.ready_queues[i].head = 0;
        scheduler.ready_queues[i].tail = 0;
        scheduler.ready_queues[i].count = 0;
    }
    
    log_info("Scheduler initialized successfully");
}

// Add a task to the appropriate ready queue based on priority
static void scheduler_add_task(task_t* task) {
    if (!task) return;
    
    // Determine priority queue (0 is highest, MAX_PRIORITY-1 is lowest)
    int priority = task->priority;
    if (priority >= MAX_PRIORITY) priority = MAX_PRIORITY - 1;
    if (priority < 0) priority = 0;
    
    // Add to the queue
    task_queue_t* queue = &scheduler.ready_queues[priority];
    if (queue->count < MAX_TASKS) {
        queue->tasks[queue->tail] = task;
        queue->tail = (queue->tail + 1) % MAX_TASKS;
        queue->count++;
    } else {
        log_error("Ready queue for priority %d is full, cannot add task %d", 
                 priority, task->id);
    }
}

// Remove and return the next task from the highest non-empty priority queue
static task_t* scheduler_get_next_task(void) {
    // Start from highest priority (lowest number)
    for (int priority = 0; priority < MAX_PRIORITY; priority++) {
        task_queue_t* queue = &scheduler.ready_queues[priority];
        if (queue->count > 0) {
            task_t* task = queue->tasks[queue->head];
            queue->head = (queue->head + 1) % MAX_TASKS;
            queue->count--;
            return task;
        }
    }
    
    // No ready tasks, return the idle task
    return scheduler.idle_task;
}

// Register the idle task
void scheduler_register_idle_task(task_t* idle_task) {
    scheduler.idle_task = idle_task;
    idle_task->state = TASK_STATE_READY;
    idle_task->priority = MAX_PRIORITY - 1;  // Lowest priority
}

// Create a new task with the given attributes
int scheduler_create_task(void (*entry_point)(), const char* name, 
                         unsigned int priority, unsigned int flags) {
    spinlock_acquire(&scheduler.lock);
    
    // Allocate task structure
    task_t* task = (task_t*)heap_alloc(sizeof(task_t));
    if (!task) {
        spinlock_release(&scheduler.lock);
        log_error("Failed to allocate memory for new task");
        return -1;
    }
    
    // Initialize task structure
    memset(task, 0, sizeof(task_t));
    task->id = scheduler.next_task_id++;
    task->state = TASK_STATE_READY;
    task->flags = flags;
    task->priority = priority;
    task->stack_size = DEFAULT_STACK_SIZE;
    task->entry_point = entry_point;
    strncpy(task->name, name ? name : "unnamed", sizeof(task->name) - 1);
    
    // Allocate stack
    task->stack = heap_alloc(task->stack_size);
    if (!task->stack) {
        heap_free(task);
        spinlock_release(&scheduler.lock);
        log_error("Failed to allocate stack for new task");
        return -1;
    }
    
    // Set up initial task context
    task_setup_context(task);
    
    // Add task to scheduler
    scheduler_add_task(task);
    log_info("Created task '%s' (ID: %d, priority: %d)", task->name, task->id, task->priority);
    
    spinlock_release(&scheduler.lock);
    return task->id;
}

// Schedule the next task to run
void scheduler_schedule(void) {
    // Don't schedule if preemption is disabled
    if (!scheduler.preemption_enabled) {
        return;
    }
    
    spinlock_acquire(&scheduler.lock);
    
    // Save current task if there is one
    if (scheduler.current_task && 
        scheduler.current_task->state == TASK_STATE_RUNNING) {
        scheduler.current_task->state = TASK_STATE_READY;
        scheduler_add_task(scheduler.current_task);
    }
    
    // Get the next task to run
    task_t* next_task = scheduler_get_next_task();
    if (!next_task) {
        // This should never happen if idle task is registered
        spinlock_release(&scheduler.lock);
        log_error("No tasks available to run!");
        return;
    }
    
    // Update task state
    next_task->state = TASK_STATE_RUNNING;
    scheduler.current_task = next_task;
    scheduler.total_switches++;
    
    // Calculate time slice based on priority
    int time_slice = TIME_SLICE_BASE + 
                    (MAX_PRIORITY - 1 - next_task->priority) * TIME_SLICE_PRIORITY_FACTOR;
    next_task->time_slice = time_slice;
    
    spinlock_release(&scheduler.lock);
    
    // Perform the context switch
    task_switch_to(next_task);
}

// Timer tick handler for the scheduler
void scheduler_tick(void) {
    scheduler.scheduler_ticks++;
    
    if (!scheduler.preemption_enabled || !scheduler.current_task) {
        return;
    }
    
    // Decrement time slice of current task
    if (scheduler.current_task->time_slice > 0) {
        scheduler.current_task->time_slice--;
    }
    
    // If time slice exhausted, schedule another task
    if (scheduler.current_task->time_slice == 0) {
        scheduler_schedule();
    }
}

// Enable preemptive scheduling
void scheduler_enable_preemption(void) {
    scheduler.preemption_enabled = 1;
    log_debug("Preemptive scheduling enabled");
}

// Disable preemptive scheduling
void scheduler_disable_preemption(void) {
    scheduler.preemption_enabled = 0;
    log_debug("Preemptive scheduling disabled");
}

// Get current task
task_t* scheduler_get_current_task(void) {
    return scheduler.current_task;
}

// Task voluntarily yields the CPU
void scheduler_yield(void) {
    if (scheduler.preemption_enabled) {
        scheduler_schedule();
    }
}

// Block the current task
void scheduler_block_current_task(void) {
    if (!scheduler.current_task) return;
    
    spinlock_acquire(&scheduler.lock);
    scheduler.current_task->state = TASK_STATE_BLOCKED;
    spinlock_release(&scheduler.lock);
    
    // Schedule another task
    scheduler_schedule();
}

// Unblock a task
void scheduler_unblock_task(int task_id) {
    spinlock_acquire(&scheduler.lock);
    
    // Find the task
    task_t* task = scheduler_find_task_by_id(task_id);
    if (task && task->state == TASK_STATE_BLOCKED) {
        task->state = TASK_STATE_READY;
        scheduler_add_task(task);
        log_debug("Unblocked task %d", task_id);
    }
    
    spinlock_release(&scheduler.lock);
}

// Find a task by ID
task_t* scheduler_find_task_by_id(int task_id) {
    // Check current task first
    if (scheduler.current_task && scheduler.current_task->id == task_id) {
        return scheduler.current_task;
    }
    
    // Search ready queues
    for (int priority = 0; priority < MAX_PRIORITY; priority++) {
        task_queue_t* queue = &scheduler.ready_queues[priority];
        for (int i = 0; i < queue->count; i++) {
            int idx = (queue->head + i) % MAX_TASKS;
            if (queue->tasks[idx]->id == task_id) {
                return queue->tasks[idx];
            }
        }
    }
    
    return NULL;
}

// Get scheduler statistics
void scheduler_get_stats(scheduler_stats_t* stats) {
    if (!stats) return;
    
    spinlock_acquire(&scheduler.lock);
    
    stats->total_tasks = 0;
    for (int i = 0; i < MAX_PRIORITY; i++) {
        stats->total_tasks += scheduler.ready_queues[i].count;
    }
    
    stats->total_task_switches = scheduler.total_switches;
    stats->total_ticks = scheduler.scheduler_ticks;
    stats->current_task_id = scheduler.current_task ? scheduler.current_task->id : 0;
    
    spinlock_release(&scheduler.lock);
}

// Set task priority
int scheduler_set_task_priority(int task_id, unsigned int priority) {
    if (priority >= MAX_PRIORITY) {
        priority = MAX_PRIORITY - 1;
    }
    
    spinlock_acquire(&scheduler.lock);
    
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) {
        spinlock_release(&scheduler.lock);
        return -1; // Task not found
    }
    
    task->priority = priority;
    log_debug("Set priority of task %d to %d", task_id, priority);
    
    spinlock_release(&scheduler.lock);
    return 0;
}

// Terminate a task
int scheduler_terminate_task(int task_id, int exit_code) {
    spinlock_acquire(&scheduler.lock);
    
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) {
        spinlock_release(&scheduler.lock);
        return -1; // Task not found
    }
    
    // Update task state
    task->state = TASK_STATE_ZOMBIE;
    task->exit_code = exit_code;
    
    // If terminating current task, schedule new one
    if (scheduler.current_task && scheduler.current_task->id == task_id) {
        spinlock_release(&scheduler.lock);
        scheduler_schedule(); // This will not return
    } else {
        spinlock_release(&scheduler.lock);
    }
    
    return 0;
}