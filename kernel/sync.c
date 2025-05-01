#include "sync.h"
#include "task.h"
#include "logging/log.h"

/**
 * Initialize a spinlock
 * 
 * @param lock Spinlock to initialize
 */
void spinlock_init(spinlock_t* lock) {
    if (lock) {
        lock->lock = 0;
    }
}

/**
 * Acquire a spinlock
 * 
 * @param lock Spinlock to acquire
 */
void spinlock_acquire(spinlock_t* lock) {
    if (!lock) {
        return;
    }
    
    // Simple spinlock implementation with atomic test-and-set
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        // While waiting for the lock to be released, yield to other tasks
        // This prevents a task from monopolizing CPU time while waiting
        switch_task();
    }
}

/**
 * Try to acquire a spinlock (non-blocking)
 * 
 * @param lock Spinlock to acquire
 * @return 1 if acquired, 0 if already locked
 */
int spinlock_try_acquire(spinlock_t* lock) {
    if (!lock) {
        return 0;
    }
    
    // Non-blocking test-and-set
    return !__sync_lock_test_and_set(&lock->lock, 1);
}

/**
 * Release a spinlock
 * 
 * @param lock Spinlock to release
 */
void spinlock_release(spinlock_t* lock) {
    if (!lock) {
        return;
    }
    
    // Release the lock with a memory barrier
    __sync_lock_release(&lock->lock);
}

/**
 * Check if a spinlock is held
 * 
 * @param lock Spinlock to check
 * @return 1 if held, 0 if not held
 */
int spinlock_is_held(spinlock_t* lock) {
    return lock && lock->lock == 1;
}

/**
 * Initialize a mutex
 * 
 * @param mutex Mutex to initialize
 */
void mutex_init(mutex_t* mutex) {
    if (mutex) {
        spinlock_init(&mutex->spinlock);
        mutex->owner_task = -1;
        mutex->lock_count = 0;
    }
}

/**
 * Lock a mutex
 * 
 * @param mutex Mutex to lock
 */
void mutex_lock(mutex_t* mutex) {
    if (!mutex) {
        return;
    }
    
    int current_task_id = get_current_task_id();
    
    // Check if we already own this mutex (re-entrant mutex support)
    if (mutex->owner_task == current_task_id) {
        // We already own it, just increment the lock count
        mutex->lock_count++;
        return;
    }
    
    // Acquire the spinlock that protects the mutex structure
    spinlock_acquire(&mutex->spinlock);
    
    // Check if the mutex is already owned
    while (mutex->owner_task != -1) {
        // Release the spinlock before waiting
        spinlock_release(&mutex->spinlock);
        
        // Yield to other tasks while waiting
        switch_task();
        
        // Try to acquire the spinlock again to check the mutex state
        spinlock_acquire(&mutex->spinlock);
    }
    
    // We now own the mutex
    mutex->owner_task = current_task_id;
    mutex->lock_count = 1;
    
    // Release the spinlock
    spinlock_release(&mutex->spinlock);
}

/**
 * Try to lock a mutex (non-blocking)
 * 
 * @param mutex Mutex to lock
 * @return 1 if mutex was locked, 0 if already locked by another task
 */
int mutex_try_lock(mutex_t* mutex) {
    if (!mutex) {
        return 0;
    }
    
    int current_task_id = get_current_task_id();
    
    // Check if we already own this mutex (re-entrant mutex support)
    if (mutex->owner_task == current_task_id) {
        // We already own it, just increment the lock count
        mutex->lock_count++;
        return 1;
    }
    
    // Try to acquire the spinlock that protects the mutex structure
    if (!spinlock_try_acquire(&mutex->spinlock)) {
        // Couldn't get the spinlock, so mutex is probably owned
        return 0;
    }
    
    // Check if the mutex is already owned
    int result = 0;
    if (mutex->owner_task == -1) {
        // We can take ownership
        mutex->owner_task = current_task_id;
        mutex->lock_count = 1;
        result = 1;
    }
    
    // Release the spinlock
    spinlock_release(&mutex->spinlock);
    
    return result;
}

/**
 * Unlock a mutex
 * 
 * @param mutex Mutex to unlock
 */
void mutex_unlock(mutex_t* mutex) {
    if (!mutex) {
        return;
    }
    
    int current_task_id = get_current_task_id();
    
    // Check if we own this mutex
    if (mutex->owner_task != current_task_id) {
        log_warning("SYNC", "Task %d attempted to unlock mutex owned by task %d",
                   current_task_id, mutex->owner_task);
        return;
    }
    
    // Acquire the spinlock that protects the mutex structure
    spinlock_acquire(&mutex->spinlock);
    
    // Decrement the lock count
    mutex->lock_count--;
    
    // If lock count is zero, release ownership
    if (mutex->lock_count == 0) {
        mutex->owner_task = -1;
    }
    
    // Release the spinlock
    spinlock_release(&mutex->spinlock);
}

/**
 * Initialize a semaphore
 * 
 * @param sem Semaphore to initialize
 * @param initial_count Initial count value
 * @param max_count Maximum count value
 */
void semaphore_init(semaphore_t* sem, int initial_count, int max_count) {
    if (sem) {
        spinlock_init(&sem->spinlock);
        sem->count = initial_count;
        sem->max_count = max_count;
    }
}

/**
 * Wait on a semaphore
 * 
 * @param sem Semaphore to wait on
 */
void semaphore_wait(semaphore_t* sem) {
    if (!sem) {
        return;
    }
    
    while (1) {
        // Acquire the spinlock
        spinlock_acquire(&sem->spinlock);
        
        // Check if the semaphore count is > 0
        if (sem->count > 0) {
            // Decrement the count and return
            sem->count--;
            spinlock_release(&sem->spinlock);
            return;
        }
        
        // Release the spinlock before yielding
        spinlock_release(&sem->spinlock);
        
        // Yield to other tasks while waiting
        switch_task();
    }
}

/**
 * Try to wait on a semaphore (non-blocking)
 * 
 * @param sem Semaphore to wait on
 * @return 1 if successful (semaphore acquired), 0 if would block
 */
int semaphore_try_wait(semaphore_t* sem) {
    if (!sem) {
        return 0;
    }
    
    // Acquire the spinlock
    spinlock_acquire(&sem->spinlock);
    
    // Check if the semaphore count is > 0
    if (sem->count > 0) {
        // Decrement the count and return success
        sem->count--;
        spinlock_release(&sem->spinlock);
        return 1;
    }
    
    // Count is 0, would block, so return failure
    spinlock_release(&sem->spinlock);
    return 0;
}

/**
 * Signal a semaphore
 * 
 * @param sem Semaphore to signal
 */
void semaphore_signal(semaphore_t* sem) {
    if (!sem) {
        return;
    }
    
    // Acquire the spinlock
    spinlock_acquire(&sem->spinlock);
    
    // Check if the count is less than max_count
    if (sem->count < sem->max_count) {
        // Increment the count
        sem->count++;
    }
    
    // Release the spinlock
    spinlock_release(&sem->spinlock);
}

/**
 * Initialize a condition variable
 * 
 * @param cond Condition variable to initialize
 */
void condition_init(condition_t* cond) {
    if (cond) {
        spinlock_init(&cond->spinlock);
        cond->waiters_count = 0;
    }
}

/**
 * Wait on a condition variable
 * 
 * @param cond Condition variable to wait on
 * @param mutex Mutex that protects the condition
 */
void condition_wait(condition_t* cond, mutex_t* mutex) {
    if (!cond || !mutex) {
        return;
    }
    
    // Acquire the condition's spinlock
    spinlock_acquire(&cond->spinlock);
    
    // Increment the number of waiters
    cond->waiters_count++;
    
    // Release the spinlock
    spinlock_release(&cond->spinlock);
    
    // Release the mutex to allow other tasks to signal the condition
    mutex_unlock(mutex);
    
    // Wait for the condition to be signaled
    // This is a simple polling implementation
    while (1) {
        // Check if the condition has been signaled (waiters_count decreased)
        spinlock_acquire(&cond->spinlock);
        int is_signaled = (cond->waiters_count < 1);
        spinlock_release(&cond->spinlock);
        
        if (is_signaled) {
            break;
        }
        
        // Yield to other tasks while waiting
        switch_task();
    }
    
    // Reacquire the mutex before returning
    mutex_lock(mutex);
}

/**
 * Signal a condition variable
 * 
 * @param cond Condition variable to signal
 */
void condition_signal(condition_t* cond) {
    if (!cond) {
        return;
    }
    
    // Acquire the spinlock
    spinlock_acquire(&cond->spinlock);
    
    // If there are waiters, decrement the count
    if (cond->waiters_count > 0) {
        cond->waiters_count--;
    }
    
    // Release the spinlock
    spinlock_release(&cond->spinlock);
}

/**
 * Broadcast to all waiters on a condition variable
 * 
 * @param cond Condition variable to broadcast on
 */
void condition_broadcast(condition_t* cond) {
    if (!cond) {
        return;
    }
    
    // Acquire the spinlock
    spinlock_acquire(&cond->spinlock);
    
    // Reset waiters count to 0 to wake all waiters
    cond->waiters_count = 0;
    
    // Release the spinlock
    spinlock_release(&cond->spinlock);
}