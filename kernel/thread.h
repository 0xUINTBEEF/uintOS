#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include "sync.h"

// Thread state definitions
#define THREAD_STATE_NEW      0
#define THREAD_STATE_READY    1
#define THREAD_STATE_RUNNING  2
#define THREAD_STATE_BLOCKED  3
#define THREAD_STATE_WAITING  4
#define THREAD_STATE_ZOMBIE   5
#define THREAD_STATE_DEAD     6

// Thread priority levels
#define THREAD_PRIORITY_LOWEST     0
#define THREAD_PRIORITY_LOW        1
#define THREAD_PRIORITY_NORMAL     2
#define THREAD_PRIORITY_HIGH       3
#define THREAD_PRIORITY_HIGHEST    4
#define THREAD_PRIORITY_REALTIME   5

// Thread flags
#define THREAD_FLAG_NONE           0x00
#define THREAD_FLAG_SYSTEM         0x01  // System thread
#define THREAD_FLAG_USER           0x02  // User thread
#define THREAD_FLAG_DETACHED       0x04  // Thread is detached
#define THREAD_FLAG_JOINABLE       0x08  // Thread can be joined

// Maximum thread name length
#define MAX_THREAD_NAME_LENGTH     32

// Maximum number of threads per task/process
#define MAX_THREADS_PER_TASK       16

// Thread ID type
typedef int thread_id_t;

// Thread handler type
typedef void (*thread_entry_t)(void*);

// Thread context structure - CPU registers state
typedef struct thread_context {
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
} thread_context_t;

// Thread control block
typedef struct thread {
    thread_id_t id;                                // Thread ID
    int task_id;                                   // Parent task/process ID
    char name[MAX_THREAD_NAME_LENGTH];            // Thread name
    int state;                                    // Current state
    int priority;                                 // Priority level
    int flags;                                    // Thread flags
    
    thread_entry_t entry_point;                   // Thread entry function
    void* arg;                                    // Thread argument
    
    thread_context_t context;                     // CPU context
    void* stack;                                  // Stack memory pointer
    size_t stack_size;                            // Stack size
    
    int exit_code;                                // Exit code
    semaphore_t join_semaphore;                   // For thread join functionality
    
    struct thread* next;                          // Next thread in list
    struct thread* prev;                          // Previous thread in list
} thread_t;

// Initialize the threading system
void thread_init(void);

// Create a new thread
thread_id_t thread_create(thread_entry_t entry_point, void* arg, size_t stack_size, 
                          int priority, int flags, const char* name);

// Get current thread ID
thread_id_t thread_get_current_id(void);

// Get current thread
thread_t* thread_get_current(void);

// Exit current thread
void thread_exit(int exit_code);

// Join a thread (wait for it to finish)
int thread_join(thread_id_t thread_id, int* exit_code);

// Detach a thread
int thread_detach(thread_id_t thread_id);

// Change thread priority
int thread_set_priority(thread_id_t thread_id, int priority);

// Get thread priority
int thread_get_priority(thread_id_t thread_id);

// Get thread by ID
thread_t* thread_get_by_id(thread_id_t thread_id);

// Yield execution to another thread
void thread_yield(void);

// Put current thread to sleep
void thread_sleep(uint32_t milliseconds);

// Wake up a sleeping thread
int thread_wake(thread_id_t thread_id);

// Get thread state
int thread_get_state(thread_id_t thread_id);

// Block a thread
void thread_block(void);

// Unblock a thread
int thread_unblock(thread_id_t thread_id);

// Get thread name
const char* thread_get_name(thread_id_t thread_id);

// Set thread name
int thread_set_name(thread_id_t thread_id, const char* name);

// Get total number of threads
int thread_get_count(void);

// Get number of threads in a specific task
int thread_get_count_by_task(int task_id);

// List all threads (for debug purposes)
void thread_list(void);

#endif // THREAD_H