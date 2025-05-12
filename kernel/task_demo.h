#ifndef TASK_DEMO_H
#define TASK_DEMO_H

#include <stdint.h>

/**
 * Start the multitasking demo with two competing tasks
 * This demonstrates the difference between cooperative and preemptive multitasking.
 * When preemption is enabled, both tasks make progress regardless of their CPU usage.
 * When preemption is disabled, the busier task can monopolize CPU time.
 */
void start_multitasking_demo();

#endif /* TASK_DEMO_H */
