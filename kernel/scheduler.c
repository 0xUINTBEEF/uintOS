#include "scheduler.h"
#include "task.h"
#include "sync.h"
#include "logging/log.h"
#include "../memory/heap.h"
#include "lapic.h"
#include <string.h>

// Scheduler configuration
#define MAX_TASKS 256
#define MAX_PRIORITY 32
#define TIME_SLICE_BASE 10  // Base time slice in milliseconds
#define TIME_SLICE_PRIORITY_FACTOR 2  // Additional ms per priority level
#define MAX_CPUS 16  // Maximum supported CPU cores

// Task queue for each priority level
typedef struct {
    task_t* tasks[MAX_TASKS];
    int head;
    int tail;
    int count;
} task_queue_t;

// Sleeping task structure
typedef struct sleeping_task {
    task_t* task;
    uint64_t wake_time;  // Tick count when task should wake up
    struct sleeping_task* next;
} sleeping_task_t;

// Child task wait entry
typedef struct waiting_parent {
    int parent_id;              // Parent task ID waiting
    int child_id;               // Child task ID being waited for
    int* exit_code_ptr;         // Pointer to store exit code
    int options;                // Wait options
    struct waiting_parent* next;
} waiting_parent_t;

// Per-CPU scheduler state
typedef struct {
    task_t* current_task;       // Currently running task on this CPU
    int is_active;              // Whether this CPU is running
    uint64_t total_switches;    // Task switches on this CPU
    uint64_t idle_ticks;        // Idle ticks on this CPU
    task_queue_t local_queue;   // CPU-local task queue
} cpu_state_t;

// Global scheduler state
static struct {
    task_queue_t ready_queues[MAX_PRIORITY];  // Ready queues for each priority
    sleeping_task_t* sleeping_tasks;          // List of sleeping tasks
    waiting_parent_t* waiting_parents;        // List of waiting parents
    task_t* idle_task;                        // Idle task (runs when nothing else can)
    spinlock_t lock;                          // Scheduler lock
    int preemption_enabled;                   // Whether preemption is enabled
    uint64_t total_switches;                  // Total number of task switches
    uint64_t scheduler_ticks;                 // Total scheduler ticks since boot
    int next_task_id;                         // Next task ID to assign
    int algorithm;                            // Current scheduling algorithm
    unsigned int quantum_ms;                  // Base quantum for all tasks
    
    // SMP support
    int num_cpus;                             // Number of available CPUs
    cpu_state_t cpu_states[MAX_CPUS];         // Per-CPU state
    spinlock_t smp_lock;                      // Lock for SMP operations
    int load_balance_counter;                 // Counter for periodic load balancing
} scheduler;

// Forward declarations for internal functions
static void scheduler_add_task(task_t* task);
static task_t* scheduler_get_next_task(void);
static void scheduler_add_sleeping_task(task_t* task, uint64_t wake_time_ms);
static void scheduler_check_sleeping_tasks(void);
static void scheduler_check_waiting_parents(int task_id, int exit_code);
static void scheduler_remove_task_from_ready_queue(int task_id);
static int scheduler_smp_get_target_cpu(void);
static void scheduler_load_balance(void);

// Initialize the scheduler
void scheduler_init(void) {
    log_info("Initializing priority-based scheduler");
    
    // Initialize scheduler data structures
    memset(&scheduler, 0, sizeof(scheduler));
    scheduler.preemption_enabled = 0;   // Start with preemption disabled
    scheduler.next_task_id = 1;         // Start task IDs at 1
    scheduler.sleeping_tasks = NULL;    // No sleeping tasks at start
    scheduler.waiting_parents = NULL;   // No waiting parents at start
    scheduler.algorithm = SCHEDULER_ALGORITHM_PRIORITY; // Default to priority scheduling
    scheduler.quantum_ms = TIME_SLICE_BASE; // Default quantum
    scheduler.num_cpus = 1;             // Default to single CPU
    
    spinlock_init(&scheduler.lock);
    spinlock_init(&scheduler.smp_lock);
    
    // Initialize ready queues
    for (int i = 0; i < MAX_PRIORITY; i++) {
        scheduler.ready_queues[i].head = 0;
        scheduler.ready_queues[i].tail = 0;
        scheduler.ready_queues[i].count = 0;
    }
    
    // Initialize CPU states (at least CPU 0)
    scheduler.cpu_states[0].is_active = 1;
    
    log_info("Scheduler initialized successfully");
}

// Initialize scheduler for multiple CPU cores
void scheduler_init_smp(int num_cpus) {
    if (num_cpus <= 0) num_cpus = 1;
    if (num_cpus > MAX_CPUS) num_cpus = MAX_CPUS;
    
    spinlock_acquire(&scheduler.smp_lock);
    
    scheduler.num_cpus = num_cpus;
    
    // Initialize all CPU states
    for (int i = 0; i < num_cpus; i++) {
        scheduler.cpu_states[i].is_active = 1;
        scheduler.cpu_states[i].current_task = NULL;
        scheduler.cpu_states[i].total_switches = 0;
        scheduler.cpu_states[i].idle_ticks = 0;
        scheduler.cpu_states[i].local_queue.head = 0;
        scheduler.cpu_states[i].local_queue.tail = 0;
        scheduler.cpu_states[i].local_queue.count = 0;
    }
    
    spinlock_release(&scheduler.smp_lock);
    
    log_info("SMP scheduler initialized with %d CPUs", num_cpus);
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

// Remove task from any ready queue
static void scheduler_remove_task_from_ready_queue(int task_id) {
    // Check all priority queues
    for (int priority = 0; priority < MAX_PRIORITY; priority++) {
        task_queue_t* queue = &scheduler.ready_queues[priority];
        
        // Search for the task in this queue
        for (int i = 0; i < queue->count; i++) {
            int idx = (queue->head + i) % MAX_TASKS;
            
            if (queue->tasks[idx] && queue->tasks[idx]->id == task_id) {
                // Found the task, remove it by shifting tasks
                for (int j = i; j < queue->count - 1; j++) {
                    int curr_idx = (queue->head + j) % MAX_TASKS;
                    int next_idx = (queue->head + j + 1) % MAX_TASKS;
                    queue->tasks[curr_idx] = queue->tasks[next_idx];
                }
                
                // Decrement count and adjust tail
                queue->count--;
                queue->tail = (queue->tail - 1 + MAX_TASKS) % MAX_TASKS;
                return;
            }
        }
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
    
    // Set parent ID to current task if it exists
    task_t* current = scheduler_get_current_task();
    if (current) {
        task->parent_id = current->id;
    } else {
        task->parent_id = 0; // No parent
    }
    
    // Copy name with safety check
    if (name) {
        strncpy(task->name, name, sizeof(task->name) - 1);
    } else {
        snprintf(task->name, sizeof(task->name), "Task_%d", task->id);
    }
    
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

// Get the current CPU ID
int scheduler_get_current_cpu(void) {
    // In a real implementation, this would use CPU-specific register or APIC ID
    // For now we'll just return 0, assuming we're on the boot CPU
    return 0;
}

// Find the optimal CPU to run a new task (for load balancing)
static int scheduler_smp_get_target_cpu(void) {
    if (scheduler.num_cpus <= 1) {
        return 0; // Single CPU system
    }
    
    // Find CPU with fewest ready tasks
    int target_cpu = 0;
    int min_tasks = INT_MAX;
    
    for (int i = 0; i < scheduler.num_cpus; i++) {
        if (scheduler.cpu_states[i].is_active) {
            int cpu_tasks = scheduler.cpu_states[i].local_queue.count;
            
            // If this CPU has fewer tasks, select it
            if (cpu_tasks < min_tasks) {
                min_tasks = cpu_tasks;
                target_cpu = i;
            }
        }
    }
    
    return target_cpu;
}

// Schedule the next task to run
void scheduler_schedule(void) {
    // Don't schedule if preemption is disabled
    if (!scheduler.preemption_enabled) {
        return;
    }
    
    spinlock_acquire(&scheduler.lock);
    
    // Get current CPU ID
    int cpu_id = scheduler_get_current_cpu();
    
    // Save current task if there is one
    if (scheduler.cpu_states[cpu_id].current_task && 
        scheduler.cpu_states[cpu_id].current_task->state == TASK_STATE_RUNNING) {
        scheduler.cpu_states[cpu_id].current_task->state = TASK_STATE_READY;
        scheduler_add_task(scheduler.cpu_states[cpu_id].current_task);
    }
    
    // Check for sleeping tasks that need to wake up
    scheduler_check_sleeping_tasks();
    
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
    scheduler.cpu_states[cpu_id].current_task = next_task;
    scheduler.cpu_states[cpu_id].total_switches++;
    scheduler.total_switches++;
    
    // Calculate time slice based on priority and algorithm
    int time_slice = 0;
    
    switch (scheduler.algorithm) {
        case SCHEDULER_ALGORITHM_ROUND_ROBIN:
            time_slice = scheduler.quantum_ms / MILLISECONDS_PER_TICK;
            break;
            
        case SCHEDULER_ALGORITHM_PRIORITY:
            // Higher priority (lower number) gets more time
            time_slice = scheduler.quantum_ms / MILLISECONDS_PER_TICK + 
                        (MAX_PRIORITY - 1 - next_task->priority) * TIME_SLICE_PRIORITY_FACTOR;
            break;
            
        default:
            // Default to priority-based
            time_slice = scheduler.quantum_ms / MILLISECONDS_PER_TICK + 
                        (MAX_PRIORITY - 1 - next_task->priority) * TIME_SLICE_PRIORITY_FACTOR;
    }
    
    next_task->time_slice = time_slice;
    next_task->last_run_time = scheduler.scheduler_ticks;
    
    // Update CPU time tracking
    if (next_task != scheduler.idle_task) {
        next_task->cpu_time_used++;
    } else {
        scheduler.cpu_states[cpu_id].idle_ticks++;
    }
    
    // Check if we need to perform load balancing
    scheduler.load_balance_counter++;
    if (scheduler.num_cpus > 1 && scheduler.load_balance_counter >= 100) {
        scheduler.load_balance_counter = 0;
        scheduler_load_balance();
    }
    
    spinlock_release(&scheduler.lock);
    
    // Perform the context switch
    task_switch_to(next_task);
}

// Perform load balancing across CPUs
static void scheduler_load_balance(void) {
    if (scheduler.num_cpus <= 1) return;
    
    // Find CPU with most and least tasks
    int max_cpu = 0;
    int min_cpu = 0;
    int max_tasks = 0;
    int min_tasks = INT_MAX;
    
    for (int i = 0; i < scheduler.num_cpus; i++) {
        if (scheduler.cpu_states[i].is_active) {
            int cpu_tasks = scheduler.cpu_states[i].local_queue.count;
            
            if (cpu_tasks > max_tasks) {
                max_tasks = cpu_tasks;
                max_cpu = i;
            }
            
            if (cpu_tasks < min_tasks) {
                min_tasks = cpu_tasks;
                min_cpu = i;
            }
        }
    }
    
    // If there's a significant imbalance, migrate a task
    if (max_tasks > min_tasks + 2 && max_cpu != min_cpu) {
        // Get a task from the most loaded CPU
        task_queue_t* max_queue = &scheduler.cpu_states[max_cpu].local_queue;
        if (max_queue->count > 0) {
            task_t* task = max_queue->tasks[max_queue->head];
            max_queue->head = (max_queue->head + 1) % MAX_TASKS;
            max_queue->count--;
            
            // Add it to the least loaded CPU's queue
            task_queue_t* min_queue = &scheduler.cpu_states[min_cpu].local_queue;
            if (min_queue->count < MAX_TASKS) {
                min_queue->tasks[min_queue->tail] = task;
                min_queue->tail = (min_queue->tail + 1) % MAX_TASKS;
                min_queue->count++;
                log_debug("Load balance: Migrated task %d (%s) from CPU %d to CPU %d",
                         task->id, task->name, max_cpu, min_cpu);
            } else {
                // If min_queue is full, put the task back in max_queue
                max_queue->tasks[max_queue->tail] = task;
                max_queue->tail = (max_queue->tail + 1) % MAX_TASKS;
                max_queue->count++;
            }
        }
    }
}

// Timer tick handler for the scheduler
void scheduler_tick(void) {
    scheduler.scheduler_ticks++;
    
    if (!scheduler.preemption_enabled) {
        return;
    }
    
    // Get current CPU ID
    int cpu_id = scheduler_get_current_cpu();
    task_t* current_task = scheduler.cpu_states[cpu_id].current_task;
    
    if (!current_task) {
        return;
    }
    
    // Update task timing information
    if (current_task->time_slice > 0) {
        current_task->time_slice--;
    }
    
    // If time slice exhausted, schedule another task
    if (current_task->time_slice == 0) {
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
    int cpu_id = scheduler_get_current_cpu();
    return scheduler.cpu_states[cpu_id].current_task;
}

// Task voluntarily yields the CPU
void scheduler_yield(void) {
    if (scheduler.preemption_enabled) {
        scheduler_schedule();
    }
}

// Block the current task
void scheduler_block_current_task(void) {
    int cpu_id = scheduler_get_current_cpu();
    task_t* current = scheduler.cpu_states[cpu_id].current_task;
    
    if (!current) return;
    
    spinlock_acquire(&scheduler.lock);
    current->state = TASK_STATE_BLOCKED;
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

// Add a task to the sleeping queue
static void scheduler_add_sleeping_task(task_t* task, uint64_t wake_time_ms) {
    if (!task) return;
    
    // Convert wake time to ticks
    uint64_t wake_time = scheduler.scheduler_ticks + (wake_time_ms / MILLISECONDS_PER_TICK);
    
    // Create new sleeping task entry
    sleeping_task_t* new_entry = (sleeping_task_t*)heap_alloc(sizeof(sleeping_task_t));
    if (!new_entry) {
        log_error("Failed to allocate sleeping task entry for task %d", task->id);
        return;
    }
    
    new_entry->task = task;
    new_entry->wake_time = wake_time;
    
    // Insert into ordered list (by wake time)
    if (!scheduler.sleeping_tasks || scheduler.sleeping_tasks->wake_time > wake_time) {
        // Insert at head
        new_entry->next = scheduler.sleeping_tasks;
        scheduler.sleeping_tasks = new_entry;
    } else {
        // Find position to insert
        sleeping_task_t* current = scheduler.sleeping_tasks;
        while (current->next && current->next->wake_time <= wake_time) {
            current = current->next;
        }
        
        // Insert after current
        new_entry->next = current->next;
        current->next = new_entry;
    }
    
    task->state = TASK_STATE_BLOCKED;  // Set task state to blocked while sleeping
    log_debug("Task %d sleeping until tick %llu (current: %llu)", 
             task->id, wake_time, scheduler.scheduler_ticks);
}

// Check for sleeping tasks that need to wake up
static void scheduler_check_sleeping_tasks(void) {
    sleeping_task_t* current = scheduler.sleeping_tasks;
    sleeping_task_t* prev = NULL;
    
    while (current) {
        if (current->wake_time <= scheduler.scheduler_ticks) {
            // Time to wake up this task
            task_t* task = current->task;
            
            if (task && task->state == TASK_STATE_BLOCKED) {
                task->state = TASK_STATE_READY;
                scheduler_add_task(task);
                log_debug("Woke up sleeping task %d", task->id);
            }
            
            // Remove from sleeping list
            sleeping_task_t* to_remove = current;
            if (prev) {
                prev->next = current->next;
                current = current->next;
            } else {
                scheduler.sleeping_tasks = current->next;
                current = scheduler.sleeping_tasks;
            }
            
            heap_free(to_remove);
        } else {
            // Move to next
            prev = current;
            current = current->next;
        }
    }
}

// Sleep the current task for the specified number of milliseconds
int scheduler_sleep(unsigned int milliseconds) {
    if (milliseconds == 0) {
        // Just yield if sleep time is 0
        scheduler_yield();
        return 0;
    }
    
    spinlock_acquire(&scheduler.lock);
    
    task_t* current = scheduler_get_current_task();
    if (!current) {
        spinlock_release(&scheduler.lock);
        return -1;
    }
    
    // Add to sleeping queue
    scheduler_add_sleeping_task(current, milliseconds);
    
    spinlock_release(&scheduler.lock);
    
    // Schedule another task (will not return until this task is awakened)
    scheduler_schedule();
    
    return 0;
}

// Wake up a sleeping task before its sleep period has expired
int scheduler_wake_task(int task_id) {
    spinlock_acquire(&scheduler.lock);
    
    // Find the task in sleeping list
    sleeping_task_t* current = scheduler.sleeping_tasks;
    sleeping_task_t* prev = NULL;
    
    while (current) {
        if (current->task && current->task->id == task_id) {
            // Found the task, wake it up
            task_t* task = current->task;
            
            if (task->state == TASK_STATE_BLOCKED) {
                task->state = TASK_STATE_READY;
                scheduler_add_task(task);
                log_debug("Force woke up sleeping task %d", task_id);
            }
            
            // Remove from sleeping list
            if (prev) {
                prev->next = current->next;
            } else {
                scheduler.sleeping_tasks = current->next;
            }
            
            heap_free(current);
            spinlock_release(&scheduler.lock);
            return 0;
        }
        
        prev = current;
        current = current->next;
    }
    
    spinlock_release(&scheduler.lock);
    return -1;  // Task not found in sleeping list
}

// Add a parent to the waiting list
static void scheduler_add_waiting_parent(int parent_id, int child_id, int* exit_code_ptr, int options) {
    // Create new waiting entry
    waiting_parent_t* new_entry = (waiting_parent_t*)heap_alloc(sizeof(waiting_parent_t));
    if (!new_entry) {
        log_error("Failed to allocate waiting parent entry");
        return;
    }
    
    new_entry->parent_id = parent_id;
    new_entry->child_id = child_id;
    new_entry->exit_code_ptr = exit_code_ptr;
    new_entry->options = options;
    
    // Add to list
    new_entry->next = scheduler.waiting_parents;
    scheduler.waiting_parents = new_entry;
    
    log_debug("Task %d now waiting for child %d", parent_id, child_id);
}

// Check if any parents are waiting for a terminated task
static void scheduler_check_waiting_parents(int task_id, int exit_code) {
    waiting_parent_t* current = scheduler.waiting_parents;
    waiting_parent_t* prev = NULL;
    
    while (current) {
        if (current->child_id == task_id) {
            // Found a waiting parent
            task_t* parent = scheduler_find_task_by_id(current->parent_id);
            
            // Store exit code if pointer provided
            if (current->exit_code_ptr) {
                *(current->exit_code_ptr) = exit_code;
            }
            
            // Unblock the parent if it's blocked
            if (parent && parent->state == TASK_STATE_BLOCKED) {
                parent->state = TASK_STATE_READY;
                scheduler_add_task(parent);
                log_debug("Unblocked waiting parent task %d", parent->id);
            }
            
            // Remove from waiting list
            if (prev) {
                prev->next = current->next;
            } else {
                scheduler.waiting_parents = current->next;
            }
            
            waiting_parent_t* to_free = current;
            current = current->next;
            heap_free(to_free);
        } else {
            prev = current;
            current = current->next;
        }
    }
}

// Wait for a child task to terminate
int scheduler_waitpid(int task_id, int* exit_code, int options) {
    spinlock_acquire(&scheduler.lock);
    
    task_t* current = scheduler_get_current_task();
    if (!current) {
        spinlock_release(&scheduler.lock);
        return -1;
    }
    
    task_t* child = scheduler_find_task_by_id(task_id);
    if (!child) {
        spinlock_release(&scheduler.lock);
        return -1;  // Task not found
    }
    
    // Check if this is actually a child of the current task
    if (child->parent_id != current->id) {
        spinlock_release(&scheduler.lock);
        return -2;  // Not a child
    }
    
    // If child already terminated (zombie), get exit code and return
    if (child->state == TASK_STATE_ZOMBIE) {
        if (exit_code) {
            *exit_code = child->exit_code;
        }
        
        // Free child resources
        if (child->stack) {
            heap_free(child->stack);
        }
        heap_free(child);
        
        spinlock_release(&scheduler.lock);
        return task_id;
    }
    
    // If WNOHANG option and child not terminated, return 0
    if ((options & 1) && child->state != TASK_STATE_ZOMBIE) {
        spinlock_release(&scheduler.lock);
        return 0;
    }
    
    // Add current task to waiting list
    scheduler_add_waiting_parent(current->id, task_id, exit_code, options);
    
    // Block the current task
    current->state = TASK_STATE_BLOCKED;
    
    spinlock_release(&scheduler.lock);
    
    // Schedule another task
    scheduler_schedule();
    
    // When we return here, the child has terminated
    return task_id;
}

// Find a task by ID
task_t* scheduler_find_task_by_id(int task_id) {
    // Check if it's the current task on any CPU
    for (int i = 0; i < scheduler.num_cpus; i++) {
        if (scheduler.cpu_states[i].current_task && 
            scheduler.cpu_states[i].current_task->id == task_id) {
            return scheduler.cpu_states[i].current_task;
        }
    }
    
    // Check ready queues
    for (int priority = 0; priority < MAX_PRIORITY; priority++) {
        task_queue_t* queue = &scheduler.ready_queues[priority];
        for (int i = 0; i < queue->count; i++) {
            int idx = (queue->head + i) % MAX_TASKS;
            if (queue->tasks[idx] && queue->tasks[idx]->id == task_id) {
                return queue->tasks[idx];
            }
        }
    }
    
    // Check sleeping tasks
    sleeping_task_t* current = scheduler.sleeping_tasks;
    while (current) {
        if (current->task && current->task->id == task_id) {
            return current->task;
        }
        current = current->next;
    }
    
    return NULL;
}

// Get scheduler statistics
void scheduler_get_stats(scheduler_stats_t* stats) {
    if (!stats) return;
    
    spinlock_acquire(&scheduler.lock);
    
    stats->total_tasks = 0;
    stats->ready_tasks = 0;
    stats->blocked_tasks = 0;
    stats->sleeping_tasks = 0;
    stats->zombie_tasks = 0;
    
    // Count tasks in ready queues
    for (int i = 0; i < MAX_PRIORITY; i++) {
        stats->ready_tasks += scheduler.ready_queues[i].count;
    }
    stats->total_tasks += stats->ready_tasks;
    
    // Count sleeping tasks
    sleeping_task_t* current = scheduler.sleeping_tasks;
    while (current) {
        stats->sleeping_tasks++;
        current = current->next;
    }
    stats->total_tasks += stats->sleeping_tasks;
    
    // Add current running tasks
    for (int i = 0; i < scheduler.num_cpus; i++) {
        if (scheduler.cpu_states[i].current_task) {
            stats->total_tasks++;
        }
    }
    
    stats->total_task_switches = scheduler.total_switches;
    stats->total_ticks = scheduler.scheduler_ticks;
    
    int cpu_id = scheduler_get_current_cpu();
    stats->current_task_id = scheduler.cpu_states[cpu_id].current_task ? 
                             scheduler.cpu_states[cpu_id].current_task->id : 0;
    
    spinlock_release(&scheduler.lock);
}

// Get per-CPU scheduler information
void scheduler_get_cpu_info(int cpu_id, cpu_scheduler_info_t* info) {
    if (!info || cpu_id < 0 || cpu_id >= scheduler.num_cpus) {
        return;
    }
    
    spinlock_acquire(&scheduler.smp_lock);
    
    info->cpu_id = cpu_id;
    info->is_active = scheduler.cpu_states[cpu_id].is_active;
    info->total_switches = scheduler.cpu_states[cpu_id].total_switches;
    info->idle_ticks = scheduler.cpu_states[cpu_id].idle_ticks;
    
    if (scheduler.cpu_states[cpu_id].current_task) {
        info->current_task = scheduler.cpu_states[cpu_id].current_task;
    } else {
        info->current_task = NULL;
    }
    
    spinlock_release(&scheduler.smp_lock);
}

// Get the number of active CPU cores
int scheduler_get_cpu_count(void) {
    return scheduler.num_cpus;
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
    
    // Update task state and exit code
    task->state = TASK_STATE_ZOMBIE;
    task->exit_code = exit_code;
    
    // Remove task from any scheduler queues
    scheduler_remove_task_from_ready_queue(task_id);
    
    // Check if any parents are waiting for this task
    scheduler_check_waiting_parents(task_id, exit_code);
    
    log_info("Task %d (%s) terminated with exit code %d", 
            task_id, task->name, exit_code);
    
    // If terminating current task, schedule new one
    int cpu_id = scheduler_get_current_cpu();
    if (scheduler.cpu_states[cpu_id].current_task && 
        scheduler.cpu_states[cpu_id].current_task->id == task_id) {
        spinlock_release(&scheduler.lock);
        scheduler_schedule(); // This will not return
    } else {
        spinlock_release(&scheduler.lock);
    }
    
    return 0;
}

// Migrate a task to a specific CPU
int scheduler_migrate_task(int task_id, int cpu_id) {
    if (cpu_id < 0 || cpu_id >= scheduler.num_cpus || !scheduler.cpu_states[cpu_id].is_active) {
        return -1;
    }
    
    spinlock_acquire(&scheduler.lock);
    
    // Find the task
    task_t* task = scheduler_find_task_by_id(task_id);
    if (!task) {
        spinlock_release(&scheduler.lock);
        return -2; // Task not found
    }
    
    // Remove task from any ready queue
    scheduler_remove_task_from_ready_queue(task_id);
    
    // Add to target CPU's local queue
    task_queue_t* queue = &scheduler.cpu_states[cpu_id].local_queue;
    if (queue->count < MAX_TASKS) {
        queue->tasks[queue->tail] = task;
        queue->tail = (queue->tail + 1) % MAX_TASKS;
        queue->count++;
        log_debug("Migrated task %d to CPU %d", task_id, cpu_id);
        spinlock_release(&scheduler.lock);
        return 0;
    } else {
        // If target CPU queue is full, put back in global queue
        scheduler_add_task(task);
        spinlock_release(&scheduler.lock);
        return -3; // Target CPU queue full
    }
}

// Set the scheduler algorithm
int scheduler_set_algorithm(int algorithm) {
    if (algorithm < 0 || algorithm > 3) {
        return -1;
    }
    
    scheduler.algorithm = algorithm;
    
    switch (algorithm) {
        case SCHEDULER_ALGORITHM_ROUND_ROBIN:
            log_info("Scheduler algorithm set to Round Robin");
            break;
        case SCHEDULER_ALGORITHM_PRIORITY:
            log_info("Scheduler algorithm set to Priority-based");
            break;
        case SCHEDULER_ALGORITHM_FAIR_SHARE:
            log_info("Scheduler algorithm set to Fair Share");
            break;
        case SCHEDULER_ALGORITHM_EDF:
            log_info("Scheduler algorithm set to Earliest Deadline First");
            break;
    }
    
    return 0;
}

// Set the time slice for a specific priority level
void scheduler_set_priority_time_slice(unsigned int priority, unsigned int time_slice_ms) {
    if (priority >= MAX_PRIORITY) {
        return;
    }
    
    // Store priority-specific time slice configuration
    // For simplicity, we're just logging this; in a real implementation, 
    // we'd have an array to store per-priority time slice values
    log_info("Set time slice for priority %d to %dms", priority, time_slice_ms);
}

// Set the quantum for all tasks (base time slice)
void scheduler_set_quantum(unsigned int quantum_ms) {
    scheduler.quantum_ms = quantum_ms;
    log_info("Set base scheduling quantum to %dms", quantum_ms);
}