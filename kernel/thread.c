#include "thread.h"
#include "task.h"
#include "sync.h"
#include "logging/log.h"
#include "../memory/heap.h"
#include "../hal/include/hal_timer.h"
#include <string.h>

// Default stack size for threads (16 KB)
#define DEFAULT_STACK_SIZE (16 * 1024)

// Maximum number of threads in the system
#define MAX_THREADS (MAX_TASKS * MAX_THREADS_PER_TASK)

// Thread table
static thread_t* threads[MAX_THREADS] = {NULL};
static int thread_count = 0;
static thread_id_t next_thread_id = 1;
static thread_id_t current_thread_id = 0;

// Thread list (for scheduling)
static thread_t* ready_threads_head = NULL;
static thread_t* ready_threads_tail = NULL;
static thread_t* blocked_threads_head = NULL;

// Global thread lock to protect thread operations
static spinlock_t thread_lock;

// Forward declarations for internal functions
static void thread_scheduler(void);
static void thread_clean_up(thread_t* thread);
static void thread_wrapper(void);
static void thread_add_to_ready_list(thread_t* thread);
static void thread_remove_from_ready_list(thread_t* thread);
static thread_t* thread_get_next_to_run(void);
static void thread_initialize_context(thread_t* thread);

/**
 * Initialize the threading system
 */
void thread_init(void) {
    log_info("THREAD", "Initializing threading system");
    
    // Initialize global thread lock
    spinlock_init(&thread_lock);
    
    // Initialize the thread table
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i] = NULL;
    }
    
    thread_count = 0;
    next_thread_id = 1;
    
    // Create an initial thread for the main task
    int main_task_id = get_current_task_id();
    if (main_task_id >= 0) {
        // Create a main thread for the initial task
        thread_t* main_thread = (thread_t*)malloc(sizeof(thread_t));
        if (main_thread) {
            // Initialize main thread
            memset(main_thread, 0, sizeof(thread_t));
            main_thread->id = 0; // Main thread gets ID 0
            main_thread->task_id = main_task_id;
            main_thread->state = THREAD_STATE_RUNNING;
            main_thread->priority = THREAD_PRIORITY_NORMAL;
            main_thread->flags = THREAD_FLAG_SYSTEM;
            
            strncpy(main_thread->name, "main", MAX_THREAD_NAME_LENGTH - 1);
            main_thread->name[MAX_THREAD_NAME_LENGTH - 1] = '\0';
            
            main_thread->stack = NULL; // The main thread uses the task's stack
            main_thread->stack_size = 0;
            
            // Initialize semaphore for join operations
            semaphore_init(&main_thread->join_semaphore, 0, 1);
            
            // Store the main thread in the thread table
            threads[0] = main_thread;
            thread_count = 1;
            current_thread_id = 0;
        }
    }
    
    log_info("THREAD", "Threading system initialized");
}

/**
 * Create a new thread
 * 
 * @param entry_point Thread entry function
 * @param arg Argument to pass to the thread function
 * @param stack_size Stack size in bytes, or 0 for default
 * @param priority Thread priority
 * @param flags Thread flags
 * @param name Thread name (optional, can be NULL)
 * @return Thread ID, or -1 on error
 */
thread_id_t thread_create(thread_entry_t entry_point, void* arg, size_t stack_size, 
                          int priority, int flags, const char* name) {
    if (!entry_point) {
        log_error("THREAD", "Thread creation failed: entry point is NULL");
        return -1;
    }
    
    // Validate priority
    if (priority < THREAD_PRIORITY_LOWEST || priority > THREAD_PRIORITY_REALTIME) {
        priority = THREAD_PRIORITY_NORMAL;
    }
    
    // Use default stack size if not specified
    if (stack_size == 0) {
        stack_size = DEFAULT_STACK_SIZE;
    }
    
    // Align stack size to 16 bytes
    stack_size = (stack_size + 15) & ~15;
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Check if we've reached the thread limit
    if (thread_count >= MAX_THREADS) {
        spinlock_release(&thread_lock);
        log_error("THREAD", "Thread creation failed: maximum thread limit reached");
        return -1;
    }
    
    // Find a free slot in the thread table
    int slot = -1;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        spinlock_release(&thread_lock);
        log_error("THREAD", "Thread creation failed: no free slots");
        return -1;
    }
    
    // Allocate memory for the thread control block
    thread_t* thread = (thread_t*)malloc(sizeof(thread_t));
    if (!thread) {
        spinlock_release(&thread_lock);
        log_error("THREAD", "Thread creation failed: could not allocate thread control block");
        return -1;
    }
    
    // Initialize the thread control block
    memset(thread, 0, sizeof(thread_t));
    thread->id = next_thread_id++;
    thread->task_id = get_current_task_id();
    thread->state = THREAD_STATE_NEW;
    thread->priority = priority;
    thread->flags = flags;
    
    // Set thread name if provided
    if (name) {
        strncpy(thread->name, name, MAX_THREAD_NAME_LENGTH - 1);
        thread->name[MAX_THREAD_NAME_LENGTH - 1] = '\0';
    } else {
        snprintf(thread->name, MAX_THREAD_NAME_LENGTH, "thread-%d", thread->id);
    }
    
    thread->entry_point = entry_point;
    thread->arg = arg;
    
    // Allocate stack memory
    thread->stack = malloc(stack_size);
    if (!thread->stack) {
        free(thread);
        spinlock_release(&thread_lock);
        log_error("THREAD", "Thread creation failed: could not allocate stack memory");
        return -1;
    }
    
    thread->stack_size = stack_size;
    
    // Initialize semaphore for join operations
    semaphore_init(&thread->join_semaphore, 0, 1);
    
    // Initialize thread context
    thread_initialize_context(thread);
    
    // Add thread to the table
    threads[slot] = thread;
    thread_count++;
    
    log_debug("THREAD", "Created thread %d (%s), priority %d, stack %d bytes", 
             thread->id, thread->name, priority, stack_size);
    
    // Add thread to the ready list
    thread->state = THREAD_STATE_READY;
    thread_add_to_ready_list(thread);
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return thread->id;
}

/**
 * Get current thread ID
 * 
 * @return Current thread ID
 */
thread_id_t thread_get_current_id(void) {
    return current_thread_id;
}

/**
 * Get current thread
 * 
 * @return Pointer to current thread control block
 */
thread_t* thread_get_current(void) {
    if (current_thread_id < 0 || current_thread_id >= MAX_THREADS) {
        return NULL;
    }
    return threads[current_thread_id];
}

/**
 * Exit current thread
 * 
 * @param exit_code Thread exit code
 */
void thread_exit(int exit_code) {
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    thread_t* thread = thread_get_current();
    if (!thread) {
        spinlock_release(&thread_lock);
        return;
    }
    
    log_debug("THREAD", "Thread %d (%s) exiting with code %d", 
             thread->id, thread->name, exit_code);
    
    // Set thread state and exit code
    thread->state = THREAD_STATE_ZOMBIE;
    thread->exit_code = exit_code;
    
    // Remove from ready list
    thread_remove_from_ready_list(thread);
    
    // Signal any thread waiting on this one
    semaphore_signal(&thread->join_semaphore);
    
    // If detached, clean up now
    if (thread->flags & THREAD_FLAG_DETACHED) {
        thread_clean_up(thread);
    }
    
    // Release the thread lock
    spinlock_release(&thread_lock);
    
    // Switch to the next thread
    // Note: This function will not return
    thread_scheduler();
    
    // We should never reach here
    while (1) {
        // Halt CPU
        asm volatile("hlt");
    }
}

/**
 * Join a thread (wait for it to finish)
 * 
 * @param thread_id Thread ID to join
 * @param exit_code Pointer to store the thread's exit code, or NULL
 * @return 0 on success, negative value on error
 */
int thread_join(thread_id_t thread_id, int* exit_code) {
    // Cannot join the current thread
    if (thread_id == current_thread_id) {
        return -1;
    }
    
    // Find the thread
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    
    // Cannot join a detached thread
    if (thread->flags & THREAD_FLAG_DETACHED) {
        return -1;
    }
    
    // If the thread is still running, wait for it
    if (thread->state != THREAD_STATE_ZOMBIE && thread->state != THREAD_STATE_DEAD) {
        // Wait on the join semaphore
        semaphore_wait(&thread->join_semaphore);
    }
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Store exit code if requested
    if (exit_code) {
        *exit_code = thread->exit_code;
    }
    
    // Clean up the thread resources
    thread_clean_up(thread);
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return 0;
}

/**
 * Detach a thread
 * 
 * @param thread_id Thread ID to detach
 * @return 0 on success, negative value on error
 */
int thread_detach(thread_id_t thread_id) {
    // Find the thread
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Mark as detached
    thread->flags |= THREAD_FLAG_DETACHED;
    
    // If already zombie, clean up now
    if (thread->state == THREAD_STATE_ZOMBIE) {
        thread_clean_up(thread);
    }
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return 0;
}

/**
 * Change thread priority
 * 
 * @param thread_id Thread ID
 * @param priority New priority
 * @return 0 on success, negative value on error
 */
int thread_set_priority(thread_id_t thread_id, int priority) {
    // Validate priority
    if (priority < THREAD_PRIORITY_LOWEST || priority > THREAD_PRIORITY_REALTIME) {
        return -1;
    }
    
    // Find the thread
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Change priority
    thread->priority = priority;
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return 0;
}

/**
 * Get thread priority
 * 
 * @param thread_id Thread ID
 * @return Thread priority, or -1 on error
 */
int thread_get_priority(thread_id_t thread_id) {
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    return thread->priority;
}

/**
 * Get thread by ID
 * 
 * @param thread_id Thread ID
 * @return Pointer to thread control block, or NULL if not found
 */
thread_t* thread_get_by_id(thread_id_t thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i] && threads[i]->id == thread_id) {
            return threads[i];
        }
    }
    
    return NULL;
}

/**
 * Yield execution to another thread
 */
void thread_yield(void) {
    thread_scheduler();
}

/**
 * Put current thread to sleep
 * 
 * @param milliseconds Sleep time in milliseconds
 */
void thread_sleep(uint32_t milliseconds) {
    // TODO: Implement a proper sleep queue with timer-based wakeup
    // For now, use a simple busy wait with thread yields
    
    // Calculate end time
    uint64_t start_time = hal_time_now_ns();
    uint64_t end_time = start_time + (milliseconds * 1000000ULL);
    
    while (hal_time_now_ns() < end_time) {
        // Yield to other threads while sleeping
        thread_yield();
    }
}

/**
 * Wake up a sleeping thread
 * 
 * @param thread_id Thread ID to wake
 * @return 0 on success, negative value on error
 */
int thread_wake(thread_id_t thread_id) {
    // TODO: Implement when proper sleep queue is added
    return -1;
}

/**
 * Get thread state
 * 
 * @param thread_id Thread ID
 * @return Thread state, or -1 on error
 */
int thread_get_state(thread_id_t thread_id) {
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    return thread->state;
}

/**
 * Block a thread
 */
void thread_block(void) {
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    thread_t* thread = thread_get_current();
    if (!thread) {
        spinlock_release(&thread_lock);
        return;
    }
    
    // Change state to blocked
    thread->state = THREAD_STATE_BLOCKED;
    
    // Remove from ready list
    thread_remove_from_ready_list(thread);
    
    // Add to blocked list
    thread->next = blocked_threads_head;
    if (blocked_threads_head) {
        blocked_threads_head->prev = thread;
    }
    thread->prev = NULL;
    blocked_threads_head = thread;
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    // Switch to the next thread
    thread_scheduler();
}

/**
 * Unblock a thread
 * 
 * @param thread_id Thread ID to unblock
 * @return 0 on success, negative value on error
 */
int thread_unblock(thread_id_t thread_id) {
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Check if thread is blocked
    if (thread->state != THREAD_STATE_BLOCKED) {
        spinlock_release(&thread_lock);
        return -1;
    }
    
    // Remove from blocked list
    if (thread->prev) {
        thread->prev->next = thread->next;
    } else {
        blocked_threads_head = thread->next;
    }
    
    if (thread->next) {
        thread->next->prev = thread->prev;
    }
    
    // Change state to ready
    thread->state = THREAD_STATE_READY;
    
    // Add to ready list
    thread_add_to_ready_list(thread);
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return 0;
}

/**
 * Get thread name
 * 
 * @param thread_id Thread ID
 * @return Thread name, or NULL on error
 */
const char* thread_get_name(thread_id_t thread_id) {
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return NULL;
    }
    return thread->name;
}

/**
 * Set thread name
 * 
 * @param thread_id Thread ID
 * @param name New thread name
 * @return 0 on success, negative value on error
 */
int thread_set_name(thread_id_t thread_id, const char* name) {
    if (!name) {
        return -1;
    }
    
    thread_t* thread = thread_get_by_id(thread_id);
    if (!thread) {
        return -1;
    }
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Set thread name
    strncpy(thread->name, name, MAX_THREAD_NAME_LENGTH - 1);
    thread->name[MAX_THREAD_NAME_LENGTH - 1] = '\0';
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return 0;
}

/**
 * Get total number of threads
 * 
 * @return Number of threads in the system
 */
int thread_get_count(void) {
    return thread_count;
}

/**
 * Get number of threads in a specific task
 * 
 * @param task_id Task ID
 * @return Number of threads in the task
 */
int thread_get_count_by_task(int task_id) {
    int count = 0;
    
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Count threads in the specified task
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i] && threads[i]->task_id == task_id) {
            count++;
        }
    }
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    return count;
}

/**
 * List all threads (for debug purposes)
 */
void thread_list(void) {
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    log_info("THREAD", "--- Thread List ---");
    log_info("THREAD", "Total threads: %d", thread_count);
    log_info("THREAD", "ID | Task | State | Priority | Name");
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i]) {
            thread_t* t = threads[i];
            const char* state_str = "UNKNOWN";
            
            switch (t->state) {
                case THREAD_STATE_NEW:     state_str = "NEW"; break;
                case THREAD_STATE_READY:   state_str = "READY"; break;
                case THREAD_STATE_RUNNING: state_str = "RUNNING"; break;
                case THREAD_STATE_BLOCKED: state_str = "BLOCKED"; break;
                case THREAD_STATE_WAITING: state_str = "WAITING"; break;
                case THREAD_STATE_ZOMBIE:  state_str = "ZOMBIE"; break;
                case THREAD_STATE_DEAD:    state_str = "DEAD"; break;
            }
            
            log_info("THREAD", "%2d | %4d | %6s | %8d | %s%s", 
                    t->id, t->task_id, state_str, t->priority, 
                    t->name, (t->id == current_thread_id) ? " (current)" : "");
        }
    }
    
    // Release thread lock
    spinlock_release(&thread_lock);
}

/***********************************************
 * Internal helper functions
 ***********************************************/

/**
 * Thread context initialization
 * 
 * @param thread Thread to initialize context for
 */
static void thread_initialize_context(thread_t* thread) {
    // Base address of the stack (top)
    uint8_t* stack_top = (uint8_t*)thread->stack + thread->stack_size;
    
    // Align stack top to 16 bytes
    stack_top = (uint8_t*)((uintptr_t)stack_top & ~15UL);
    
    // Initialize stack with the thread wrapper function
    // This function will call the thread entry point
    
    // Push thread argument
    stack_top -= sizeof(void*);
    *(void**)stack_top = thread->arg;
    
    // Push thread entry point
    stack_top -= sizeof(void*);
    *(void**)stack_top = thread->entry_point;
    
    // Push return address (thread wrapper)
    stack_top -= sizeof(void*);
    *(void**)stack_top = (void*)thread_wrapper;
    
    // Initialize CPU context
    memset(&thread->context, 0, sizeof(thread_context_t));
    thread->context.esp = (uint32_t)stack_top;
    thread->context.ebp = (uint32_t)stack_top;
    thread->context.eip = (uint32_t)thread_wrapper;
    thread->context.eflags = 0x202; // IF=1, bit 1 is always 1
}

/**
 * Thread wrapper function
 * 
 * This function is called when a thread starts and handles calling the
 * thread entry point and cleaning up when the thread function returns.
 */
static void thread_wrapper(void) {
    // Get our thread structure
    thread_t* thread = thread_get_current();
    if (!thread) {
        // Something went wrong
        log_error("THREAD", "Thread wrapper: couldn't get current thread");
        return;
    }
    
    // The entry point and argument are on the stack (set up by thread_initialize_context)
    // Get them and call the thread function
    thread_entry_t entry = thread->entry_point;
    void* arg = thread->arg;
    
    // Call the thread entry point
    log_debug("THREAD", "Thread %d (%s) starting", thread->id, thread->name);
    entry(arg);
    
    // The thread function has returned, exit the thread
    log_debug("THREAD", "Thread %d (%s) returned", thread->id, thread->name);
    thread_exit(0);
}

/**
 * Add a thread to the ready list
 * 
 * @param thread Thread to add
 * @note Assumes the thread lock is already held
 */
static void thread_add_to_ready_list(thread_t* thread) {
    if (!thread) {
        return;
    }
    
    // Add to the end of the ready list
    thread->next = NULL;
    thread->prev = ready_threads_tail;
    
    if (ready_threads_tail) {
        ready_threads_tail->next = thread;
    } else {
        ready_threads_head = thread;
    }
    
    ready_threads_tail = thread;
}

/**
 * Remove a thread from the ready list
 * 
 * @param thread Thread to remove
 * @note Assumes the thread lock is already held
 */
static void thread_remove_from_ready_list(thread_t* thread) {
    if (!thread) {
        return;
    }
    
    // Remove from the ready list
    if (thread->prev) {
        thread->prev->next = thread->next;
    } else {
        ready_threads_head = thread->next;
    }
    
    if (thread->next) {
        thread->next->prev = thread->prev;
    } else {
        ready_threads_tail = thread->prev;
    }
    
    thread->next = NULL;
    thread->prev = NULL;
}

/**
 * Get the next thread to run
 * 
 * @return Next thread to run, or NULL if none
 * @note Assumes the thread lock is already held
 */
static thread_t* thread_get_next_to_run(void) {
    // Start with the highest priority thread
    thread_t* best = NULL;
    int best_priority = -1;
    
    // Find the highest priority thread in the ready list
    for (thread_t* t = ready_threads_head; t != NULL; t = t->next) {
        if (t->state == THREAD_STATE_READY && t->priority > best_priority) {
            best = t;
            best_priority = t->priority;
        }
    }
    
    return best;
}

/**
 * Thread scheduler
 * 
 * This function chooses the next thread to run and switches to it.
 */
static void thread_scheduler(void) {
    // Acquire thread lock
    spinlock_acquire(&thread_lock);
    
    // Get current thread
    thread_t* current = thread_get_current();
    
    // Get next thread to run
    thread_t* next = thread_get_next_to_run();
    
    if (!next) {
        // No other thread to run
        spinlock_release(&thread_lock);
        return;
    }
    
    // If current thread is still running, add it back to ready list
    if (current && current->state == THREAD_STATE_RUNNING) {
        current->state = THREAD_STATE_READY;
    }
    
    // Mark next thread as running
    next->state = THREAD_STATE_RUNNING;
    current_thread_id = next->id;
    
    // Get the context pointers
    thread_context_t* from_ctx = current ? &current->context : NULL;
    thread_context_t* to_ctx = &next->context;
    
    // Release thread lock
    spinlock_release(&thread_lock);
    
    // Perform context switch
    if (from_ctx) {
        // Save current context and switch to new context
        asm volatile(
            "pushf\n"                    // Save EFLAGS
            "push %%ebp\n"               // Save EBP
            "mov %%esp, %0\n"            // Save ESP
            "mov %1, %%esp\n"            // Load new ESP
            "mov %%cr3, %%eax\n"         // Get CR3 value
            "mov %%eax, %%cr3\n"         // Reload CR3 to flush TLB
            "pop %%ebp\n"                // Restore EBP
            "popf\n"                     // Restore EFLAGS
            : "=m" (from_ctx->esp)
            : "m" (to_ctx->esp)
            : "memory", "cc", "eax"
        );
    } else {
        // Just switch to the new context without saving current
        asm volatile(
            "mov %0, %%esp\n"            // Load new ESP
            "mov %%cr3, %%eax\n"         // Get CR3 value
            "mov %%eax, %%cr3\n"         // Reload CR3 to flush TLB
            "pop %%ebp\n"                // Restore EBP
            "popf\n"                     // Restore EFLAGS
            "ret\n"                      // Jump to EIP
            :
            : "m" (to_ctx->esp)
            : "memory", "cc", "eax"
        );
    }
}

/**
 * Clean up a thread's resources
 * 
 * @param thread Thread to clean up
 * @note Assumes the thread lock is already held
 */
static void thread_clean_up(thread_t* thread) {
    if (!thread) {
        return;
    }
    
    // Remove from any lists
    thread_remove_from_ready_list(thread);
    
    // Mark slot as free
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i] == thread) {
            threads[i] = NULL;
            break;
        }
    }
    
    // Free the thread's stack
    if (thread->stack) {
        free(thread->stack);
        thread->stack = NULL;
    }
    
    // Free the thread control block
    free(thread);
    
    // Update thread count
    thread_count--;
}