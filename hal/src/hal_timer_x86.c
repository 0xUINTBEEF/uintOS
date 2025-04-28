#include "../include/hal_timer.h"
#include "../include/hal_io.h"
#include "../include/hal_interrupt.h"

// LAPIC base address (same as in kernel/lapic.h)
#define LAPIC_BASE 0xfee00000

// LAPIC register offsets
#define LAPIC_ID                 0x020
#define LAPIC_VERSION            0x030
#define LAPIC_TPR                0x080
#define LAPIC_EOI                0x0B0
#define LAPIC_SVR                0x0F0
#define LAPIC_TIMER              0x320
#define LAPIC_TIMER_INIT_COUNT   0x380
#define LAPIC_TIMER_CURRENT      0x390
#define LAPIC_TIMER_DIV_CONFIG   0x3E0

// Timer modes
#define TIMER_MODE_ONESHOT       0x0
#define TIMER_MODE_PERIODIC      0x1

// Timer divider values
#define TIMER_DIV_1              0xB
#define TIMER_DIV_2              0x0
#define TIMER_DIV_4              0x1
#define TIMER_DIV_8              0x2
#define TIMER_DIV_16             0x3
#define TIMER_DIV_32             0x8
#define TIMER_DIV_64             0x9
#define TIMER_DIV_128            0xA

// Bit field manipulation macros
#define TIMER_MODE(mode) ((mode) << 17)
#define TIMER_VECTOR(v) (v)

// Timer configuration storage
static struct {
    bool initialized;
    bool active;
    hal_timer_mode_t mode;
    uint32_t initial_count;
    uint32_t vector;
    uint32_t divider;
    hal_timer_callback_t callback;
    void* callback_context;
} timer_state[4] = {0}; // Support up to 4 timers (0=LAPIC, 1=PIT, 2=HPET, 3=RTC)

/**
 * Initialize the timer subsystem
 */
int hal_timer_initialize(void) {
    // Initialize timer state
    for (int i = 0; i < 4; i++) {
        timer_state[i].initialized = false;
        timer_state[i].active = false;
        timer_state[i].callback = NULL;
    }
    
    // LAPIC timer requires LAPIC to be enabled, which should be done in the interrupt HAL
    // We'll just mark this timer available
    timer_state[0].initialized = true;
    
    return 0;
}

/**
 * Finalize the timer subsystem
 */
int hal_timer_finalize(void) {
    // Stop all active timers
    for (int i = 0; i < 4; i++) {
        if (timer_state[i].active) {
            hal_timer_stop(i);
        }
    }
    
    return 0;
}

/**
 * Get information about a timer
 */
int hal_timer_get_info(uint32_t timer_id, hal_timer_info_t* info) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized || !info) {
        return -1; // Invalid parameters
    }
    
    // Fill in timer info based on ID
    switch (timer_id) {
        case 0: // LAPIC timer
            info->type = HAL_TIMER_APIC;
            info->frequency = 0; // Unknown until calibrated
            info->resolution = 1000; // ~1µs resolution (approximate)
            info->max_value = 0xFFFFFFFF;
            info->is_available = true;
            // Get LAPIC version register
            info->version = hal_io_memory_read32(LAPIC_BASE + LAPIC_VERSION);
            break;
            
        case 1: // PIT
            info->type = HAL_TIMER_PIT;
            info->frequency = 1193182; // Standard PIT frequency
            info->resolution = 838; // ~838ns resolution
            info->max_value = 0xFFFF;
            info->is_available = true;
            info->version = 0;
            break;
            
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            info->is_available = false;
            return -1;
            
        default:
            return -1;
    }
    
    return 0;
}

/**
 * Configure a timer
 */
int hal_timer_configure(uint32_t timer_id, const hal_timer_config_t* config) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized || !config) {
        return -1; // Invalid parameters
    }
    
    // Store configuration
    timer_state[timer_id].mode = config->mode;
    timer_state[timer_id].initial_count = config->initial_count;
    timer_state[timer_id].vector = config->vector;
    timer_state[timer_id].callback = config->callback;
    timer_state[timer_id].callback_context = config->callback_context;
    
    // For LAPIC timer, set up a reasonable divider based on count
    if (timer_id == 0) {
        // Choose appropriate divider based on initial count
        if (config->initial_count < 0x100) {
            timer_state[timer_id].divider = TIMER_DIV_1;
        } else if (config->initial_count < 0x1000) {
            timer_state[timer_id].divider = TIMER_DIV_8;
        } else if (config->initial_count < 0x10000) {
            timer_state[timer_id].divider = TIMER_DIV_32;
        } else {
            timer_state[timer_id].divider = TIMER_DIV_128;
        }
    }
    
    return 0;
}

/**
 * Start a timer
 */
int hal_timer_start(uint32_t timer_id) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized) {
        return -1; // Invalid parameters
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            // Set the divider
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER_DIV_CONFIG, 
                                 timer_state[timer_id].divider);
            
            // Set initial count
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER_INIT_COUNT, 
                                 timer_state[timer_id].initial_count);
            
            // Configure timer mode and vector
            uint32_t timer_value = TIMER_MODE(timer_state[timer_id].mode) | 
                                  TIMER_VECTOR(timer_state[timer_id].vector);
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER, timer_value);
            
            // Mark timer as active
            timer_state[timer_id].active = true;
            break;
            
        case 1: // PIT
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return -1;
    }
    
    return 0;
}

/**
 * Stop a timer
 */
int hal_timer_stop(uint32_t timer_id) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized || !timer_state[timer_id].active) {
        return -1; // Invalid parameters or timer not active
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            // Disable the timer by setting initial count to 0
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER_INIT_COUNT, 0);
            
            // Mark timer as inactive
            timer_state[timer_id].active = false;
            break;
            
        case 1: // PIT
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return -1;
    }
    
    return 0;
}

/**
 * Get current counter value of a timer
 */
uint32_t hal_timer_get_counter(uint32_t timer_id) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized) {
        return 0; // Invalid parameters
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            return hal_io_memory_read32(LAPIC_BASE + LAPIC_TIMER_CURRENT);
            
        case 1: // PIT
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return 0;
    }
    
    return 0;
}

/**
 * Get current ticks from the system's high-resolution timer
 */
uint64_t hal_timer_get_current_ticks(void) {
    // Use TSC for high-resolution timing on x86
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

/**
 * Get the frequency of the high-resolution timer
 */
uint64_t hal_timer_get_frequency(void) {
    // TODO: This would require calibration for accurate TSC frequency
    // For now, return an approximate value for a modern system
    return 3000000000ULL; // 3 GHz
}

/**
 * Convert ticks to nanoseconds
 */
uint64_t hal_timer_ticks_to_ns(uint64_t ticks) {
    // Simple conversion assuming 1 GHz frequency
    // ticks * (1,000,000,000 / frequency)
    return (ticks * 1000000000ULL) / hal_timer_get_frequency();
}

/**
 * Convert nanoseconds to ticks
 */
uint64_t hal_timer_ns_to_ticks(uint64_t ns) {
    // Simple conversion assuming 1 GHz frequency
    // ns * (frequency / 1,000,000,000)
    return (ns * hal_timer_get_frequency()) / 1000000000ULL;
}

/**
 * Set interval for a timer
 */
int hal_timer_set_interval(uint32_t timer_id, uint64_t interval_ns) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized) {
        return -1; // Invalid parameters
    }
    
    // Convert nanoseconds to timer ticks
    uint32_t ticks = 0;
    
    switch (timer_id) {
        case 0: // LAPIC timer
            // Simple approximation for demonstration
            ticks = interval_ns / 1000; // ~1µs per tick
            
            // Store new initial count
            timer_state[timer_id].initial_count = ticks;
            
            // If the timer is active, update it
            if (timer_state[timer_id].active) {
                hal_timer_stop(timer_id);
                hal_timer_start(timer_id);
            }
            break;
            
        case 1: // PIT
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return -1;
    }
    
    return 0;
}

/**
 * Get remaining time on a timer
 */
int hal_timer_get_remaining(uint32_t timer_id, uint64_t* remaining_ns) {
    if (timer_id >= 4 || !timer_state[timer_id].initialized || !remaining_ns) {
        return -1; // Invalid parameters
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            {
                uint32_t current = hal_timer_get_counter(timer_id);
                // Simple approximation for demonstration
                *remaining_ns = current * 1000; // ~1µs per tick
            }
            break;
            
        case 1: // PIT
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return -1;
    }
    
    return 0;
}

/**
 * Get current time in nanoseconds
 */
uint64_t hal_time_now_ns(void) {
    return hal_timer_ticks_to_ns(hal_timer_get_current_ticks());
}

/**
 * Delay for specified number of nanoseconds
 */
void hal_time_delay_ns(uint64_t ns) {
    uint64_t start = hal_timer_get_current_ticks();
    uint64_t end = start + hal_timer_ns_to_ticks(ns);
    
    while (hal_timer_get_current_ticks() < end) {
        // Simple busy-wait
        // In a real OS, we might yield the CPU if the delay is long
        asm volatile("pause");
    }
}

/**
 * Delay for specified number of microseconds
 */
void hal_time_delay_us(uint64_t us) {
    hal_time_delay_ns(us * 1000);
}

/**
 * Delay for specified number of milliseconds
 */
void hal_time_delay_ms(uint64_t ms) {
    hal_time_delay_ns(ms * 1000000);
}

/**
 * Calibrate the timer system
 */
int hal_timer_calibrate(void) {
    // This would be a more complex implementation in a real OS
    // For now, return success
    return 0;
}