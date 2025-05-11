/**
 * @file ps2_mouse.c
 * @brief PS/2 Mouse driver implementation for uintOS
 * 
 * This file implements the driver support for PS/2 mouse devices including
 * standard PS/2 mice and compatible devices like touchpads.
 */

#include "ps2_mouse.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include "../../hal/include/hal_io.h"
#include "../../hal/include/hal_interrupt.h"
#include "../../kernel/sync.h"
#include <string.h>

#define PS2_MOUSE_TAG "PS2_MOUSE"
#define PS2_MOUSE_IRQ 12  // Standard IRQ for PS/2 mouse

// Timer for detecting timeout while waiting for PS/2 controller
#define PS2_TIMEOUT 10000  // Timeout in loop iterations

// Maximum number of events in the queue
#define MAX_MOUSE_EVENTS 32

// Mouse event queue structure
typedef struct {
    mouse_event_t events[MAX_MOUSE_EVENTS];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    mutex_t mutex;
} mouse_event_queue_t;

// Global mouse device structure
static ps2_mouse_device_t* mouse_device = NULL;

// Forward declarations for internal functions
static int ps2_mouse_wait_input(void);
static int ps2_mouse_wait_output(void);
static int ps2_mouse_send_command(uint8_t cmd);
static int ps2_mouse_send_data(uint8_t data);
static uint8_t ps2_mouse_read_data(void);
static void ps2_mouse_irq_handler(registers_t* regs);
static void ps2_mouse_process_packet(void);
static int ps2_mouse_detect_type(void);

// Device operations
static int ps2_mouse_dev_open(device_t* dev, uint32_t flags);
static int ps2_mouse_dev_close(device_t* dev);
static int ps2_mouse_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset);
static int ps2_mouse_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset);
static int ps2_mouse_dev_ioctl(device_t* dev, int request, void* arg);

// Device operations structure
static device_ops_t ps2_mouse_ops = {
    .open = ps2_mouse_dev_open,
    .close = ps2_mouse_dev_close,
    .read = ps2_mouse_dev_read,
    .write = ps2_mouse_dev_write,
    .ioctl = ps2_mouse_dev_ioctl
};

/**
 * Wait for PS/2 controller to be ready for input
 * (wait for input buffer to be empty)
 */
static int ps2_mouse_wait_input(void) {
    uint32_t timeout = PS2_TIMEOUT;
    while (timeout--) {
        if ((hal_io_inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return 0;
        }
    }
    return -1;  // Timeout
}

/**
 * Wait for PS/2 controller to be ready for output
 * (wait for output buffer to be full)
 */
static int ps2_mouse_wait_output(void) {
    uint32_t timeout = PS2_TIMEOUT;
    while (timeout--) {
        if ((hal_io_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return 0;
        }
    }
    return -1;  // Timeout
}

/**
 * Send a PS/2 controller command
 */
static int ps2_mouse_send_command(uint8_t cmd) {
    if (ps2_mouse_wait_input() < 0) {
        return -1;
    }
    
    hal_io_outb(PS2_COMMAND_PORT, cmd);
    return 0;
}

/**
 * Send data to the PS/2 mouse
 */
static int ps2_mouse_send_data(uint8_t data) {
    // Wait for the PS/2 controller to be ready
    if (ps2_mouse_wait_input() < 0) {
        return -1;
    }
    
    // Send the "Write to Mouse" command
    hal_io_outb(PS2_COMMAND_PORT, 0xD4);
    
    // Wait for the PS/2 controller to be ready again
    if (ps2_mouse_wait_input() < 0) {
        return -1;
    }
    
    // Send the data
    hal_io_outb(PS2_DATA_PORT, data);
    
    // Wait for acknowledgment (0xFA = ACK)
    if (ps2_mouse_wait_output() < 0) {
        return -1;
    }
    
    uint8_t response = hal_io_inb(PS2_DATA_PORT);
    if (response != 0xFA) {
        log_warning(PS2_MOUSE_TAG, "Mouse did not acknowledge command 0x%02X (got 0x%02X)", 
                   data, response);
        return -1;
    }
    
    return 0;
}

/**
 * Read data from the PS/2 controller
 */
static uint8_t ps2_mouse_read_data(void) {
    // Wait for data to be available
    if (ps2_mouse_wait_output() < 0) {
        return 0xFF;  // Error value
    }
    
    // Read the data
    return hal_io_inb(PS2_DATA_PORT);
}

/**
 * Handle PS/2 mouse IRQ
 */
static void ps2_mouse_irq_handler(registers_t* regs) {
    // Don't do anything if device is not initialized
    if (!mouse_device || !mouse_device->initialized) {
        // Just read the data to clear the interrupt
        hal_io_inb(PS2_DATA_PORT);
        return;
    }
    
    // Read the data byte from the PS/2 controller
    uint8_t data = hal_io_inb(PS2_DATA_PORT);
    
    // Store the byte in the packet buffer
    mouse_device->packets[mouse_device->packet_index] = data;
    mouse_device->packet_index++;
    
    // Check if we have received a complete packet
    if (mouse_device->packet_index >= mouse_device->packet_size) {
        // Process the packet
        ps2_mouse_process_packet();
        
        // Reset the packet index for the next packet
        mouse_device->packet_index = 0;
    }
}

/**
 * Process a complete mouse packet
 */
static void ps2_mouse_process_packet(void) {
    mouse_event_t event;
    memset(&event, 0, sizeof(event));
    
    // Timestamp the event
    // In a real system, we would use a system time function
    static uint32_t timestamp = 0;
    event.timestamp = timestamp++;
    
    // Get button states from the first byte
    uint8_t buttons = mouse_device->packets[0] & 0x07;  // Bits 0, 1, 2 for Left, Right, Middle
    
    // Check if any button state changed
    if (buttons != mouse_device->current_buttons) {
        event.type = MOUSE_EVENT_BUTTON;
        event.buttons = buttons;
        mouse_device->current_buttons = buttons;
        
        // Add event to the queue if there's space
        mouse_event_queue_t* queue = (mouse_event_queue_t*)mouse_device->event_queue;
        
        if (queue->count < MAX_MOUSE_EVENTS) {
            mutex_lock(&queue->mutex);
            
            queue->events[queue->tail] = event;
            queue->tail = (queue->tail + 1) % MAX_MOUSE_EVENTS;
            queue->count++;
            
            mutex_unlock(&queue->mutex);
        }
    }
    
    // Get X and Y movement from bytes 1 and 2
    int16_t x_mov = mouse_device->packets[1];
    int16_t y_mov = mouse_device->packets[2];
    
    // Handle sign extension for negative values
    if (mouse_device->packets[0] & MOUSE_PACKET_X_NEGATIVE) {
        x_mov |= 0xFF00;  // Sign extend to 16 bits
    }
    
    if (mouse_device->packets[0] & MOUSE_PACKET_Y_NEGATIVE) {
        y_mov |= 0xFF00;  // Sign extend to 16 bits
    }
    
    // In PS/2, positive Y is down, so we negate it to match screen coordinates
    y_mov = -y_mov;
    
    // Check if there's any movement
    if (x_mov != 0 || y_mov != 0) {
        event.type = MOUSE_EVENT_MOVE;
        event.buttons = buttons;
        event.x_rel = x_mov;
        event.y_rel = y_mov;
        
        // Update absolute positions
        mouse_device->x_pos += x_mov;
        mouse_device->y_pos += y_mov;
        
        // Add event to the queue if there's space
        mouse_event_queue_t* queue = (mouse_event_queue_t*)mouse_device->event_queue;
        
        if (queue->count < MAX_MOUSE_EVENTS) {
            mutex_lock(&queue->mutex);
            
            queue->events[queue->tail] = event;
            queue->tail = (queue->tail + 1) % MAX_MOUSE_EVENTS;
            queue->count++;
            
            mutex_unlock(&queue->mutex);
        }
    }
    
    // Handle scroll wheel movement (if available) in the 4th byte
    if (mouse_device->has_wheel && mouse_device->packet_size >= 4) {
        int8_t wheel_mov = (int8_t)mouse_device->packets[3];
        
        if (wheel_mov != 0) {
            event.type = MOUSE_EVENT_WHEEL;
            event.buttons = buttons;
            event.wheel_rel = wheel_mov;
            
            // Add event to the queue if there's space
            mouse_event_queue_t* queue = (mouse_event_queue_t*)mouse_device->event_queue;
            
            if (queue->count < MAX_MOUSE_EVENTS) {
                mutex_lock(&queue->mutex);
                
                queue->events[queue->tail] = event;
                queue->tail = (queue->tail + 1) % MAX_MOUSE_EVENTS;
                queue->count++;
                
                mutex_unlock(&queue->mutex);
            }
        }
    }
}

/**
 * Detect the mouse type using the ID byte and sample rate trick
 */
static int ps2_mouse_detect_type(void) {
    // First, reset the mouse to a known state
    if (ps2_mouse_send_data(MOUSE_CMD_RESET) < 0) {
        return -1;
    }
    
    // Wait for the 0xAA (self-test passed) response
    if (ps2_mouse_wait_output() < 0) {
        return -1;
    }
    
    uint8_t response = ps2_mouse_read_data();
    if (response != 0xAA) {
        log_warning(PS2_MOUSE_TAG, "Mouse reset: unexpected response 0x%02X", response);
        return -1;
    }
    
    // The mouse also sends its ID byte after reset
    if (ps2_mouse_wait_output() < 0) {
        return -1;
    }
    
    uint8_t id = ps2_mouse_read_data();
    log_info(PS2_MOUSE_TAG, "Mouse ID byte: 0x%02X", id);
    
    // Start with basic detection based on ID
    mouse_device->mouse_type = id;
    mouse_device->has_wheel = false;
    mouse_device->has_5buttons = false;
    mouse_device->packet_size = 3;  // Standard PS/2 mouse has 3-byte packets
    
    // Most standard wheels mice identify themselves as a standard mouse (ID 0x00)
    // We need to use the sample rate trick to detect them
    
    // Try to enable wheel mouse mode
    // The sequence 200, 100, 80 is a magic knock to enable wheel mode on compatible mice
    if (id == MOUSE_TYPE_STANDARD) {
        // Set sample rate 200
        if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
            return -1;
        }
        if (ps2_mouse_send_data(200) < 0) {
            return -1;
        }
        
        // Set sample rate 100
        if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
            return -1;
        }
        if (ps2_mouse_send_data(100) < 0) {
            return -1;
        }
        
        // Set sample rate 80
        if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
            return -1;
        }
        if (ps2_mouse_send_data(80) < 0) {
            return -1;
        }
        
        // Request ID again
        if (ps2_mouse_send_data(MOUSE_CMD_GET_DEVID) < 0) {
            return -1;
        }
        
        // Read the ID byte
        if (ps2_mouse_wait_output() < 0) {
            return -1;
        }
        
        id = ps2_mouse_read_data();
        log_info(PS2_MOUSE_TAG, "Mouse ID after sample rate trick: 0x%02X", id);
        
        // ID 0x03 indicates a mouse with scroll wheel
        if (id == MOUSE_TYPE_WHEEL) {
            mouse_device->mouse_type = MOUSE_TYPE_WHEEL;
            mouse_device->has_wheel = true;
            mouse_device->packet_size = 4;  // 4-byte packets for wheel mouse
            log_info(PS2_MOUSE_TAG, "Detected mouse with scroll wheel");
        }
        
        // Now try the 5-button detection sequence
        // The sequence 200, 200, 80 is a magic knock to enable 5-button mode on compatible mice
        
        // Set sample rate 200
        if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
            return -1;
        }
        if (ps2_mouse_send_data(200) < 0) {
            return -1;
        }
        
        // Set sample rate 200
        if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
            return -1;
        }
        if (ps2_mouse_send_data(200) < 0) {
            return -1;
        }
        
        // Set sample rate 80
        if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
            return -1;
        }
        if (ps2_mouse_send_data(80) < 0) {
            return -1;
        }
        
        // Request ID again
        if (ps2_mouse_send_data(MOUSE_CMD_GET_DEVID) < 0) {
            return -1;
        }
        
        // Read the ID byte
        if (ps2_mouse_wait_output() < 0) {
            return -1;
        }
        
        id = ps2_mouse_read_data();
        log_info(PS2_MOUSE_TAG, "Mouse ID after 5-button trick: 0x%02X", id);
        
        // ID 0x04 indicates a 5-button mouse
        if (id == MOUSE_TYPE_5BUTTON) {
            mouse_device->mouse_type = MOUSE_TYPE_5BUTTON;
            mouse_device->has_wheel = true;
            mouse_device->has_5buttons = true;
            mouse_device->packet_size = 4;  // 4-byte packets for wheel and 5-button mice
            log_info(PS2_MOUSE_TAG, "Detected 5-button mouse");
        }
    }
    
    return 0;
}

/**
 * Initialize PS/2 mouse driver
 */
int ps2_mouse_init(void) {
    log_info(PS2_MOUSE_TAG, "Initializing PS/2 mouse driver");
    
    // Check if already initialized
    if (mouse_device) {
        log_warning(PS2_MOUSE_TAG, "Mouse already initialized");
        return 0;
    }
    
    // Allocate device structure
    mouse_device = (ps2_mouse_device_t*)heap_alloc(sizeof(ps2_mouse_device_t));
    if (!mouse_device) {
        log_error(PS2_MOUSE_TAG, "Failed to allocate memory for mouse device");
        return -1;
    }
    
    // Initialize device structure
    memset(mouse_device, 0, sizeof(ps2_mouse_device_t));
    mouse_device->irq = PS2_MOUSE_IRQ;
    
    // Allocate event queue
    mouse_event_queue_t* queue = (mouse_event_queue_t*)heap_alloc(sizeof(mouse_event_queue_t));
    if (!queue) {
        log_error(PS2_MOUSE_TAG, "Failed to allocate memory for mouse event queue");
        heap_free(mouse_device);
        mouse_device = NULL;
        return -1;
    }
    
    // Initialize event queue
    memset(queue, 0, sizeof(mouse_event_queue_t));
    mutex_init(&queue->mutex);
    
    mouse_device->event_queue = queue;
    
    // Enable the PS/2 mouse port
    // 1. Disable port 2 (mouse)
    ps2_mouse_send_command(PS2_CMD_DISABLE_PORT2);
    
    // 2. Flush the output buffer
    ps2_mouse_read_data();
    
    // 3. Get the controller configuration byte
    ps2_mouse_send_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_mouse_read_data();
    
    // 4. Enable the auxiliary device (mouse)
    config |= 0x02;  // Enable IRQ12 (bit 1)
    
    // 5. Write modified configuration byte back
    ps2_mouse_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_mouse_wait_input();
    hal_io_outb(PS2_DATA_PORT, config);
    
    // 6. Enable the mouse port
    ps2_mouse_send_command(PS2_CMD_ENABLE_PORT2);
    
    // 7. Setup the mouse
    // Reset the mouse
    if (ps2_mouse_send_data(MOUSE_CMD_RESET) < 0) {
        log_error(PS2_MOUSE_TAG, "Failed to reset mouse");
        goto cleanup;
    }
    
    // Wait for acknowledgment (0xFA = ACK) (already handled in ps2_mouse_send_data)
    // Wait for completion (0xAA = Self-test passed)
    if (ps2_mouse_wait_output() < 0) {
        log_error(PS2_MOUSE_TAG, "Mouse reset timeout");
        goto cleanup;
    }
    
    uint8_t response = ps2_mouse_read_data();
    if (response != 0xAA) {
        log_error(PS2_MOUSE_TAG, "Mouse self-test failed: 0x%02X", response);
        goto cleanup;
    }
    
    // The mouse will also send its ID
    if (ps2_mouse_wait_output() < 0) {
        log_error(PS2_MOUSE_TAG, "Failed to read mouse ID");
        goto cleanup;
    }
    
    uint8_t mouse_id = ps2_mouse_read_data();
    
    // Set default settings
    if (ps2_mouse_send_data(MOUSE_CMD_SET_DEFAULTS) < 0) {
        log_error(PS2_MOUSE_TAG, "Failed to set default settings");
        goto cleanup;
    }
    
    // Enable packet streaming
    if (ps2_mouse_send_data(MOUSE_CMD_ENABLE) < 0) {
        log_error(PS2_MOUSE_TAG, "Failed to enable mouse data reporting");
        goto cleanup;
    }
    
    // Detect mouse type
    if (ps2_mouse_detect_type() < 0) {
        log_error(PS2_MOUSE_TAG, "Failed to detect mouse type");
        goto cleanup;
    }
    
    // Register IRQ handler
    if (hal_interrupt_register_handler(mouse_device->irq, ps2_mouse_irq_handler) != HAL_SUCCESS) {
        log_error(PS2_MOUSE_TAG, "Failed to register mouse IRQ handler");
        goto cleanup;
    }
    
    // Create a device in the device manager
    device_t* mouse_dev = (device_t*)heap_alloc(sizeof(device_t));
    if (!mouse_dev) {
        log_error(PS2_MOUSE_TAG, "Failed to allocate device structure");
        goto cleanup;
    }
    
    memset(mouse_dev, 0, sizeof(device_t));
    
    strncpy(mouse_dev->name, "ps2_mouse", sizeof(mouse_dev->name));
    mouse_dev->type = DEVICE_TYPE_INPUT;
    mouse_dev->status = DEVICE_STATUS_ENABLED;
    mouse_dev->irq = mouse_device->irq;
    mouse_dev->private_data = mouse_device;
    mouse_dev->ops = &ps2_mouse_ops;
    
    // Register the device with the device manager
    if (device_register(mouse_dev) != DEVICE_OK) {
        log_error(PS2_MOUSE_TAG, "Failed to register mouse device");
        heap_free(mouse_dev);
        goto cleanup;
    }
    
    mouse_device->enabled = true;
    mouse_device->initialized = true;
    
    log_info(PS2_MOUSE_TAG, "PS/2 mouse initialized successfully");
    return 0;
    
cleanup:
    if (mouse_device) {
        if (mouse_device->event_queue) {
            heap_free(mouse_device->event_queue);
        }
        heap_free(mouse_device);
        mouse_device = NULL;
    }
    return -1;
}

/**
 * Reset PS/2 mouse to default state
 */
int ps2_mouse_reset(device_t* dev) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    
    // First disable reporting
    if (ps2_mouse_send_data(MOUSE_CMD_DISABLE) < 0) {
        return -1;
    }
    
    // Reset the mouse
    if (ps2_mouse_send_data(MOUSE_CMD_RESET) < 0) {
        return -1;
    }
    
    // Wait for reset completion
    if (ps2_mouse_wait_output() < 0) {
        return -1;
    }
    
    uint8_t response = ps2_mouse_read_data();
    if (response != 0xAA) {
        return -1;
    }
    
    // Set default settings
    if (ps2_mouse_send_data(MOUSE_CMD_SET_DEFAULTS) < 0) {
        return -1;
    }
    
    // Detect mouse type again
    if (ps2_mouse_detect_type() < 0) {
        return -1;
    }
    
    // Re-enable reporting if it was enabled
    if (mouse->enabled) {
        if (ps2_mouse_send_data(MOUSE_CMD_ENABLE) < 0) {
            return -1;
        }
    }
    
    // Reset position
    mouse->x_pos = 0;
    mouse->y_pos = 0;
    mouse->current_buttons = 0;
    
    // Clear the event queue
    mouse_event_queue_t* queue = (mouse_event_queue_t*)mouse->event_queue;
    mutex_lock(&queue->mutex);
    queue->head = queue->tail = queue->count = 0;
    mutex_unlock(&queue->mutex);
    
    return 0;
}

/**
 * Enable data reporting for PS/2 mouse
 */
int ps2_mouse_enable(device_t* dev) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    
    // Send the enable command
    if (ps2_mouse_send_data(MOUSE_CMD_ENABLE) < 0) {
        return -1;
    }
    
    mouse->enabled = true;
    return 0;
}

/**
 * Disable data reporting for PS/2 mouse
 */
int ps2_mouse_disable(device_t* dev) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    
    // Send the disable command
    if (ps2_mouse_send_data(MOUSE_CMD_DISABLE) < 0) {
        return -1;
    }
    
    mouse->enabled = false;
    return 0;
}

/**
 * Set sample rate for PS/2 mouse
 */
int ps2_mouse_set_sample_rate(device_t* dev, uint8_t rate) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Valid rates are 10, 20, 40, 60, 80, 100, 200
    if (rate != 10 && rate != 20 && rate != 40 && rate != 60 && 
        rate != 80 && rate != 100 && rate != 200) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    
    // Send sample rate command
    if (ps2_mouse_send_data(MOUSE_CMD_SET_SAMPLE) < 0) {
        return -1;
    }
    
    // Send the rate
    if (ps2_mouse_send_data(rate) < 0) {
        return -1;
    }
    
    mouse->sample_rate = rate;
    return 0;
}

/**
 * Get next event from mouse device
 */
int ps2_mouse_get_event(device_t* dev, mouse_event_t* event) {
    if (!dev || !dev->private_data || !event) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    mouse_event_queue_t* queue = (mouse_event_queue_t*)mouse->event_queue;
    
    // Check if there's an event available
    mutex_lock(&queue->mutex);
    
    if (queue->count == 0) {
        mutex_unlock(&queue->mutex);
        return 0;  // No events
    }
    
    // Copy the event from the queue
    *event = queue->events[queue->head];
    
    // Update the queue
    queue->head = (queue->head + 1) % MAX_MOUSE_EVENTS;
    queue->count--;
    
    mutex_unlock(&queue->mutex);
    
    return 1;  // Event returned
}

/**
 * Set absolute position of mouse cursor
 */
int ps2_mouse_set_position(device_t* dev, int16_t x, int16_t y) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    
    // Update position
    mouse->x_pos = x;
    mouse->y_pos = y;
    
    return 0;
}

/**
 * Open PS/2 mouse device
 */
static int ps2_mouse_dev_open(device_t* dev, uint32_t flags) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Enable the mouse if not already enabled
    return ps2_mouse_enable(dev);
}

/**
 * Close PS/2 mouse device
 */
static int ps2_mouse_dev_close(device_t* dev) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Nothing special to do for close
    return DEVICE_OK;
}

/**
 * Read from PS/2 mouse device (get events)
 */
static int ps2_mouse_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if the buffer is large enough for at least one event
    if (size < sizeof(mouse_event_t)) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get events from the queue
    mouse_event_t* events = (mouse_event_t*)buffer;
    size_t max_events = size / sizeof(mouse_event_t);
    size_t events_read = 0;
    
    for (size_t i = 0; i < max_events; i++) {
        int result = ps2_mouse_get_event(dev, &events[i]);
        
        if (result == 1) {
            events_read++;
        } else {
            break;  // No more events
        }
    }
    
    // Return the number of bytes read
    return events_read * sizeof(mouse_event_t);
}

/**
 * Write to PS/2 mouse device (not supported)
 */
static int ps2_mouse_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset) {
    // Write operation not supported for mouse
    return DEVICE_ERROR_UNSUPPORTED;
}

/**
 * IOCTL for PS/2 mouse device
 */
static int ps2_mouse_dev_ioctl(device_t* dev, int request, void* arg) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    ps2_mouse_device_t* mouse = (ps2_mouse_device_t*)dev->private_data;
    
    switch (request) {
        case 0x5001:  // Get mouse info
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                // Assuming arg is a struct with space for mouse info
                struct {
                    uint8_t mouse_type;
                    bool has_wheel;
                    bool has_5buttons;
                    int16_t x_pos;
                    int16_t y_pos;
                    uint8_t buttons;
                } *info = (void*)arg;
                
                info->mouse_type = mouse->mouse_type;
                info->has_wheel = mouse->has_wheel;
                info->has_5buttons = mouse->has_5buttons;
                info->x_pos = mouse->x_pos;
                info->y_pos = mouse->y_pos;
                info->buttons = mouse->current_buttons;
                
                return DEVICE_OK;
            }
            
        case 0x5002:  // Reset mouse
            return ps2_mouse_reset(dev);
            
        case 0x5003:  // Enable mouse
            return ps2_mouse_enable(dev);
            
        case 0x5004:  // Disable mouse
            return ps2_mouse_disable(dev);
            
        case 0x5005:  // Set sample rate
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                uint8_t rate = *(uint8_t*)arg;
                return ps2_mouse_set_sample_rate(dev, rate);
            }
            
        case 0x5006:  // Set position
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                int16_t* pos = (int16_t*)arg;
                return ps2_mouse_set_position(dev, pos[0], pos[1]);
            }
            
        default:
            return DEVICE_ERROR_UNSUPPORTED;
    }
}