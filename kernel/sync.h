#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>

// Spinlock type
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

// Mutex type
typedef struct {
    spinlock_t spinlock;
    int owner_task;
    int lock_count;
} mutex_t;

// Semaphore type
typedef struct {
    spinlock_t spinlock;
    int count;
    int max_count;
} semaphore_t;

// Condition variable type
typedef struct {
    spinlock_t spinlock;
    int waiters_count;
} condition_t;

// Initialize a spinlock
void spinlock_init(spinlock_t* lock);

// Acquire a spinlock
void spinlock_acquire(spinlock_t* lock);

// Try to acquire a spinlock (non-blocking)
int spinlock_try_acquire(spinlock_t* lock);

// Release a spinlock
void spinlock_release(spinlock_t* lock);

// Check if a spinlock is held
int spinlock_is_held(spinlock_t* lock);

// Initialize a mutex
void mutex_init(mutex_t* mutex);

// Lock a mutex
void mutex_lock(mutex_t* mutex);

// Try to lock a mutex (non-blocking)
int mutex_try_lock(mutex_t* mutex);

// Unlock a mutex
void mutex_unlock(mutex_t* mutex);

// Initialize a semaphore
void semaphore_init(semaphore_t* sem, int initial_count, int max_count);

// Wait on a semaphore
void semaphore_wait(semaphore_t* sem);

// Try to wait on a semaphore (non-blocking)
int semaphore_try_wait(semaphore_t* sem);

// Signal a semaphore
void semaphore_signal(semaphore_t* sem);

// Initialize a condition variable
void condition_init(condition_t* cond);

// Wait on a condition variable
void condition_wait(condition_t* cond, mutex_t* mutex);

// Signal a condition variable
void condition_signal(condition_t* cond);

// Broadcast to all waiters on a condition variable
void condition_broadcast(condition_t* cond);

#endif // SYNC_H