#include "task.h"
#include "scheduler.h"
#include "preempt.h"

// Yield the CPU to another task
void task_yield() {
    // Record this as a voluntary task switch
    record_voluntary_switch();
    
    // Call scheduler to yield
    scheduler_yield();
}
