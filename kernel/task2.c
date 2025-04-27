#include "task.h"
#include "io.h"

// Simple idle task that can demonstrate our task management
void idle_task() {
    while (1) {
        // In a real system, this would use a proper CPU idle instruction
        // For now, we'll just have an empty loop that can be switched away from
        
        // This would be where we'd use the HLT instruction in a real system
        // asm volatile("hlt");
        
        // Yield to other tasks
        switch_task();
    }
}

// Simple task that counts up to demonstrate execution
void counter_task() {
    unsigned int counter = 0;
    
    while (1) {
        counter++;
        
        // Every so often, trigger a task switch
        // This simulates a task doing work and then yielding
        if ((counter % 1000000) == 0) {
            switch_task();
        }
    }
}