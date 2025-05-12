#include "preempt.h"
#include "scheduler.h"
#include "irq.h"
#include "lapic.h"
#include "logging/log.h"
#include "../hal/include/hal_timer.h"
#include "../hal/include/hal_interrupt.h"

// The timer interrupt vector number
#define TIMER_INTERRUPT_VECTOR 32

// Preemption state
static int preemption_enabled = 0;
static uint64_t preemption_ticks = 0;
static uint32_t timer_frequency = 100; // Default to 100Hz (10ms intervals)

// Preemption statistics
static struct {
    uint64_t involuntary_switches;   // Number of task switches due to timer preemption
    uint64_t voluntary_switches;     // Number of task switches due to explicit yield
    uint64_t timer_interrupts;       // Total number of timer interrupts received
    uint64_t preemption_disabled_time;  // Time spent with preemption disabled
    uint32_t longest_preemption_off;   // Longest stretch with preemption disabled
    uint32_t current_preemption_off;   // Current stretch with preemption disabled
} preempt_stats = {0};

// Forward declaration of the timer interrupt handler
static void timer_interrupt_handler(void* context);

/**
 * Initialize preemptive multitasking by setting up timer interrupts
 * 
 * @param frequency The frequency of the timer interrupts in Hz
 * @return 0 on success, non-zero on failure
 */
int init_preemptive_multitasking(uint32_t frequency) {
    log_info("Initializing preemptive multitasking at %d Hz", frequency);
    
    // Store the frequency for future reference
    if (frequency > 0) {
        timer_frequency = frequency;
    }
    
    // Initialize the HAL timer subsystem
    if (hal_timer_initialize() != 0) {
        log_error("Failed to initialize HAL timer");
        return -1;
    }
    
    // Get timer information
    hal_timer_info_t timer_info;
    if (hal_timer_get_info(0, &timer_info) != 0) {
        log_error("Failed to get timer information");
        return -2;
    }
    
    // Configure the APIC timer for periodic interrupts
    hal_timer_config_t timer_config = {
        .mode = HAL_TIMER_PERIODIC,
        .frequency = timer_frequency,
        .initial_count = 0, // Will be calculated based on frequency
        .vector = TIMER_INTERRUPT_VECTOR,
        .callback = timer_interrupt_handler,
        .callback_context = NULL
    };
    
    // Configure and start the timer
    if (hal_timer_configure(0, &timer_config) != 0) {
        log_error("Failed to configure timer");
        return -3;
    }
    
    // Register the timer interrupt handler
    hal_interrupt_register_handler(TIMER_INTERRUPT_VECTOR, timer_interrupt_handler, NULL);
    
    // Enable the interrupt in the system
    hal_interrupt_enable(TIMER_INTERRUPT_VECTOR);
    
    log_info("Starting timer at %d Hz", timer_frequency);
    if (hal_timer_start(0) != 0) {
        log_error("Failed to start timer");
        return -4;
    }
    
    // Initially, preemption is disabled until explicitly enabled
    preemption_enabled = 0;
    preemption_ticks = 0;
    
    log_info("Preemptive multitasking initialized successfully");
    return 0;
}

/**
 * Enable preemptive multitasking
 */
void enable_preemption() {
    log_info("Enabling preemptive task switching");
    preemption_enabled = 1;
    scheduler_enable_preemption();
}

/**
 * Disable preemptive multitasking
 */
void disable_preemption() {
    log_info("Disabling preemptive task switching");
    preemption_enabled = 0;
    scheduler_disable_preemption();
}

/**
 * Check if preemption is currently enabled
 * 
 * @return 1 if enabled, 0 if disabled
 */
int is_preemption_enabled() {
    return preemption_enabled;
}

/**
 * Get the number of timer ticks since preemption was initialized
 * 
 * @return The number of timer ticks
 */
uint64_t get_preemption_ticks() {
    return preemption_ticks;
}

/**
 * Get preemption statistics
 * 
 * @param involuntary Pointer to store involuntary switch count
 * @param voluntary Pointer to store voluntary switch count
 * @param timer_ints Pointer to store timer interrupt count
 * @param disabled_time Pointer to store disabled time
 */
void get_preemption_stats(uint64_t* involuntary, uint64_t* voluntary,
                          uint64_t* timer_ints, uint64_t* disabled_time) {
    if (involuntary) *involuntary = preempt_stats.involuntary_switches;
    if (voluntary) *voluntary = preempt_stats.voluntary_switches;
    if (timer_ints) *timer_ints = preempt_stats.timer_interrupts;
    if (disabled_time) *disabled_time = preempt_stats.preemption_disabled_time;
}

/**
 * Reset preemption statistics
 */
void reset_preemption_stats() {
    preempt_stats.involuntary_switches = 0;
    preempt_stats.voluntary_switches = 0;
    preempt_stats.timer_interrupts = 0;
    preempt_stats.preemption_disabled_time = 0;
    preempt_stats.longest_preemption_off = 0;
    preempt_stats.current_preemption_off = 0;
}

/**
 * Record an involuntary task switch
 */
void record_involuntary_switch() {
    preempt_stats.involuntary_switches++;
}

/**
 * Record a voluntary task switch
 */
void record_voluntary_switch() {
    preempt_stats.voluntary_switches++;
}

/**
 * Timer interrupt handler - This gets called every time the timer fires
 * 
 * @param context Optional context pointer (unused)
 */
static void timer_interrupt_handler(void* context) {
    // Increment the tick counter
    preemption_ticks++;
    
    // Track timer interrupts in statistics
    preempt_stats.timer_interrupts++;
    
    // Track time spent with preemption disabled
    if (!preemption_enabled) {
        preempt_stats.current_preemption_off++;
        preempt_stats.preemption_disabled_time++;
        
        // Track longest stretch with preemption disabled
        if (preempt_stats.current_preemption_off > preempt_stats.longest_preemption_off) {
            preempt_stats.longest_preemption_off = preempt_stats.current_preemption_off;
        }
    } else {
        preempt_stats.current_preemption_off = 0;
    }
    
    // If preemption is enabled, process the scheduler tick
    if (preemption_enabled) {
        // Before calling scheduler_tick, remember current task
        int current_task_id = get_current_task_id();
        
        scheduler_tick();
        
        // After tick, check if task was preempted
        if (current_task_id != get_current_task_id() && current_task_id >= 0) {
            // Task was switched involuntarily
            preempt_stats.involuntary_switches++;
        }
    }
    
    // Acknowledge the interrupt in the Local APIC
    lapic_send_eoi();
}
