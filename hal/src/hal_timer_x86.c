#include "../include/hal_timer.h"
#include "../include/hal_io.h"
#include "../include/hal_interrupt.h"
#include <stdbool.h>

// Error codes
#define HAL_TIMER_SUCCESS        0
#define HAL_TIMER_INVALID_PARAM -1
#define HAL_TIMER_NOT_AVAILABLE -2
#define HAL_TIMER_CALIBRATION_FAILED -3

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

// Maximum number of timers supported
#define MAX_TIMERS 4

// TSC frequency calibration state
static uint64_t g_tsc_frequency = 0;
static bool g_tsc_calibrated = false;

// Timer configuration storage
typedef struct {
    bool initialized;           // Timer has been initialized
    bool active;               // Timer is currently running
    hal_timer_mode_t mode;     // Timer mode (one-shot or periodic)
    uint32_t initial_count;    // Initial counter value
    uint32_t vector;           // Interrupt vector
    uint32_t divider;          // Timer divider value
    uint32_t frequency;        // Timer frequency in Hz (if known)
    hal_timer_callback_t callback;      // User callback function
    void* callback_context;    // Context for callback function
} timer_state_t;

static timer_state_t timer_state[MAX_TIMERS] = {0}; 

/**
 * Validates a timer ID is within range and initialized
 *
 * @param timer_id The timer ID to validate
 * @return true if valid, false otherwise
 */
static bool validate_timer_id(uint32_t timer_id) {
    return (timer_id < MAX_TIMERS && timer_state[timer_id].initialized);
}

/**
 * Initialize the timer subsystem
 * 
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_initialize(void) {
    // Initialize timer state
    for (int i = 0; i < MAX_TIMERS; i++) {
        timer_state[i].initialized = false;
        timer_state[i].active = false;
        timer_state[i].callback = NULL;
    }
    
    // Initialize LAPIC timer
    if (hal_io_memory_read32(LAPIC_BASE + LAPIC_VERSION) != 0) {
        timer_state[0].initialized = true;
    }
    
    // Initialize PIT timer
    timer_state[1].initialized = true;
    timer_state[1].frequency = 1193182; // Standard PIT frequency
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Finalize the timer subsystem
 * 
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_finalize(void) {
    // Stop all active timers
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timer_state[i].active) {
            hal_timer_stop(i);
        }
        timer_state[i].initialized = false;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Get information about a timer
 * 
 * @param timer_id Timer ID to query
 * @param info Pointer to hal_timer_info_t structure to fill
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_get_info(uint32_t timer_id, hal_timer_info_t* info) {
    // Validate parameters
    if (timer_id >= MAX_TIMERS || !info) {
        return HAL_TIMER_INVALID_PARAM;
    }
    
    if (!timer_state[timer_id].initialized) {
        return HAL_TIMER_NOT_AVAILABLE;
    }
    
    // Fill in timer info based on ID
    switch (timer_id) {
        case 0: // LAPIC timer
            info->type = HAL_TIMER_APIC;
            info->frequency = timer_state[timer_id].frequency; 
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
            if (timer_state[timer_id].initialized) {
                info->type = HAL_TIMER_HPET;
                info->frequency = timer_state[timer_id].frequency;
                info->resolution = 100; // HPET typically has better resolution
                info->max_value = 0xFFFFFFFFFFFFFFFF; // 64-bit counter
                info->is_available = true;
                info->version = 0; // Could read from HPET capabilities register
            } else {
                info->is_available = false;
                return HAL_TIMER_NOT_AVAILABLE;
            }
            break;
            
        case 3: // RTC
            if (timer_state[timer_id].initialized) {
                info->type = HAL_TIMER_RTC;
                info->frequency = 32768; // Standard RTC crystal frequency
                info->resolution = 30518; // ~30.5µs resolution
                info->max_value = 0xFFFFFFFF;
                info->is_available = true;
                info->version = 0;
            } else {
                info->is_available = false;
                return HAL_TIMER_NOT_AVAILABLE;
            }
            break;
            
        default:
            return HAL_TIMER_INVALID_PARAM;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Configure a timer
 * 
 * @param timer_id Timer ID to configure
 * @param config Pointer to configuration structure
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_configure(uint32_t timer_id, const hal_timer_config_t* config) {
    // Validate parameters
    if (!validate_timer_id(timer_id) || !config) {
        return HAL_TIMER_INVALID_PARAM;
    }
    
    // If timer is active, stop it first
    if (timer_state[timer_id].active) {
        hal_timer_stop(timer_id);
    }
    
    // Store configuration
    timer_state[timer_id].mode = config->mode;
    timer_state[timer_id].initial_count = config->initial_count;
    timer_state[timer_id].vector = config->vector;
    timer_state[timer_id].callback = config->callback;
    timer_state[timer_id].callback_context = config->callback_context;
    
    // Handle timer-specific configuration
    switch (timer_id) {
        case 0: // LAPIC timer
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
            
            // If frequency is specified, adjust initial_count accordingly
            if (config->frequency > 0 && timer_state[timer_id].frequency > 0) {
                // Calculate counts for the desired frequency
                timer_state[timer_id].initial_count = timer_state[timer_id].frequency / config->frequency;
            }
            break;
            
        case 1: // PIT
            if (config->frequency > 0) {
                // PIT uses a 16-bit counter driven by a 1.193182 MHz clock
                timer_state[timer_id].initial_count = (timer_state[timer_id].frequency / config->frequency) & 0xFFFF;
                
                // If resulting count is too small, adjust
                if (timer_state[timer_id].initial_count < 10) {
                    timer_state[timer_id].initial_count = 10; // Minimum safe value
                }
            }
            break;
            
        case 2: // HPET
        case 3: // RTC
            // Implement when these timers are added
            break;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Start a timer
 * 
 * @param timer_id Timer ID to start
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_start(uint32_t timer_id) {
    if (!validate_timer_id(timer_id)) {
        return HAL_TIMER_INVALID_PARAM;
    }
    
    // Don't restart an already active timer
    if (timer_state[timer_id].active) {
        return HAL_TIMER_SUCCESS;
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
            {
                // PIT uses I/O ports: 0x40-0x43
                // Channel 0, access mode: low byte then high byte, mode: rate generator/square wave
                uint8_t pit_mode = (timer_state[timer_id].mode == HAL_TIMER_PERIODIC) ? 0x36 : 0x30;
                hal_io_port_out8(0x43, pit_mode);
                
                // Write initial count (low byte, then high byte)
                hal_io_port_out8(0x40, timer_state[timer_id].initial_count & 0xFF);
                hal_io_port_out8(0x40, (timer_state[timer_id].initial_count >> 8) & 0xFF);
                
                timer_state[timer_id].active = true;
            }
            break;
            
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return HAL_TIMER_NOT_AVAILABLE;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Stop a timer
 * 
 * @param timer_id Timer ID to stop
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_stop(uint32_t timer_id) {
    if (!validate_timer_id(timer_id)) {
        return HAL_TIMER_INVALID_PARAM;
    }
    
    // If timer is not active, nothing to do
    if (!timer_state[timer_id].active) {
        return HAL_TIMER_SUCCESS;
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            // Disable the timer by setting initial count to 0
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER_INIT_COUNT, 0);
            
            // Mark timer as inactive
            timer_state[timer_id].active = false;
            break;
            
        case 1: // PIT
            // For PIT, use a count of 0 which is interpreted as 65536
            // This effectively makes the timer tick very infrequently
            hal_io_port_out8(0x43, 0x36); // Channel 0, low/high byte, mode 3
            hal_io_port_out8(0x40, 0xFF);
            hal_io_port_out8(0x40, 0xFF);
            
            timer_state[timer_id].active = false;
            break;
            
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return HAL_TIMER_NOT_AVAILABLE;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Get current counter value of a timer
 * 
 * @param timer_id Timer ID to read
 * @return Current counter value or 0 if invalid/error
 */
uint32_t hal_timer_get_counter(uint32_t timer_id) {
    if (!validate_timer_id(timer_id)) {
        return 0; // Invalid parameters
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            return hal_io_memory_read32(LAPIC_BASE + LAPIC_TIMER_CURRENT);
            
        case 1: // PIT
            {
                // Latch counter value for channel 0
                hal_io_port_out8(0x43, 0x00);
                
                // Read counter value (low byte, then high byte)
                uint8_t low = hal_io_port_in8(0x40);
                uint8_t high = hal_io_port_in8(0x40);
                
                return (high << 8) | low;
            }
            
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return 0;
    }
    
    return 0;
}

/**
 * Get current ticks from the system's high-resolution timer (TSC)
 * 
 * @return Current TSC value
 */
uint64_t hal_timer_get_current_ticks(void) {
    // Use TSC for high-resolution timing on x86
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

/**
 * Get the frequency of the high-resolution timer
 * 
 * @return Timer frequency in Hz
 */
uint64_t hal_timer_get_frequency(void) {
    // Return calibrated frequency if available
    if (g_tsc_calibrated) {
        return g_tsc_frequency;
    }
    
    // Return estimate if not calibrated
    return 3000000000ULL; // 3 GHz estimate
}

/**
 * Convert ticks to nanoseconds
 * 
 * @param ticks Number of ticks to convert
 * @return Equivalent time in nanoseconds
 */
uint64_t hal_timer_ticks_to_ns(uint64_t ticks) {
    // Improved conversion with overflow protection
    static const uint64_t NS_PER_SEC = 1000000000ULL;
    uint64_t freq = hal_timer_get_frequency();
    
    if (freq == 0) {
        return 0; // Prevent divide by zero
    }
    
    // Use 128-bit arithmetic to prevent overflow
    // ticks * NS_PER_SEC / freq
    
    // Split calculation to avoid 64-bit overflow
    uint64_t high_part = (ticks / freq) * NS_PER_SEC;
    uint64_t low_part = ((ticks % freq) * NS_PER_SEC) / freq;
    
    return high_part + low_part;
}

/**
 * Convert nanoseconds to ticks
 * 
 * @param ns Time in nanoseconds
 * @return Equivalent number of ticks
 */
uint64_t hal_timer_ns_to_ticks(uint64_t ns) {
    // Improved conversion with overflow protection
    static const uint64_t NS_PER_SEC = 1000000000ULL;
    uint64_t freq = hal_timer_get_frequency();
    
    // Split calculation to avoid 64-bit overflow
    uint64_t high_part = (ns / NS_PER_SEC) * freq;
    uint64_t low_part = ((ns % NS_PER_SEC) * freq) / NS_PER_SEC;
    
    return high_part + low_part;
}

/**
 * Set interval for a timer
 * 
 * @param timer_id Timer ID to configure
 * @param interval_ns Interval in nanoseconds
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_set_interval(uint32_t timer_id, uint64_t interval_ns) {
    if (!validate_timer_id(timer_id)) {
        return HAL_TIMER_INVALID_PARAM;
    }
    
    // Convert nanoseconds to timer ticks based on timer type
    uint32_t ticks = 0;
    bool was_active = timer_state[timer_id].active;
    
    switch (timer_id) {
        case 0: // LAPIC timer
            {
                // More precise conversion based on calibrated LAPIC frequency
                // If frequency is known, use it, otherwise use approximation
                if (timer_state[timer_id].frequency > 0) {
                    // interval_ns * (frequency / 1,000,000,000)
                    ticks = (uint32_t)((interval_ns * timer_state[timer_id].frequency) / 1000000000ULL);
                } else {
                    // Fallback approximation: ~1µs per tick
                    ticks = (uint32_t)(interval_ns / 1000);
                }
                
                // Ensure minimum valid value
                if (ticks < 10) {
                    ticks = 10;
                }
                
                // Store new initial count
                timer_state[timer_id].initial_count = ticks;
                
                // If the timer is active, update it
                if (was_active) {
                    hal_timer_stop(timer_id);
                    hal_timer_start(timer_id);
                }
            }
            break;
            
        case 1: // PIT
            {
                // PIT has a fixed frequency of 1.193182 MHz
                // Convert ns to PIT ticks: interval_ns * (1,193,182 / 1,000,000,000)
                ticks = (uint32_t)((interval_ns * 1193182ULL) / 1000000000ULL);
                
                // Ensure valid range (16-bit counter)
                if (ticks > 0xFFFF) {
                    ticks = 0xFFFF;
                } else if (ticks < 10) {
                    ticks = 10; // Minimum safe value
                }
                
                timer_state[timer_id].initial_count = ticks;
                
                if (was_active) {
                    hal_timer_stop(timer_id);
                    hal_timer_start(timer_id);
                }
            }
            break;
            
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return HAL_TIMER_NOT_AVAILABLE;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Get remaining time on a timer
 * 
 * @param timer_id Timer ID to query
 * @param remaining_ns Pointer to store remaining time in nanoseconds
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_get_remaining(uint32_t timer_id, uint64_t* remaining_ns) {
    if (!validate_timer_id(timer_id) || !remaining_ns) {
        return HAL_TIMER_INVALID_PARAM;
    }
    
    if (!timer_state[timer_id].active) {
        *remaining_ns = 0;
        return HAL_TIMER_SUCCESS;
    }
    
    switch (timer_id) {
        case 0: // LAPIC timer
            {
                uint32_t current = hal_timer_get_counter(timer_id);
                
                // Convert current count to nanoseconds
                if (timer_state[timer_id].frequency > 0) {
                    *remaining_ns = ((uint64_t)current * 1000000000ULL) / timer_state[timer_id].frequency;
                } else {
                    // Fallback approximation: ~1µs per tick
                    *remaining_ns = (uint64_t)current * 1000;
                }
            }
            break;
            
        case 1: // PIT
            {
                uint32_t current = hal_timer_get_counter(timer_id);
                
                // Convert PIT ticks to nanoseconds
                // current * (1,000,000,000 / 1,193,182)
                *remaining_ns = ((uint64_t)current * 1000000000ULL) / 1193182ULL;
            }
            break;
            
        case 2: // HPET
        case 3: // RTC
            // Not implemented yet
            return HAL_TIMER_NOT_AVAILABLE;
    }
    
    return HAL_TIMER_SUCCESS;
}

/**
 * Get current time in nanoseconds
 * 
 * @return Current system time in nanoseconds
 */
uint64_t hal_time_now_ns(void) {
    return hal_timer_ticks_to_ns(hal_timer_get_current_ticks());
}

/**
 * Delay for specified number of nanoseconds
 * 
 * @param ns Time to delay in nanoseconds
 */
void hal_time_delay_ns(uint64_t ns) {
    // For very short delays, use busy-wait
    if (ns < 50000) { // Less than 50µs
        uint64_t start = hal_timer_get_current_ticks();
        uint64_t end = start + hal_timer_ns_to_ticks(ns);
        
        while (hal_timer_get_current_ticks() < end) {
            // CPU hint to reduce power consumption in tight loop
            asm volatile("pause");
        }
    } else {
        // For longer delays, consider yielding in future implementation
        // For now, still use busy-wait but with pause instruction
        uint64_t start = hal_timer_get_current_ticks();
        uint64_t end = start + hal_timer_ns_to_ticks(ns);
        
        while (hal_timer_get_current_ticks() < end) {
            asm volatile("pause");
            
            // TODO: In a preemptive OS, could call a yield function here
            // if available through the scheduler
        }
    }
}

/**
 * Delay for specified number of microseconds
 * 
 * @param us Time to delay in microseconds
 */
void hal_time_delay_us(uint64_t us) {
    hal_time_delay_ns(us * 1000ULL);
}

/**
 * Delay for specified number of milliseconds
 * 
 * @param ms Time to delay in milliseconds
 */
void hal_time_delay_ms(uint64_t ms) {
    hal_time_delay_ns(ms * 1000000ULL);
}

/**
 * Calibrate the timer system
 * 
 * This function calibrates the TSC frequency using a known time source
 * (typically the PIT or APIC timer) for accurate time measurements.
 * 
 * @return HAL_TIMER_SUCCESS on success, error code otherwise
 */
int hal_timer_calibrate(void) {
    // Use PIT to calibrate TSC frequency
    // Set up PIT for a 10ms interval
    hal_io_port_out8(0x43, 0x36); // Channel 0, square wave, LSB+MSB
    hal_io_port_out8(0x40, 0xFF);  // Low byte of count (max)
    hal_io_port_out8(0x40, 0xFF);  // High byte of count (max)
    
    // Check if PIT output bit is functioning - try to detect changes
    uint8_t pit_bit_initial = hal_io_port_in8(0x61) & 0x20;
    unsigned int check_timeout = 0;
    uint8_t pit_bit_changed = 0;
    
    for (unsigned int i = 0; i < 1000000; i++) {
        if ((hal_io_port_in8(0x61) & 0x20) != pit_bit_initial) {
            pit_bit_changed = 1;
            break;
        }
    }
    
    if (!pit_bit_changed) {
        log_error("HAL Timer", "PIT output bit not toggling, cannot calibrate timer");
        log_debug("HAL Timer", "Using fallback frequency estimate");
        g_tsc_frequency = 3000000000ULL; // 3 GHz estimate
        g_tsc_calibrated = false;
        return HAL_TIMER_CALIBRATION_FAILED;
    }
    
    // Wait for first PIT cycle to complete
    uint8_t initial_count = hal_io_port_in8(0x61) & 0x20;
    
    unsigned int timeout_counter = 0;
    while ((hal_io_port_in8(0x61) & 0x20) == initial_count) {
        timeout_counter++;
        if (timeout_counter > 1000000) {
            // If we timeout, log error and exit calibration
            log_error("HAL Timer", "Timeout waiting for initial PIT cycle");
            g_tsc_frequency = 3000000000ULL; // 3 GHz estimate
            g_tsc_calibrated = false;
            return HAL_TIMER_CALIBRATION_FAILED;
        }
        
        // Short busy-wait delay
        for (volatile int j = 0; j < 10; j++) { /* very short delay */ }
    }
    
    // Start TSC measurement
    uint64_t start_tsc = hal_timer_get_current_ticks();
    
    // Wait for 100 PIT cycles (about 100ms for 1.19MHz clock)
    uint8_t last_tick = hal_io_port_in8(0x61) & 0x20;
    for (int i = 0; i < 100; i++) {
        // Wait for output bit to toggle with timeout
        unsigned int timeout_counter = 0;
        while ((hal_io_port_in8(0x61) & 0x20) == last_tick) {
            // Add timeout to prevent infinite loop
            timeout_counter++;
            if (timeout_counter > 1000000) {
                // If we timeout, log error and exit calibration
                log_error("HAL Timer", "Timeout waiting for PIT to toggle");
                return HAL_TIMER_CALIBRATION_FAILED;
            }
            // Short busy-wait delay
            for (volatile int j = 0; j < 10; j++) { /* very short delay */ }
        }
        last_tick = hal_io_port_in8(0x61) & 0x20;
    }
    
    // End TSC measurement
    uint64_t end_tsc = hal_timer_get_current_ticks();
    
    // Calculate TSC frequency (ticks in 0.1 seconds * 10 = ticks per second)
    uint64_t tsc_diff = end_tsc - start_tsc;
    g_tsc_frequency = tsc_diff * 10;
    
    // Validate result (between 100MHz and 10GHz)
    if (g_tsc_frequency > 100000000ULL && g_tsc_frequency < 10000000000ULL) {
        g_tsc_calibrated = true;
        
        // Update LAPIC timer frequency if LAPIC is available
        if (timer_state[0].initialized) {
            // Calibrate LAPIC timer against TSC
            
            // Set LAPIC timer to one-shot mode with a known count
            uint32_t lapic_test_count = 1000000;
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER_DIV_CONFIG, TIMER_DIV_1);
            hal_io_memory_write32(LAPIC_BASE + LAPIC_TIMER_INIT_COUNT, lapic_test_count);
            
            // Start TSC measurement
            uint64_t lapic_start_tsc = hal_timer_get_current_ticks();
            
            // Wait until LAPIC timer decreases by at least 10%
            while (hal_io_memory_read32(LAPIC_BASE + LAPIC_TIMER_CURRENT) > (lapic_test_count * 0.9)) {
                // Wait
            }
            
            // Read current values
            uint32_t lapic_current = hal_io_memory_read32(LAPIC_BASE + LAPIC_TIMER_CURRENT);
            uint64_t lapic_end_tsc = hal_timer_get_current_ticks();
            
            // Calculate LAPIC frequency
            uint32_t lapic_count_diff = lapic_test_count - lapic_current;
            uint64_t tsc_time_diff = lapic_end_tsc - lapic_start_tsc;
            uint64_t tsc_time_ns = hal_timer_ticks_to_ns(tsc_time_diff);
            
            // lapic_frequency = lapic_counts_per_second = lapic_count_diff * (1e9 / tsc_time_ns)
            if (tsc_time_ns > 0) {
                timer_state[0].frequency = (uint32_t)((uint64_t)lapic_count_diff * 1000000000ULL / tsc_time_ns);
            }
        }
        
        return HAL_TIMER_SUCCESS;
    } else {
        g_tsc_calibrated = false;
        return HAL_TIMER_CALIBRATION_FAILED;
    }
}