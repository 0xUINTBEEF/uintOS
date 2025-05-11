/**
 * @file ps2_mouse.h
 * @brief PS/2 Mouse driver for uintOS
 * 
 * This file provides driver support for PS/2 mouse devices including
 * standard PS/2 mice and compatible devices like touchpads.
 */

#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/device_manager.h"

// PS/2 Mouse driver version
#define PS2_MOUSE_DRV_VERSION 0x00010000  // 1.0.0.0

// PS/2 mouse controller ports (standard I/O ports)
#define PS2_DATA_PORT           0x60  // Data port
#define PS2_STATUS_PORT         0x64  // Status port
#define PS2_COMMAND_PORT        0x64  // Command port

// PS/2 controller commands
#define PS2_CMD_READ_CONFIG     0x20  // Read controller configuration byte
#define PS2_CMD_WRITE_CONFIG    0x60  // Write controller configuration byte
#define PS2_CMD_DISABLE_PORT2   0xA7  // Disable port 2 (usually mouse)
#define PS2_CMD_ENABLE_PORT2    0xA8  // Enable port 2 (usually mouse)
#define PS2_CMD_TEST_PORT2      0xA9  // Test port 2 (usually mouse)
#define PS2_CMD_SELF_TEST       0xAA  // Controller self-test
#define PS2_CMD_RESET_CPU       0xFE  // Reset CPU (caution!)

// PS/2 mouse commands
#define MOUSE_CMD_RESET         0xFF  // Reset mouse
#define MOUSE_CMD_RESEND        0xFE  // Resend last packet
#define MOUSE_CMD_SET_DEFAULTS  0xF6  // Set default settings
#define MOUSE_CMD_DISABLE       0xF5  // Disable data reporting
#define MOUSE_CMD_ENABLE        0xF4  // Enable data reporting
#define MOUSE_CMD_SET_SAMPLE    0xF3  // Set sample rate
#define MOUSE_CMD_GET_DEVID     0xF2  // Get device ID
#define MOUSE_CMD_SET_REMOTE    0xF0  // Set remote mode
#define MOUSE_CMD_SET_WRAP      0xEE  // Set wrap mode
#define MOUSE_CMD_RESET_WRAP    0xEC  // Reset wrap mode
#define MOUSE_CMD_READ_DATA     0xEB  // Read data
#define MOUSE_CMD_SET_STREAM    0xEA  // Set stream mode
#define MOUSE_CMD_STATUS_REQ    0xE9  // Get status

// PS/2 controller status register bits
#define PS2_STATUS_OUTPUT_FULL  0x01  // Output buffer full (data available)
#define PS2_STATUS_INPUT_FULL   0x02  // Input buffer full (don't write)
#define PS2_STATUS_SYSTEM_FLAG  0x04  // System flag
#define PS2_STATUS_COMMAND      0x08  // 0 = data written to input buffer is data, 1 = command
#define PS2_STATUS_TIMEOUT      0x40  // Timeout error
#define PS2_STATUS_PARITY_ERR   0x80  // Parity error

// Mouse packet bits
#define MOUSE_PACKET_Y_OVERFLOW  0x80  // Y movement overflow
#define MOUSE_PACKET_X_OVERFLOW  0x40  // X movement overflow
#define MOUSE_PACKET_Y_NEGATIVE  0x20  // Y movement direction (1 = negative)
#define MOUSE_PACKET_X_NEGATIVE  0x10  // X movement direction (1 = negative)
#define MOUSE_PACKET_ALWAYS_1    0x08  // Always 1 bit
#define MOUSE_PACKET_MIDDLE_BTN  0x04  // Middle button state
#define MOUSE_PACKET_RIGHT_BTN   0x02  // Right button state
#define MOUSE_PACKET_LEFT_BTN    0x01  // Left button state

// Mouse types
#define MOUSE_TYPE_STANDARD     0x00  // Standard PS/2 mouse (2 buttons)
#define MOUSE_TYPE_WHEEL        0x03  // Mouse with scroll wheel
#define MOUSE_TYPE_5BUTTON      0x04  // 5-button mouse

// Mouse driver event types
typedef enum {
    MOUSE_EVENT_MOVE       = 0x01,  // Mouse movement
    MOUSE_EVENT_BUTTON     = 0x02,  // Button press/release
    MOUSE_EVENT_WHEEL      = 0x03   // Wheel movement
} mouse_event_type_t;

// Mouse button states
typedef enum {
    MOUSE_BUTTON_LEFT      = 0x01,  // Left button
    MOUSE_BUTTON_RIGHT     = 0x02,  // Right button
    MOUSE_BUTTON_MIDDLE    = 0x03,  // Middle button
    MOUSE_BUTTON_EXTRA1    = 0x04,  // Extra button 1 (typically "back")
    MOUSE_BUTTON_EXTRA2    = 0x05   // Extra button 2 (typically "forward")
} mouse_button_t;

// Mouse event structure
typedef struct {
    mouse_event_type_t type;     // Event type
    uint8_t  buttons;            // Button state bitmap
    int16_t  x_rel;              // Relative X movement
    int16_t  y_rel;              // Relative Y movement
    int8_t   wheel_rel;          // Relative wheel movement
    uint32_t timestamp;          // Event timestamp
} mouse_event_t;

// Mouse device private structure
typedef struct {
    uint8_t  mouse_type;         // Mouse type identifier
    bool     has_wheel;          // Whether mouse has a scroll wheel
    bool     has_5buttons;       // Whether mouse has 5 buttons
    uint8_t  sample_rate;        // Current sample rate
    uint8_t  resolution;         // Current resolution
    uint8_t  packets[4];         // Raw packet data
    uint8_t  packet_index;       // Current packet index
    uint8_t  packet_size;        // Expected packet size
    uint8_t  current_buttons;    // Current button states
    int16_t  x_pos;              // Current absolute X position
    int16_t  y_pos;              // Current absolute Y position
    uint8_t  irq;                // IRQ number
    bool     enabled;            // Whether data reporting is enabled
    bool     initialized;        // Whether the device is initialized
    void*    event_queue;        // Event queue for mouse events
} ps2_mouse_device_t;

/**
 * Initialize PS/2 mouse driver
 * 
 * @return 0 on success, negative error code on failure
 */
int ps2_mouse_init(void);

/**
 * Reset PS/2 mouse to default state
 * 
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int ps2_mouse_reset(device_t* dev);

/**
 * Enable data reporting for PS/2 mouse
 * 
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int ps2_mouse_enable(device_t* dev);

/**
 * Disable data reporting for PS/2 mouse
 * 
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int ps2_mouse_disable(device_t* dev);

/**
 * Set sample rate for PS/2 mouse
 * 
 * @param dev Device pointer
 * @param rate Sample rate (typically 10, 20, 40, 60, 80, 100, 200)
 * @return 0 on success, negative error code on failure
 */
int ps2_mouse_set_sample_rate(device_t* dev, uint8_t rate);

/**
 * Get next event from mouse device
 * 
 * @param dev Device pointer
 * @param event Pointer to event structure to fill
 * @return 1 if event returned, 0 if no events, negative error code on failure
 */
int ps2_mouse_get_event(device_t* dev, mouse_event_t* event);

/**
 * Set absolute position of mouse cursor
 * 
 * @param dev Device pointer
 * @param x X coordinate
 * @param y Y coordinate
 * @return 0 on success, negative error code on failure
 */
int ps2_mouse_set_position(device_t* dev, int16_t x, int16_t y);

#endif /* PS2_MOUSE_H */