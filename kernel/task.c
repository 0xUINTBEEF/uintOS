#include "task.h"
#include "io.h"

#define MAX_TASKS 256

struct task {
    unsigned int esp;
    unsigned int ebp;
    unsigned int eip;
    unsigned int stack[1024];
};

struct task tasks[MAX_TASKS];
int current_task = -1;
int num_tasks = 0;

void create_task(void (*entry_point)()) {
    if (num_tasks >= MAX_TASKS) {
        print("Task limit reached\n");
        return;
    }

    struct task *new_task = &tasks[num_tasks++];
    new_task->eip = (unsigned int)entry_point;
    new_task->esp = (unsigned int)&new_task->stack[1023];
    new_task->ebp = new_task->esp;
}

void switch_task() {
    if (num_tasks <= 1) return;

    int next_task = (current_task + 1) % num_tasks;

    struct task *current = &tasks[current_task];
    struct task *next = &tasks[next_task];

    // Save current task state
    asm volatile("mov %%esp, %0" : "=r"(current->esp));
    asm volatile("mov %%ebp, %0" : "=r"(current->ebp));

    // Switch to next task
    current_task = next_task;
    asm volatile("mov %0, %%esp" : : "r"(next->esp));
    asm volatile("mov %0, %%ebp" : : "r"(next->ebp));
    asm volatile("jmp *%0" : : "r"(next->eip));
}

void initialize_multitasking() {
    current_task = 0;
    num_tasks = 0;
}