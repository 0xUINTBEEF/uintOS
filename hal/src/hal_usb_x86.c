/**
 * @file hal_usb_x86.c
 * @brief USB Hardware Abstraction Layer implementation for x86
 *
 * This file implements USB host controller operations for x86 platforms
 * supporting UHCI, OHCI, and EHCI controllers.
 */

#include "../include/hal_usb.h"
#include "../include/hal.h"
#include "../include/hal_io.h"
#include "../include/hal_memory.h"
#include "../include/hal_interrupt.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include <string.h>
#include <stddef.h>

// Control codes for USB controllers
#define MAX_USB_CONTROLLERS  8        // Maximum supported USB controllers
#define MAX_USB_DEVICES      32       // Maximum supported USB devices
#define MAX_USB_TRANSFERS    64       // Maximum concurrent transfers

// Controller types
typedef enum {
    USB_CONTROLLER_NONE = 0,
    USB_CONTROLLER_UHCI,              // Universal Host Controller Interface (USB 1.1)
    USB_CONTROLLER_OHCI,              // Open Host Controller Interface (USB 1.1) 
    USB_CONTROLLER_EHCI,              // Enhanced Host Controller Interface (USB 2.0)
    USB_CONTROLLER_XHCI               // Extensible Host Controller Interface (USB 3.0)
} usb_controller_type_t;

// USB Controller structure
typedef struct {
    usb_controller_type_t type;       // Controller type
    uint32_t base_address;            // Base memory or I/O address
    uint8_t irq;                      // IRQ number
    uint8_t bus;                      // PCI bus number
    uint8_t device;                   // PCI device number
    uint8_t function;                 // PCI function number
    uint8_t num_ports;                // Number of root hub ports
    uint8_t num_devices;              // Number of devices connected
    hal_usb_controller_caps_t caps;   // Controller capabilities
    void* private_data;               // Controller-specific private data
} usb_controller_t;

// USB Device structure 
typedef struct {
    uint8_t address;                  // Device address (1-127)
    uint8_t port;                     // Port number
    uint8_t controller_id;            // Controller ID
    bool connected;                   // Device connection status
    hal_usb_speed_t speed;            // Device speed
    hal_usb_device_info_t info;       // Device information
} usb_device_t;

// Transfer structure
typedef struct {
    int id;                           // Transfer ID
    uint8_t device_addr;              // Device address
    uint8_t endpoint;                 // Endpoint address
    hal_usb_transfer_type_t type;     // Transfer type
    void* data;                       // Data buffer
    uint32_t length;                  // Data length
    uint32_t actual_length;           // Actual bytes transferred
    int status;                       // Transfer status
    hal_usb_transfer_callback_t callback; // Completion callback
    void* context;                    // User context for callback
    bool in_use;                      // Whether this slot is in use
} usb_transfer_t;

// --- Global variables ---
static bool usb_initialized = false;                // Initialization flag
static usb_controller_t controllers[MAX_USB_CONTROLLERS]; // Controllers array
static usb_device_t devices[MAX_USB_DEVICES];       // Devices array
static usb_transfer_t transfers[MAX_USB_TRANSFERS]; // Transfers array
static uint8_t num_controllers = 0;                 // Number of controllers

// --- Forward declarations for internal functions ---
static int detect_usb_controllers(void);
static int initialize_controller(usb_controller_t* controller);
static void usb_interrupt_handler(void* context);
static int allocate_device_address(void);
static int usb_get_device_by_address(uint8_t address);
static int usb_allocate_transfer(void);

/**
 * Initialize the USB subsystem and detect controllers
 * 
 * @return 0 on success, negative value on error
 */
int hal_usb_init(void) {
    if (usb_initialized) {
        return 0; // Already initialized
    }

    log_info("USB", "Initializing USB subsystem");
    
    // Initialize arrays
    memset(controllers, 0, sizeof(controllers));
    memset(devices, 0, sizeof(devices));
    memset(transfers, 0, sizeof(transfers));
    
    // Detect USB controllers
    int result = detect_usb_controllers();
    if (result < 0) {
        log_error("USB", "Failed to detect USB controllers: %d", result);
        return result;
    }

    if (num_controllers == 0) {
        log_warning("USB", "No USB controllers found");
        return 0;
    }
    
    log_info("USB", "Found %d USB controllers", num_controllers);
    
    // Initialize detected controllers
    for (int i = 0; i < num_controllers; i++) {
        result = initialize_controller(&controllers[i]);
        if (result < 0) {
            log_error("USB", "Failed to initialize controller %d: %d", i, result);
            // Continue with other controllers
        } else {
            log_info("USB", "Initialized controller %d: type=%d, ports=%d", 
                    i, controllers[i].type, controllers[i].num_ports);
        }
    }
    
    usb_initialized = true;
    log_info("USB", "USB subsystem initialized successfully");
    return 0;
}

/**
 * Shut down the USB subsystem
 */
void hal_usb_shutdown(void) {
    if (!usb_initialized) {
        return;
    }
    
    log_info("USB", "Shutting down USB subsystem");
    
    // Free controller resources and unregister interrupts
    for (int i = 0; i < num_controllers; i++) {
        // Controller-specific shutdown code would go here
        
        // Free private data if allocated
        if (controllers[i].private_data) {
            free(controllers[i].private_data);
            controllers[i].private_data = NULL;
        }
    }
    
    usb_initialized = false;
    log_info("USB", "USB subsystem shut down");
}

/**
 * Get USB host controller capabilities
 * 
 * @param controller_id Controller ID (0-based)
 * @param caps Pointer to capabilities structure to fill
 * @return 0 on success, negative value on error
 */
int hal_usb_get_controller_caps(uint8_t controller_id, hal_usb_controller_caps_t* caps) {
    // Validate parameters
    if (controller_id >= num_controllers || !caps) {
        return -1;
    }
    
    // Copy capabilities
    memcpy(caps, &controllers[controller_id].caps, sizeof(hal_usb_controller_caps_t));
    return 0;
}

/**
 * Enumerate devices connected to the USB
 * 
 * @param devices_info Array to store device information
 * @param max_devices Maximum number of devices to enumerate
 * @return Number of devices found, or negative value on error
 */
int hal_usb_enumerate_devices(hal_usb_device_info_t* devices_info, uint8_t max_devices) {
    if (!usb_initialized) {
        return -1;
    }
    
    if (!devices_info || max_devices == 0) {
        return -1;
    }
    
    // Count connected devices and copy their information
    int count = 0;
    for (int i = 0; i < MAX_USB_DEVICES && count < max_devices; i++) {
        if (devices[i].connected) {
            memcpy(&devices_info[count], &devices[i].info, sizeof(hal_usb_device_info_t));
            count++;
        }
    }
    
    return count;
}

/**
 * Reset a USB port
 * 
 * @param controller_id Controller ID (0-based)
 * @param port Port number (0-based)
 * @return 0 on success, negative value on error
 */
int hal_usb_reset_port(uint8_t controller_id, uint8_t port) {
    // Validate parameters
    if (controller_id >= num_controllers) {
        return -1;
    }
    
    if (port >= controllers[controller_id].num_ports) {
        return -1;
    }
    
    // The actual implementation depends on the controller type
    // This is a simplified placeholder
    
    log_info("USB", "Resetting controller %d port %d", controller_id, port);
    
    // Simulate port reset for demonstration
    // In a real implementation, we would:
    // 1. Set the port reset bit in the controller
    // 2. Wait for the minimum reset time (10ms for USB 2.0)
    // 3. Clear the port reset bit
    // 4. Wait for the port to stabilize

    return 0;
}

/**
 * Perform a control transfer to a USB device
 * 
 * @param device_addr Device address
 * @param request_type Request type
 * @param request Request
 * @param value Value
 * @param index Index
 * @param data Data buffer (NULL if none)
 * @param length Data buffer length
 * @param callback Completion callback (NULL for synchronous)
 * @param context User context for callback
 * @return Transfer ID on success, negative value on error
 */
int hal_usb_control_transfer(uint8_t device_addr, uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, void* data, uint16_t length,
                             hal_usb_transfer_callback_t callback, void* context) {
    if (!usb_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Allocate a transfer structure
    int transfer_id = usb_allocate_transfer();
    if (transfer_id < 0) {
        return -1;
    }
    
    // Set up the transfer
    transfers[transfer_id].device_addr = device_addr;
    transfers[transfer_id].endpoint = 0; // Control endpoint is always 0
    transfers[transfer_id].type = HAL_USB_TRANSFER_CONTROL;
    transfers[transfer_id].data = data;
    transfers[transfer_id].length = length;
    transfers[transfer_id].actual_length = 0;
    transfers[transfer_id].status = -1; // Not completed
    transfers[transfer_id].callback = callback;
    transfers[transfer_id].context = context;
    
    // Placeholder for control transfer implementation
    // Actual implementation would:
    // 1. Create setup packet from request_type, request, value, index, length
    // 2. Submit to appropriate controller
    // 3. Handle completion via interrupt or polling
    
    // Simulate successful transfer for demonstration
    transfers[transfer_id].status = 0;
    transfers[transfer_id].actual_length = length;
    
    // Call the callback if provided
    if (callback) {
        hal_usb_transfer_result_t result = {
            .status = transfers[transfer_id].status,
            .actual_length = transfers[transfer_id].actual_length
        };
        callback(&result, context);
    }
    
    return transfer_id;
}

/**
 * Perform a bulk transfer to a USB device
 * 
 * @param device_addr Device address
 * @param endpoint Endpoint address
 * @param data Data buffer
 * @param length Data buffer length
 * @param callback Completion callback (NULL for synchronous)
 * @param context User context for callback
 * @return Transfer ID on success, negative value on error
 */
int hal_usb_bulk_transfer(uint8_t device_addr, uint8_t endpoint,
                          void* data, uint32_t length,
                          hal_usb_transfer_callback_t callback, void* context) {
    // Similar implementation to control transfer
    // The difference is in how the controller handles bulk vs. control transfers
    
    if (!usb_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Allocate a transfer structure
    int transfer_id = usb_allocate_transfer();
    if (transfer_id < 0) {
        return -1;
    }
    
    // Set up the transfer
    transfers[transfer_id].device_addr = device_addr;
    transfers[transfer_id].endpoint = endpoint;
    transfers[transfer_id].type = HAL_USB_TRANSFER_BULK;
    transfers[transfer_id].data = data;
    transfers[transfer_id].length = length;
    transfers[transfer_id].actual_length = 0;
    transfers[transfer_id].status = -1; // Not completed
    transfers[transfer_id].callback = callback;
    transfers[transfer_id].context = context;
    
    // Placeholder for bulk transfer implementation
    // Simulate successful transfer for demonstration
    transfers[transfer_id].status = 0;
    transfers[transfer_id].actual_length = length;
    
    // Call the callback if provided
    if (callback) {
        hal_usb_transfer_result_t result = {
            .status = transfers[transfer_id].status,
            .actual_length = transfers[transfer_id].actual_length
        };
        callback(&result, context);
    }
    
    return transfer_id;
}

/**
 * Perform an interrupt transfer to a USB device
 * 
 * @param device_addr Device address
 * @param endpoint Endpoint address
 * @param data Data buffer
 * @param length Data buffer length
 * @param callback Completion callback (NULL for synchronous)
 * @param context User context for callback
 * @return Transfer ID on success, negative value on error
 */
int hal_usb_interrupt_transfer(uint8_t device_addr, uint8_t endpoint,
                               void* data, uint32_t length,
                               hal_usb_transfer_callback_t callback, void* context) {
    // Similar implementation to bulk transfer
    // The difference is in how the controller schedules interrupt vs. bulk transfers
    
    if (!usb_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Allocate a transfer structure
    int transfer_id = usb_allocate_transfer();
    if (transfer_id < 0) {
        return -1;
    }
    
    // Set up the transfer
    transfers[transfer_id].device_addr = device_addr;
    transfers[transfer_id].endpoint = endpoint;
    transfers[transfer_id].type = HAL_USB_TRANSFER_INTERRUPT;
    transfers[transfer_id].data = data;
    transfers[transfer_id].length = length;
    transfers[transfer_id].actual_length = 0;
    transfers[transfer_id].status = -1; // Not completed
    transfers[transfer_id].callback = callback;
    transfers[transfer_id].context = context;
    
    // Placeholder for interrupt transfer implementation
    // Simulate successful transfer for demonstration
    transfers[transfer_id].status = 0;
    transfers[transfer_id].actual_length = length;
    
    // Call the callback if provided
    if (callback) {
        hal_usb_transfer_result_t result = {
            .status = transfers[transfer_id].status,
            .actual_length = transfers[transfer_id].actual_length
        };
        callback(&result, context);
    }
    
    return transfer_id;
}

/**
 * Cancel a pending USB transfer
 * 
 * @param transfer_id Transfer ID returned from transfer function
 * @return 0 on success, negative value on error
 */
int hal_usb_cancel_transfer(int transfer_id) {
    if (!usb_initialized) {
        return -1;
    }
    
    // Validate transfer ID
    if (transfer_id < 0 || transfer_id >= MAX_USB_TRANSFERS) {
        return -1;
    }
    
    // Check if the transfer is in use
    if (!transfers[transfer_id].in_use) {
        return -1;
    }
    
    // Placeholder for cancellation implementation
    // Actual implementation would signal the controller to abort the transfer
    
    // Mark the transfer as completed with cancelled status
    transfers[transfer_id].status = -2; // Cancelled
    
    // Call the callback if provided
    if (transfers[transfer_id].callback) {
        hal_usb_transfer_result_t result = {
            .status = transfers[transfer_id].status,
            .actual_length = transfers[transfer_id].actual_length
        };
        transfers[transfer_id].callback(&result, transfers[transfer_id].context);
    }
    
    // Free the transfer slot
    transfers[transfer_id].in_use = false;
    
    return 0;
}

/**
 * Get USB device descriptor
 * 
 * @param device_addr Device address
 * @param device_info Pointer to device info structure to fill
 * @return 0 on success, negative value on error
 */
int hal_usb_get_device_descriptor(uint8_t device_addr, hal_usb_device_info_t* device_info) {
    if (!usb_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Copy the descriptor
    memcpy(device_info, &devices[device_index].info, sizeof(hal_usb_device_info_t));
    
    return 0;
}

/**
 * Get USB string descriptor (converted to ASCII)
 * 
 * @param device_addr Device address
 * @param string_index String descriptor index
 * @param buffer Buffer to store string
 * @param buffer_size Buffer size
 * @return String length on success, negative value on error
 */
int hal_usb_get_string_descriptor(uint8_t device_addr, uint8_t string_index,
                                 char* buffer, uint16_t buffer_size) {
    if (!usb_initialized) {
        return -1;
    }
    
    // Validate parameters
    if (!buffer || buffer_size == 0) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Handle common string descriptors
    switch (string_index) {
        case 1: // Manufacturer
            strncpy(buffer, devices[device_index].info.manufacturer, buffer_size);
            buffer[buffer_size - 1] = '\0';
            return strlen(buffer);
            
        case 2: // Product
            strncpy(buffer, devices[device_index].info.product, buffer_size);
            buffer[buffer_size - 1] = '\0';
            return strlen(buffer);
            
        case 3: // Serial number
            strncpy(buffer, devices[device_index].info.serial_number, buffer_size);
            buffer[buffer_size - 1] = '\0';
            return strlen(buffer);
            
        default:
            // For other strings, we'd need to fetch them from the device
            // Placeholder: return empty string for now
            buffer[0] = '\0';
            return 0;
    }
}

// --- Internal helper functions ---

/**
 * Detect USB controllers in the system
 * 
 * @return Number of controllers found, or negative value on error
 */
static int detect_usb_controllers(void) {
    // In a real implementation, we would scan the PCI bus
    // For this sample, we'll simulate finding controllers
    
    // Simulate UHCI controller (USB 1.1)
    controllers[num_controllers].type = USB_CONTROLLER_UHCI;
    controllers[num_controllers].base_address = 0xC000; // I/O port base
    controllers[num_controllers].irq = 11;
    controllers[num_controllers].bus = 0;
    controllers[num_controllers].device = 29;
    controllers[num_controllers].function = 0;
    controllers[num_controllers].num_ports = 2;
    
    // Set capabilities
    controllers[num_controllers].caps.supports_low_speed = true;
    controllers[num_controllers].caps.supports_full_speed = true;
    controllers[num_controllers].caps.supports_high_speed = false;
    controllers[num_controllers].caps.supports_super_speed = false;
    controllers[num_controllers].caps.max_ports = 2;
    controllers[num_controllers].caps.max_bandwidth = 12; // 12 MBps
    
    num_controllers++;
    
    // Simulate EHCI controller (USB 2.0)
    controllers[num_controllers].type = USB_CONTROLLER_EHCI;
    controllers[num_controllers].base_address = 0xE000; // Memory mapped
    controllers[num_controllers].irq = 16;
    controllers[num_controllers].bus = 0;
    controllers[num_controllers].device = 29;
    controllers[num_controllers].function = 1;
    controllers[num_controllers].num_ports = 4;
    
    // Set capabilities
    controllers[num_controllers].caps.supports_low_speed = false;
    controllers[num_controllers].caps.supports_full_speed = false;
    controllers[num_controllers].caps.supports_high_speed = true;
    controllers[num_controllers].caps.supports_super_speed = false;
    controllers[num_controllers].caps.max_ports = 4;
    controllers[num_controllers].caps.max_bandwidth = 480; // 480 MBps
    
    num_controllers++;
    
    log_info("USB", "Detected %d USB controllers", num_controllers);
    return num_controllers;
}

/**
 * Initialize a USB controller
 * 
 * @param controller Pointer to controller structure
 * @return 0 on success, negative value on error
 */
static int initialize_controller(usb_controller_t* controller) {
    // Different initialization based on controller type
    switch (controller->type) {
        case USB_CONTROLLER_UHCI:
            log_info("USB", "Initializing UHCI controller");
            // UHCI specific initialization would go here
            
            // Register interrupt handler
            hal_interrupt_register_handler(controller->irq, usb_interrupt_handler, controller);
            break;
            
        case USB_CONTROLLER_OHCI:
            log_info("USB", "Initializing OHCI controller");
            // OHCI specific initialization would go here
            
            // Register interrupt handler
            hal_interrupt_register_handler(controller->irq, usb_interrupt_handler, controller);
            break;
            
        case USB_CONTROLLER_EHCI:
            log_info("USB", "Initializing EHCI controller");
            // EHCI specific initialization would go here
            
            // Register interrupt handler
            hal_interrupt_register_handler(controller->irq, usb_interrupt_handler, controller);
            break;
            
        case USB_CONTROLLER_XHCI:
            log_info("USB", "Initializing XHCI controller");
            // XHCI specific initialization would go here
            
            // Register interrupt handler
            hal_interrupt_register_handler(controller->irq, usb_interrupt_handler, controller);
            break;
            
        default:
            log_error("USB", "Unknown controller type: %d", controller->type);
            return -1;
    }
    
    return 0;
}

/**
 * USB interrupt handler
 * 
 * @param context Pointer to controller structure
 */
static void usb_interrupt_handler(void* context) {
    usb_controller_t* controller = (usb_controller_t*)context;
    
    // Controller-specific interrupt handling would go here
    
    // Acknowledge the interrupt
    hal_interrupt_acknowledge(controller->irq);
}

/**
 * Allocate a device address
 * 
 * @return Device address (1-127), or negative value on error
 */
static int allocate_device_address(void) {
    // Find the first unused address slot
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        if (!devices[i].connected) {
            // Start address from 1 (0 is reserved for default address)
            devices[i].address = i + 1;
            return devices[i].address;
        }
    }
    
    return -1; // No free address slots
}

/**
 * Find device index by address
 * 
 * @param address Device address
 * @return Device index, or negative value if not found
 */
static int usb_get_device_by_address(uint8_t address) {
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        if (devices[i].connected && devices[i].address == address) {
            return i;
        }
    }
    
    return -1; // Device not found
}

/**
 * Allocate a transfer structure
 * 
 * @return Transfer index, or negative value on error
 */
static int usb_allocate_transfer(void) {
    for (int i = 0; i < MAX_USB_TRANSFERS; i++) {
        if (!transfers[i].in_use) {
            transfers[i].in_use = true;
            transfers[i].id = i;
            return i;
        }
    }
    
    return -1; // No free transfer slots
}