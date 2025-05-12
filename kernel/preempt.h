#ifndef PREEMPT_H
#define PREEMPT_H

#include <stdint.h>

/**
 * Initialize preemptive multitasking by setting up timer interrupts
 * 
 * @param frequency The frequency of the timer interrupts in Hz
 * @return 0 on success, non-zero on failure
 */
int init_preemptive_multitasking(uint32_t frequency);

/**
 * Enable preemptive multitasking
 */
void enable_preemption();

/**
 * Disable preemptive multitasking
 */
void disable_preemption();

/**
 * Check if preemption is currently enabled
 * 
 * @return 1 if enabled, 0 if disabled
 */
int is_preemption_enabled();

/**
 * Get the number of timer ticks since preemption was initialized
 * 
 * @return The number of timer ticks
 */
uint64_t get_preemption_ticks();

/**
 * Get preemption statistics
 * 
 * @param involuntary Pointer to store involuntary switch count
 * @param voluntary Pointer to store voluntary switch count
 * @param timer_ints Pointer to store timer interrupt count
 * @param disabled_time Pointer to store disabled time
 */
void get_preemption_stats(uint64_t* involuntary, uint64_t* voluntary,
                          uint64_t* timer_ints, uint64_t* disabled_time);

/**
 * Reset preemption statistics
 */
void reset_preemption_stats();

/**
 * Record an involuntary task switch
 */
void record_involuntary_switch();

/**
 * Record a voluntary task switch
 */
void record_voluntary_switch();

#endif /* PREEMPT_H */
