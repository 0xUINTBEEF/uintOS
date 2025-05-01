#ifndef POWER_H
#define POWER_H

#include <stdint.h>

// System power states
typedef enum {
    POWER_STATE_ON,           // System is fully powered on
    POWER_STATE_SLEEP,        // System is in sleep mode (S1)
    POWER_STATE_SUSPEND,      // System is suspended to RAM (S3)
    POWER_STATE_HIBERNATE,    // System is hibernated to disk (S4)
    POWER_STATE_OFF           // System is powered off (S5)
} power_state_t;

// Device power states
typedef enum {
    DEVICE_POWER_ON,          // Device is fully powered on and operational
    DEVICE_POWER_STANDBY,     // Device is in standby mode (fast wake)
    DEVICE_POWER_SUSPEND,     // Device is suspended (slower wake)
    DEVICE_POWER_OFF          // Device is powered off
} device_power_state_t;

// Power event types
typedef enum {
    POWER_EVENT_AC_CONNECTED,     // AC power connected
    POWER_EVENT_AC_DISCONNECTED,  // AC power disconnected
    POWER_EVENT_BATTERY_LOW,      // Battery level is low
    POWER_EVENT_BATTERY_CRITICAL, // Battery level is critical
    POWER_EVENT_USER_ACTIVITY,    // User activity detected
    POWER_EVENT_IDLE_TIMEOUT,     // System idle timeout
    POWER_EVENT_LID_CLOSED,       // Laptop lid closed
    POWER_EVENT_LID_OPENED,       // Laptop lid opened
    POWER_EVENT_POWER_BUTTON,     // Power button pressed
    POWER_EVENT_SLEEP_BUTTON      // Sleep button pressed
} power_event_t;

// Power capabilities flags
#define POWER_CAP_S1_SLEEP         (1 << 0)    // S1 sleep state supported
#define POWER_CAP_S3_SUSPEND       (1 << 1)    // S3 suspend to RAM supported
#define POWER_CAP_S4_HIBERNATE     (1 << 2)    // S4 hibernate to disk supported
#define POWER_CAP_BATTERY_MONITOR  (1 << 3)    // Battery monitoring supported
#define POWER_CAP_THERMAL_MONITOR  (1 << 4)    // Thermal monitoring supported
#define POWER_CAP_DEVICE_POWER_CTL (1 << 5)    // Device power state control
#define POWER_CAP_CPU_THROTTLING   (1 << 6)    // CPU frequency/throttling supported

// Battery status flags
#define BATTERY_STATUS_PRESENT     (1 << 0)    // Battery is present
#define BATTERY_STATUS_CHARGING    (1 << 1)    // Battery is charging
#define BATTERY_STATUS_DISCHARGING (1 << 2)    // Battery is discharging
#define BATTERY_STATUS_LOW         (1 << 3)    // Battery level is low
#define BATTERY_STATUS_CRITICAL    (1 << 4)    // Battery level is critical
#define BATTERY_STATUS_FULL        (1 << 5)    // Battery is fully charged

// Power management callback type
typedef void (*power_callback_t)(power_event_t event, void* context);

// Battery information structure
typedef struct {
    uint8_t status;               // Battery status flags
    uint8_t present;              // Battery present (1) or not (0)
    uint8_t percentage;           // Battery charge percentage (0-100)
    uint16_t voltage;             // Battery voltage (mV)
    uint32_t capacity;            // Battery capacity (mWh)
    uint32_t rate;                // Charge/discharge rate (mW)
    uint32_t remaining_time;      // Remaining time in seconds
} battery_info_t;

// System thermal information structure
typedef struct {
    uint32_t cpu_temp;            // CPU temperature (in 0.1 degrees C)
    uint32_t system_temp;         // System temperature (in 0.1 degrees C)
    uint8_t fan_speed;            // Fan speed (percentage)
    uint8_t throttle_level;       // CPU throttling level (percentage)
} thermal_info_t;

// Power settings structure
typedef struct {
    uint32_t idle_timeout;        // System idle timeout (seconds)
    uint8_t power_button_action;  // Action when power button is pressed
    uint8_t sleep_button_action;  // Action when sleep button is pressed
    uint8_t lid_close_action;     // Action when lid is closed
    uint8_t low_battery_action;   // Action when battery is low
    uint8_t critical_battery_action; // Action when battery is critical
    uint8_t display_timeout;      // Display timeout (seconds)
    uint8_t hard_disk_timeout;    // Hard disk timeout (seconds)
    uint8_t enable_wake_on_lan;   // Enable wake on LAN (1) or not (0)
    uint8_t enable_wake_on_ring;  // Enable wake on modem ring (1) or not (0)
} power_settings_t;

// Initialize power management subsystem
int power_init(void);

// Get system power capabilities
uint32_t power_get_capabilities(void);

// Set system power state
int power_set_system_state(power_state_t state);

// Get current system power state
power_state_t power_get_system_state(void);

// Set device power state
int power_set_device_state(uint32_t device_id, device_power_state_t state);

// Get device power state
device_power_state_t power_get_device_state(uint32_t device_id);

// Register power event callback
int power_register_callback(power_callback_t callback, void* context);

// Unregister power event callback
int power_unregister_callback(power_callback_t callback);

// Get battery information
int power_get_battery_info(battery_info_t* info);

// Get thermal information
int power_get_thermal_info(thermal_info_t* info);

// Set CPU speed/throttling level
int power_set_cpu_throttle(uint8_t level);

// Get power settings
int power_get_settings(power_settings_t* settings);

// Set power settings
int power_set_settings(const power_settings_t* settings);

// Process power event
void power_process_event(power_event_t event);

// Handle system idle time
void power_update_idle_time(uint32_t idle_time_ms);

// Prepare for system power state transition
void power_prepare_transition(power_state_t old_state, power_state_t new_state);

// Resume from system power state transition
void power_resume_transition(power_state_t old_state, power_state_t new_state);

// Check if a device can wake the system
int power_device_can_wake(uint32_t device_id);

// Set device wake capability
int power_set_device_wake(uint32_t device_id, int can_wake);

#endif // POWER_H