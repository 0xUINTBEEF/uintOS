#include "task.h"
#include "io.h"

#define MAX_TASKS 256

// Task states
#define TASK_STATE_UNUSED 0
#define TASK_STATE_READY 1
#define TASK_STATE_RUNNING 2

struct task {
    unsigned int esp;
    unsigned int ebp;
    unsigned int eip;
    unsigned int state;  // Task state (unused, ready, running)
    unsigned int stack[1024];
};

struct task tasks[MAX_TASKS];
int current_task = -1;
int num_tasks = 0;
unsigned int task_switching_enabled = 0;

// Enhanced task management APIs

// Task names for better identification
static char *task_names[MAX_TASKS];

// Set a task's name for better identification in listings
void set_task_name(int task_id, const char *name) {
    if (task_id < 0 || task_id >= MAX_TASKS || !name) {
        return;
    }
    
    // Free previous name if it exists
    if (task_names[task_id]) {
        free(task_names[task_id]);
    }
    
    // Allocate and copy the new name
    size_t len = 0;
    const char *p = name;
    while (*p++) len++;
    
    task_names[task_id] = malloc(len + 1);
    if (task_names[task_id]) {
        char *dest = task_names[task_id];
        while ((*dest++ = *name++));
    }
}

// Get a task's name (returns "Unknown" if not set)
const char *get_task_name(int task_id) {
    if (task_id < 0 || task_id >= MAX_TASKS || !task_names[task_id]) {
        return "Unknown";
    }
    return task_names[task_id];
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
int get_task_info(int task_id, task_info_t *info) {
    if (task_id < 0 || task_id >= num_tasks || !info) {
        return 0; // Invalid parameters
    }
    
    info->id = task_id;
    info->state = tasks[task_id].state;
    info->stack_size = sizeof(tasks[task_id].stack);
    info->name = get_task_name(task_id);
    info->is_current = (task_id == current_task);
    
    return 1; // Success
}

// Create a task with a name
int create_named_task(void (*entry_point)(), const char *name) {
    if (num_tasks >= MAX_TASKS) {
        print("Task limit reached\n");
        return -1;
    }

    int task_id = num_tasks;
    struct task *new_task = &tasks[task_id];
    
    // Initialize task state
    new_task->eip = (unsigned int)entry_point;
    new_task->esp = (unsigned int)&new_task->stack[1023];
    new_task->ebp = new_task->esp;
    new_task->state = TASK_STATE_READY;
    
    // Set the task name if provided
    if (name) {
        set_task_name(task_id, name);
    }
    
    num_tasks++;
    
    // If this is the first task, set it as current
    if (current_task == -1) {
        current_task = 0;
    }
    
    return task_id;
}

void create_task(void (*entry_point)()) {
    if (num_tasks >= MAX_TASKS) {
        print("Task limit reached\n");
        return;
    }

    struct task *new_task = &tasks[num_tasks];
    
    // Initialize task state
    new_task->eip = (unsigned int)entry_point;
    new_task->esp = (unsigned int)&new_task->stack[1023];
    new_task->ebp = new_task->esp;
    new_task->state = TASK_STATE_READY;
    
    num_tasks++;
    
    // If this is the first task, set it as current
    if (current_task == -1) {
        current_task = 0;
    }
}

void switch_task() {
    // Safety check: don't switch if task switching is disabled or we have 1 or 0 tasks
    if (!task_switching_enabled || num_tasks <= 1) {
        return;
    }

    // If no task is running, select the first one
    if (current_task == -1) {
        current_task = 0;
        struct task *next = &tasks[current_task];
        next->state = TASK_STATE_RUNNING;
        asm volatile("mov %0, %%esp" : : "r"(next->esp) : "memory");
        asm volatile("mov %0, %%ebp" : : "r"(next->ebp) : "memory");
        asm volatile("jmp *%0" : : "r"(next->eip) : "memory");
        return; // Will never reach here
    }
    
    // Find the next available task
    int next_task = (current_task + 1) % num_tasks;
    while (next_task != current_task && tasks[next_task].state != TASK_STATE_READY) {
        next_task = (next_task + 1) % num_tasks;
    }
    
    // If no other tasks are ready, continue with current task
    if (next_task == current_task) {
        return;
    }

    struct task *current = &tasks[current_task];
    struct task *next = &tasks[next_task];

    // Save current task state (only if we have a valid current task)
    if (current_task >= 0 && current_task < num_tasks) {
        current->state = TASK_STATE_READY;
        asm volatile("mov %%esp, %0" : "=r"(current->esp) : : "memory");
        asm volatile("mov %%ebp, %0" : "=r"(current->ebp) : : "memory");
    }

    // Switch to next task
    current_task = next_task;
    next->state = TASK_STATE_RUNNING;
    asm volatile("mov %0, %%esp" : : "r"(next->esp) : "memory");
    asm volatile("mov %0, %%ebp" : : "r"(next->ebp) : "memory");
    asm volatile("jmp *%0" : : "r"(next->eip) : "memory");
}

void initialize_multitasking() {
    // Initialize all tasks as unused
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_STATE_UNUSED;
    }
    
    current_task = -1;
    num_tasks = 0;
    task_switching_enabled = 1;  // Enable task switching by default
}

// Helper function to enable/disable task switching
void set_task_switching(unsigned int enabled) {
    task_switching_enabled = enabled;
}