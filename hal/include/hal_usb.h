/**
 * @file hal_usb.h
 * @brief USB Hardware Abstraction Layer interface
 *
 * This file defines the interface for USB host controller operations
 * in the uintOS hardware abstraction layer.
 */

#ifndef HAL_USB_H
#define HAL_USB_H

#include <stdint.h>
#include <stdbool.h>
#include "hal.h"

// USB device speed definitions
typedef enum {
    HAL_USB_SPEED_LOW = 0,      // 1.5 Mbps
    HAL_USB_SPEED_FULL = 1,     // 12 Mbps
    HAL_USB_SPEED_HIGH = 2,     // 480 Mbps
    HAL_USB_SPEED_SUPER = 3,    // 5 Gbps (USB 3.0)
} hal_usb_speed_t;

// USB transfer types
typedef enum {
    HAL_USB_TRANSFER_CONTROL = 0,
    HAL_USB_TRANSFER_ISOCHRONOUS = 1,
    HAL_USB_TRANSFER_BULK = 2,
    HAL_USB_TRANSFER_INTERRUPT = 3
} hal_usb_transfer_type_t;

// USB device information
typedef struct {
    uint16_t vendor_id;         // Vendor ID
    uint16_t product_id;        // Product ID
    uint16_t device_version;    // Device version
    uint8_t  device_class;      // Device class
    uint8_t  device_subclass;   // Device subclass
    uint8_t  device_protocol;   // Device protocol
    hal_usb_speed_t speed;      // Device speed
    uint8_t  address;           // Assigned device address
    uint8_t  max_packet_size;   // Max packet size for endpoint 0
    char     manufacturer[64];  // Manufacturer string
    char     product[64];       // Product string
    char     serial_number[32]; // Serial number string
} hal_usb_device_info_t;

// USB endpoint descriptor
typedef struct {
    uint8_t  address;           // Endpoint address (bits 0-3: number, bit 7: direction)
    uint8_t  attributes;        // Endpoint attributes (bits 0-1: transfer type)
    uint16_t max_packet_size;   // Maximum packet size
    uint8_t  interval;          // Interval for polling (frames)
} hal_usb_endpoint_desc_t;

// USB transfer result
typedef struct {
    int32_t  status;            // Status code (0 = success)
    uint32_t actual_length;     // Actual bytes transferred
} hal_usb_transfer_result_t;

// USB host controller capabilities
typedef struct {
    bool supports_low_speed;    // Supports low speed devices
    bool supports_full_speed;   // Supports full speed devices
    bool supports_high_speed;   // Supports high speed devices
    bool supports_super_speed;  // Supports super speed devices (USB 3.0)
    uint8_t max_ports;          // Maximum number of root hub ports
    uint16_t max_bandwidth;     // Maximum bandwidth available (in MBps)
} hal_usb_controller_caps_t;

// USB transfer callback
typedef void (*hal_usb_transfer_callback_t)(hal_usb_transfer_result_t* result, void* context);

/**
 * Initialize the USB subsystem and detect controllers
 * 
 * @return 0 on success, negative value on error
 */
int hal_usb_init(void);

/**
 * Shut down the USB subsystem
 */
void hal_usb_shutdown(void);

/**
 * Get USB host controller capabilities
 * 
 * @param controller_id Controller ID (0-based)
 * @param caps Pointer to capabilities structure to fill
 * @return 0 on success, negative value on error
 */
int hal_usb_get_controller_caps(uint8_t controller_id, hal_usb_controller_caps_t* caps);

/**
 * Enumerate devices connected to the USB
 * 
 * @param devices Array to store device information
 * @param max_devices Maximum number of devices to enumerate
 * @return Number of devices found, or negative value on error
 */
int hal_usb_enumerate_devices(hal_usb_device_info_t* devices, uint8_t max_devices);

/**
 * Reset a USB port
 * 
 * @param controller_id Controller ID (0-based)
 * @param port Port number (0-based)
 * @return 0 on success, negative value on error
 */
int hal_usb_reset_port(uint8_t controller_id, uint8_t port);

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
                             hal_usb_transfer_callback_t callback, void* context);

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
                          hal_usb_transfer_callback_t callback, void* context);

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
                               hal_usb_transfer_callback_t callback, void* context);

/**
 * Cancel a pending USB transfer
 * 
 * @param transfer_id Transfer ID returned from transfer function
 * @return 0 on success, negative value on error
 */
int hal_usb_cancel_transfer(int transfer_id);

/**
 * Get USB device descriptor
 * 
 * @param device_addr Device address
 * @param device_info Pointer to device info structure to fill
 * @return 0 on success, negative value on error
 */
int hal_usb_get_device_descriptor(uint8_t device_addr, hal_usb_device_info_t* device_info);

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
                                  char* buffer, uint16_t buffer_size);

#endif // HAL_USB_H