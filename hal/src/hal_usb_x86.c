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
    
    // Get the controller for this device
    uint8_t controller_id = devices[device_index].controller_id;
    usb_controller_t* controller = &controllers[controller_id];
    
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
    
    // Create setup packet - 8 bytes
    uint8_t setup_packet[8];
    setup_packet[0] = request_type;
    setup_packet[1] = request;
    setup_packet[2] = value & 0xFF;         // value low byte
    setup_packet[3] = (value >> 8) & 0xFF;  // value high byte
    setup_packet[4] = index & 0xFF;         // index low byte
    setup_packet[5] = (index >> 8) & 0xFF;  // index high byte
    setup_packet[6] = length & 0xFF;        // length low byte
    setup_packet[7] = (length >> 8) & 0xFF; // length high byte
    
    // Allocate DMA buffer if needed (for alignment requirements)
    void* dma_buffer = NULL;
    if (data && length > 0) {
        // Check if the buffer is already aligned
        if (((uintptr_t)data & 0xF) != 0) {
            // Allocate aligned DMA buffer
            dma_buffer = hal_memory_allocate(length, 16); // 16-byte alignment
            
            // Copy data to DMA buffer if this is an OUT transfer
            if (!(request_type & 0x80)) { // bit 7 = 0 means OUT
                memcpy(dma_buffer, data, length);
            }
        } else {
            // Buffer is already aligned
            dma_buffer = data;
        }
    }
    
    // Create a transfer descriptor based on controller type
    bool success = false;
    switch (controller->type) {
        case USB_CONTROLLER_UHCI: {
            // UHCI: Use transfer descriptors in linked list
            uhci_transfer_desc_t* td_setup = uhci_create_transfer_desc(device_addr, 0, setup_packet, 8);
            uhci_transfer_desc_t* td_data = NULL;
            uhci_transfer_desc_t* td_status = NULL;
            
            // Data stage (if any)
            if (length > 0) {
                bool is_in = (request_type & 0x80) != 0;
                td_data = uhci_create_transfer_desc(device_addr, 0, dma_buffer, length);
                td_data->toggle = 1; // DATA1 PID for data stage
                td_data->direction = is_in ? 1 : 0;
                
                // Link setup to data
                td_setup->next_td = (uint32_t)td_data;
            }
            
            // Status stage (always present)
            bool status_is_in = !(request_type & 0x80); // Opposite direction of data
            td_status = uhci_create_transfer_desc(device_addr, 0, NULL, 0);
            td_status->toggle = 1;  // DATA1 PID for status stage
            td_status->direction = status_is_in ? 1 : 0;
            
            // Link data to status (or setup to status if no data)
            if (td_data) {
                td_data->next_td = (uint32_t)td_status;
            } else {
                td_setup->next_td = (uint32_t)td_status;
            }
            
            // Submit the transfer descriptors to the controller's schedule
            success = uhci_submit_control_transfer(controller, td_setup, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_OHCI: {
            // OHCI: Use endpoint descriptors and transfer descriptors
            ohci_endpoint_desc_t* ed = ohci_get_endpoint_desc(controller, device_addr, 0);
            
            // Create transfer descriptors for setup, data (if any), and status
            ohci_transfer_desc_t* td_setup = ohci_create_transfer_desc(OHCI_SETUP_PACKET, setup_packet, 8);
            ohci_transfer_desc_t* td_data = NULL;
            ohci_transfer_desc_t* td_status = NULL;
            
            // Data stage (if any)
            if (length > 0) {
                uint32_t pid = (request_type & 0x80) ? OHCI_IN_PACKET : OHCI_OUT_PACKET;
                td_data = ohci_create_transfer_desc(pid, dma_buffer, length);
                
                // Link setup to data
                td_setup->next_td = (uint32_t)td_data;
            }
            
            // Status stage (always present, opposite direction of data)
            uint32_t status_pid = (request_type & 0x80) ? OHCI_OUT_PACKET : OHCI_IN_PACKET;
            td_status = ohci_create_transfer_desc(status_pid, NULL, 0);
            
            // Link data to status (or setup to status if no data)
            if (td_data) {
                td_data->next_td = (uint32_t)td_status;
            } else {
                td_setup->next_td = (uint32_t)td_status;
            }
            
            // Submit the transfer descriptors to the controller
            success = ohci_submit_control_transfer(controller, ed, td_setup, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_EHCI: {
            // EHCI: Use queue head and queue element transfer descriptors
            ehci_queue_head_t* qh = ehci_get_control_queue_head(controller, device_addr, 0);
            
            // Create transfer descriptors for setup, data (if any), and status
            ehci_qtd_t* qtd_setup = ehci_create_qtd(EHCI_PID_SETUP, setup_packet, 8);
            ehci_qtd_t* qtd_data = NULL;
            ehci_qtd_t* qtd_status = NULL;
            
            // Data stage (if any)
            if (length > 0) {
                uint8_t pid = (request_type & 0x80) ? EHCI_PID_IN : EHCI_PID_OUT;
                qtd_data = ehci_create_qtd(pid, dma_buffer, length);
                
                // Link setup to data
                qtd_setup->next_qtd = (uint32_t)qtd_data;
            }
            
            // Status stage (always present, opposite direction of data)
            uint8_t status_pid = (request_type & 0x80) ? EHCI_PID_OUT : EHCI_PID_IN;
            qtd_status = ehci_create_qtd(status_pid, NULL, 0);
            qtd_status->ioc = 1; // Generate interrupt on completion
            
            // Link data to status (or setup to status if no data)
            if (qtd_data) {
                qtd_data->next_qtd = (uint32_t)qtd_status;
            } else {
                qtd_setup->next_qtd = (uint32_t)qtd_status;
            }
            
            // Submit the transfer descriptors to the controller
            success = ehci_submit_control_transfer(controller, qh, qtd_setup, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_XHCI: {
            // XHCI: Use transfer rings and transfer request blocks
            xhci_slot_t* slot = xhci_get_device_slot(controller, device_addr);
            xhci_transfer_ring_t* ring = &slot->ep_rings[0]; // EP0 = control
            
            // Allocate a transfer request block
            xhci_trb_t* trb_setup = xhci_get_next_trb(ring);
            xhci_trb_t* trb_data = NULL;
            xhci_trb_t* trb_status = NULL;
            
            // Setup stage TRB
            memset(trb_setup, 0, sizeof(xhci_trb_t));
            memcpy(trb_setup->params, setup_packet, 8);
            trb_setup->transfer_length = 8;
            trb_setup->trb_type = XHCI_TRB_SETUP;
            trb_setup->transfer_type = (request_type & 0x80) ? 3 : 2; // 3=IN, 2=OUT
            
            // Data stage TRB (if any)
            if (length > 0) {
                trb_data = xhci_get_next_trb(ring);
                memset(trb_data, 0, sizeof(xhci_trb_t));
                trb_data->data_ptr = (uint64_t)dma_buffer;
                trb_data->transfer_length = length;
                trb_data->trb_type = XHCI_TRB_DATA;
                trb_data->direction = (request_type & 0x80) ? 1 : 0;
            }
            
            // Status stage TRB (always present)
            trb_status = xhci_get_next_trb(ring);
            memset(trb_status, 0, sizeof(xhci_trb_t));
            trb_status->trb_type = XHCI_TRB_STATUS;
            trb_status->ioc = 1; // Interrupt on completion
            trb_status->direction = (request_type & 0x80) ? 0 : 1; // Opposite of data
            
            // Submit the transfer descriptors to the controller
            success = xhci_submit_control_transfer(controller, ring, transfer_id);
            
            break;
        }
        
        default:
            log_error("USB", "Unsupported controller type for control transfer");
            success = false;
    }
    
    // Handle error case
    if (!success) {
        if (dma_buffer != data && dma_buffer != NULL) {
            hal_memory_free(dma_buffer);
        }
        transfers[transfer_id].in_use = false;
        return -1;
    }
    
    // If this is a synchronous transfer, wait for completion
    if (callback == NULL) {
        // Wait for transfer to complete
        while (transfers[transfer_id].status == -1) {
            // Yield CPU time (might sleep or spin depending on OS)
            hal_yield_cpu();
        }
        
        // Copy data from DMA buffer to user buffer if this was an IN transfer
        if ((request_type & 0x80) && dma_buffer != data && dma_buffer != NULL) {
            memcpy(data, dma_buffer, transfers[transfer_id].actual_length);
        }
        
        // Free DMA buffer if we allocated one
        if (dma_buffer != data && dma_buffer != NULL) {
            hal_memory_free(dma_buffer);
        }
        
        // Get status and free transfer slot
        int status = transfers[transfer_id].status;
        uint32_t actual_length = transfers[transfer_id].actual_length;
        transfers[transfer_id].in_use = false;
        
        // Return status code
        return status == 0 ? actual_length : status;
    }
    
    // For asynchronous transfers, the callback will handle completion
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
    if (!usb_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Get the controller for this device
    uint8_t controller_id = devices[device_index].controller_id;
    usb_controller_t* controller = &controllers[controller_id];
    
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
    
    // Check endpoint direction
    bool is_in = (endpoint & 0x80) != 0; // Bit 7 set = IN endpoint
    uint8_t ep_num = endpoint & 0x7F;    // Lower 7 bits = endpoint number
    
    // Allocate DMA buffer if needed (for alignment requirements)
    void* dma_buffer = NULL;
    if (data && length > 0) {
        // Check if the buffer is already aligned
        if (((uintptr_t)data & 0xF) != 0) {
            // Allocate aligned DMA buffer
            dma_buffer = hal_memory_allocate(length, 16); // 16-byte alignment
            
            // Copy data to DMA buffer if this is an OUT transfer
            if (!is_in) { // OUT transfer
                memcpy(dma_buffer, data, length);
            }
        } else {
            // Buffer is already aligned
            dma_buffer = data;
        }
    }
    
    // Create a transfer descriptor based on controller type
    bool success = false;
    
    switch (controller->type) {
        case USB_CONTROLLER_UHCI: {
            // UHCI controller doesn't support bulk transfers for high-bandwidth devices
            // It uses a linked list of transfer descriptors
            
            // Get the maximum packet size for this endpoint
            uint16_t max_packet_size = get_endpoint_max_packet_size(device_addr, ep_num);
            if (max_packet_size == 0) {
                max_packet_size = 64; // Default to 64 bytes if not known
            }
            
            // Calculate number of transfer descriptors needed
            uint32_t num_tds = (length + max_packet_size - 1) / max_packet_size;
            if (num_tds == 0) {
                num_tds = 1; // At least one TD even for zero-length packets
            }
            
            // Create linked list of transfer descriptors
            uhci_transfer_desc_t* first_td = NULL;
            uhci_transfer_desc_t* current_td = NULL;
            uhci_transfer_desc_t* prev_td = NULL;
            
            for (uint32_t i = 0; i < num_tds; i++) {
                // Calculate size of this packet
                uint32_t offset = i * max_packet_size;
                uint32_t packet_size = length - offset;
                if (packet_size > max_packet_size) {
                    packet_size = max_packet_size;
                }
                
                // Create transfer descriptor
                current_td = uhci_create_transfer_desc(device_addr, ep_num, 
                                                     (uint8_t*)dma_buffer + offset,
                                                     packet_size);
                current_td->direction = is_in ? 1 : 0;
                
                // Set IOC (Interrupt On Completion) for the last TD
                if (i == num_tds - 1) {
                    current_td->ioc = 1;
                }
                
                // Link to previous TD
                if (prev_td) {
                    prev_td->next_td = (uint32_t)current_td;
                } else {
                    first_td = current_td;
                }
                
                prev_td = current_td;
            }
            
            // Submit the transfer descriptors to the controller
            success = uhci_submit_bulk_transfer(controller, first_td, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_OHCI: {
            // OHCI controller uses endpoint descriptors and transfer descriptors
            // Get the endpoint descriptor for this endpoint
            ohci_endpoint_desc_t* ed = ohci_get_endpoint_desc(controller, device_addr, ep_num);
            
            // Get the maximum packet size for this endpoint
            uint16_t max_packet_size = get_endpoint_max_packet_size(device_addr, ep_num);
            if (max_packet_size == 0) {
                max_packet_size = 64; // Default to 64 bytes if not known
            }
            
            // Calculate number of transfer descriptors needed
            uint32_t num_tds = (length + max_packet_size - 1) / max_packet_size;
            if (num_tds == 0) {
                num_tds = 1; // At least one TD even for zero-length packets
            }
            
            // Create linked list of transfer descriptors
            ohci_transfer_desc_t* first_td = NULL;
            ohci_transfer_desc_t* current_td = NULL;
            ohci_transfer_desc_t* prev_td = NULL;
            
            for (uint32_t i = 0; i < num_tds; i++) {
                // Calculate size of this packet
                uint32_t offset = i * max_packet_size;
                uint32_t packet_size = length - offset;
                if (packet_size > max_packet_size) {
                    packet_size = max_packet_size;
                }
                
                // Create transfer descriptor
                uint32_t pid = is_in ? OHCI_IN_PACKET : OHCI_OUT_PACKET;
                current_td = ohci_create_transfer_desc(pid, 
                                                     (uint8_t*)dma_buffer + offset,
                                                     packet_size);
                
                // Set IOC (Interrupt On Completion) for the last TD
                if (i == num_tds - 1) {
                    current_td->ioc = 1;
                }
                
                // Link to previous TD
                if (prev_td) {
                    prev_td->next_td = (uint32_t)current_td;
                } else {
                    first_td = current_td;
                }
                
                prev_td = current_td;
            }
            
            // Submit the transfer descriptors to the controller
            success = ohci_submit_bulk_transfer(controller, ed, first_td, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_EHCI: {
            // EHCI controller uses queue heads and queue element transfer descriptors
            
            // Get the queue head for this endpoint
            ehci_queue_head_t* qh = ehci_get_bulk_queue_head(controller, device_addr, ep_num);
            
            // Get the maximum packet size for this endpoint
            uint16_t max_packet_size = get_endpoint_max_packet_size(device_addr, ep_num);
            if (max_packet_size == 0) {
                max_packet_size = 512; // Default to 512 bytes for high-speed if not known
            }
            
            // Create queue element transfer descriptors
            ehci_qtd_t* first_qtd = NULL;
            ehci_qtd_t* current_qtd = NULL;
            ehci_qtd_t* prev_qtd = NULL;
            
            // Calculate number of QTDs needed (EHCI has 5 pages per QTD = 20KB max)
            // We'll use a simpler model here with one QTD per packet
            uint32_t num_qtds = (length + max_packet_size - 1) / max_packet_size;
            if (num_qtds == 0) {
                num_qtds = 1; // At least one QTD even for zero-length packets
            }
            
            for (uint32_t i = 0; i < num_qtds; i++) {
                // Calculate size of this packet
                uint32_t offset = i * max_packet_size;
                uint32_t packet_size = length - offset;
                if (packet_size > max_packet_size) {
                    packet_size = max_packet_size;
                }
                
                // Create QTD
                uint8_t pid = is_in ? EHCI_PID_IN : EHCI_PID_OUT;
                current_qtd = ehci_create_qtd(pid, 
                                            (uint8_t*)dma_buffer + offset,
                                            packet_size);
                
                // Set IOC (Interrupt On Completion) for the last QTD
                if (i == num_qtds - 1) {
                    current_qtd->ioc = 1;
                }
                
                // Link to previous QTD
                if (prev_qtd) {
                    prev_qtd->next_qtd = (uint32_t)current_qtd;
                } else {
                    first_qtd = current_qtd;
                }
                
                prev_qtd = current_qtd;
            }
            
            // Submit the QTDs to the controller
            success = ehci_submit_bulk_transfer(controller, qh, first_qtd, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_XHCI: {
            // XHCI controller uses transfer rings
            
            // Get the transfer ring for this endpoint
            xhci_slot_t* slot = xhci_get_device_slot(controller, device_addr);
            uint8_t ep_index = ep_num * 2 + (is_in ? 1 : 0); // Convert EP address to index
            xhci_transfer_ring_t* ring = &slot->ep_rings[ep_index];
            
            // Get the maximum packet size for this endpoint
            uint16_t max_packet_size = get_endpoint_max_packet_size(device_addr, ep_num);
            if (max_packet_size == 0) {
                max_packet_size = 1024; // Default to 1024 bytes for SuperSpeed if not known
            }
            
            // Calculate number of TRBs needed
            uint32_t num_trbs = (length + max_packet_size - 1) / max_packet_size;
            if (num_trbs == 0) {
                num_trbs = 1; // At least one TRB even for zero-length packets
            }
            
            // Create transfer request blocks
            for (uint32_t i = 0; i < num_trbs; i++) {
                // Calculate size of this packet
                uint32_t offset = i * max_packet_size;
                uint32_t packet_size = length - offset;
                if (packet_size > max_packet_size) {
                    packet_size = max_packet_size;
                }
                
                // Get next TRB in the ring
                xhci_trb_t* trb = xhci_get_next_trb(ring);
                
                // Initialize the TRB
                memset(trb, 0, sizeof(xhci_trb_t));
                trb->data_ptr = (uint64_t)((uint8_t*)dma_buffer + offset);
                trb->transfer_length = packet_size;
                trb->trb_type = XHCI_TRB_NORMAL;
                
                // Set IOC (Interrupt On Completion) for the last TRB
                if (i == num_trbs - 1) {
                    trb->ioc = 1;
                }
            }
            
            // Submit the transfer to the controller
            success = xhci_submit_bulk_transfer(controller, ring, transfer_id);
            
            break;
        }
        
        default:
            log_error("USB", "Unsupported controller type for bulk transfer");
            success = false;
    }
    
    // Handle error case
    if (!success) {
        if (dma_buffer != data && dma_buffer != NULL) {
            hal_memory_free(dma_buffer);
        }
        transfers[transfer_id].in_use = false;
        return -1;
    }
    
    // If this is a synchronous transfer, wait for completion
    if (callback == NULL) {
        // Wait for transfer to complete
        while (transfers[transfer_id].status == -1) {
            // Yield CPU time (might sleep or spin depending on OS)
            hal_yield_cpu();
        }
        
        // Copy data from DMA buffer to user buffer if this was an IN transfer
        if (is_in && dma_buffer != data && dma_buffer != NULL) {
            memcpy(data, dma_buffer, transfers[transfer_id].actual_length);
        }
        
        // Free DMA buffer if we allocated one
        if (dma_buffer != data && dma_buffer != NULL) {
            hal_memory_free(dma_buffer);
        }
        
        // Get status and free transfer slot
        int status = transfers[transfer_id].status;
        uint32_t actual_length = transfers[transfer_id].actual_length;
        transfers[transfer_id].in_use = false;
        
        // Return status code
        return status == 0 ? actual_length : status;
    }
    
    // For asynchronous transfers, the callback will handle completion
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
    if (!usb_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_get_device_by_address(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Get the controller for this device
    uint8_t controller_id = devices[device_index].controller_id;
    usb_controller_t* controller = &controllers[controller_id];
    
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
    
    // Check endpoint direction
    bool is_in = (endpoint & 0x80) != 0; // Bit 7 set = IN endpoint
    uint8_t ep_num = endpoint & 0x7F;    // Lower 7 bits = endpoint number
    
    // Allocate DMA buffer if needed (for alignment requirements)
    void* dma_buffer = NULL;
    if (data && length > 0) {
        // Check if the buffer is already aligned
        if (((uintptr_t)data & 0xF) != 0) {
            // Allocate aligned DMA buffer
            dma_buffer = hal_memory_allocate(length, 16); // 16-byte alignment
            
            // Copy data to DMA buffer if this is an OUT transfer
            if (!is_in) { // OUT transfer
                memcpy(dma_buffer, data, length);
            }
        } else {
            // Buffer is already aligned
            dma_buffer = data;
        }
    }
    
    // Create a transfer descriptor based on controller type
    bool success = false;
    
    switch (controller->type) {
        case USB_CONTROLLER_UHCI: {
            // UHCI: Uses transfer descriptors in the periodic frame list
            
            // Get the maximum packet size for this endpoint
            uint16_t max_packet_size = get_endpoint_max_packet_size(device_addr, ep_num);
            if (max_packet_size == 0) {
                max_packet_size = 8; // Default for interrupt if not known
            }
            
            // Get the polling interval for this endpoint
            uint8_t interval = get_endpoint_interval(device_addr, ep_num);
            if (interval == 0) {
                interval = 10; // Default to 10ms if not known
            }
            
            // Create transfer descriptor
            uhci_transfer_desc_t* td = uhci_create_transfer_desc(
                device_addr, ep_num, dma_buffer, length);
            td->direction = is_in ? 1 : 0;
            td->ioc = 1; // Interrupt on completion
            
            // Schedule the transfer descriptor in the periodic frame list
            success = uhci_schedule_interrupt_transfer(controller, td, interval, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_OHCI: {
            // OHCI: Uses endpoint descriptors in the periodic schedule
            
            // Get the endpoint descriptor for this endpoint
            ohci_endpoint_desc_t* ed = ohci_get_endpoint_desc(controller, device_addr, ep_num);
            
            // Get the polling interval for this endpoint
            uint8_t interval = get_endpoint_interval(device_addr, ep_num);
            if (interval == 0) {
                interval = 10; // Default to 10ms if not known
            }
            
            // Create transfer descriptor
            uint32_t pid = is_in ? OHCI_IN_PACKET : OHCI_OUT_PACKET;
            ohci_transfer_desc_t* td = ohci_create_transfer_desc(pid, dma_buffer, length);
            td->ioc = 1; // Interrupt on completion
            
            // Schedule the transfer descriptor in the periodic schedule
            success = ohci_schedule_interrupt_transfer(controller, ed, td, interval, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_EHCI: {
            // EHCI: Uses queue heads in the periodic schedule
            
            // Get the queue head for this endpoint
            ehci_queue_head_t* qh = ehci_get_interrupt_queue_head(controller, device_addr, ep_num);
            
            // Get the polling interval for this endpoint
            uint8_t interval = get_endpoint_interval(device_addr, ep_num);
            if (interval == 0) {
                interval = 1; // Default interval in milliseconds
            }
            
            // Calculate interval in microframes (EHCI uses microframes = 125µs)
            uint8_t microframe_interval = interval * 8;
            
            // Create queue element transfer descriptor
            uint8_t pid = is_in ? EHCI_PID_IN : EHCI_PID_OUT;
            ehci_qtd_t* qtd = ehci_create_qtd(pid, dma_buffer, length);
            qtd->ioc = 1; // Interrupt on completion
            
            // Schedule the QTD in the periodic schedule
            success = ehci_schedule_interrupt_transfer(controller, qh, qtd, microframe_interval, transfer_id);
            
            break;
        }
        
        case USB_CONTROLLER_XHCI: {
            // XHCI: Uses transfer rings with special parameters
            
            // Get the transfer ring for this endpoint
            xhci_slot_t* slot = xhci_get_device_slot(controller, device_addr);
            uint8_t ep_index = ep_num * 2 + (is_in ? 1 : 0); // Convert EP address to index
            xhci_transfer_ring_t* ring = &slot->ep_rings[ep_index];
            
            // Get the maximum packet size for this endpoint
            uint16_t max_packet_size = get_endpoint_max_packet_size(device_addr, ep_num);
            if (max_packet_size == 0) {
                max_packet_size = 64; // Default if not known
            }
            
            // Create a single transfer request block
            xhci_trb_t* trb = xhci_get_next_trb(ring);
            
            // Initialize the TRB
            memset(trb, 0, sizeof(xhci_trb_t));
            trb->data_ptr = (uint64_t)dma_buffer;
            trb->transfer_length = length;
            trb->trb_type = XHCI_TRB_NORMAL;
            trb->ioc = 1; // Interrupt on completion
            
            // Submit the transfer to the controller
            success = xhci_submit_interrupt_transfer(controller, ring, transfer_id);
            
            break;
        }
        
        default:
            log_error("USB", "Unsupported controller type for interrupt transfer");
            success = false;
    }
    
    // Handle error case
    if (!success) {
        if (dma_buffer != data && dma_buffer != NULL) {
            hal_memory_free(dma_buffer);
        }
        transfers[transfer_id].in_use = false;
        return -1;
    }
    
    // If this is a synchronous transfer, wait for completion
    if (callback == NULL) {
        // Wait for transfer to complete
        while (transfers[transfer_id].status == -1) {
            // Yield CPU time (might sleep or spin depending on OS)
            hal_yield_cpu();
        }
        
        // Copy data from DMA buffer to user buffer if this was an IN transfer
        if (is_in && dma_buffer != data && dma_buffer != NULL) {
            memcpy(data, dma_buffer, transfers[transfer_id].actual_length);
        }
        
        // Free DMA buffer if we allocated one
        if (dma_buffer != data && dma_buffer != NULL) {
            hal_memory_free(dma_buffer);
        }
        
        // Get status and free transfer slot
        int status = transfers[transfer_id].status;
        uint32_t actual_length = transfers[transfer_id].actual_length;
        transfers[transfer_id].in_use = false;
        
        // Return status code
        return status == 0 ? actual_length : status;
    }
    
    // For asynchronous transfers, the callback will handle completion
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
    
    // Get the device and controller for this transfer
    int device_index = usb_get_device_by_address(transfers[transfer_id].device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    uint8_t controller_id = devices[device_index].controller_id;
    usb_controller_t* controller = &controllers[controller_id];
    
    // Attempt to cancel the transfer based on controller type
    bool cancelled = false;
    
    switch (controller->type) {
        case USB_CONTROLLER_UHCI:
            // For UHCI, we need to remove the transfer descriptor(s) from the schedule
            // and mark it as inactive
            if (transfers[transfer_id].type == HAL_USB_TRANSFER_CONTROL) {
                cancelled = uhci_cancel_control_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_BULK) {
                cancelled = uhci_cancel_bulk_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_INTERRUPT) {
                cancelled = uhci_cancel_interrupt_transfer(controller, transfer_id);
            } else {
                // Unsupported transfer type
                cancelled = false;
            }
            break;
            
        case USB_CONTROLLER_OHCI:
            // For OHCI, we need to remove the transfer descriptor from the endpoint's chain
            // and add it to the done queue
            if (transfers[transfer_id].type == HAL_USB_TRANSFER_CONTROL) {
                cancelled = ohci_cancel_control_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_BULK) {
                cancelled = ohci_cancel_bulk_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_INTERRUPT) {
                cancelled = ohci_cancel_interrupt_transfer(controller, transfer_id);
            } else {
                // Unsupported transfer type
                cancelled = false;
            }
            break;
            
        case USB_CONTROLLER_EHCI:
            // For EHCI, we need to set the Active bit to 0 in the qTD and wait for the
            // controller to process the inactive qTD
            if (transfers[transfer_id].type == HAL_USB_TRANSFER_CONTROL) {
                cancelled = ehci_cancel_control_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_BULK) {
                cancelled = ehci_cancel_bulk_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_INTERRUPT) {
                cancelled = ehci_cancel_interrupt_transfer(controller, transfer_id);
            } else {
                // Unsupported transfer type
                cancelled = false;
            }
            break;
            
        case USB_CONTROLLER_XHCI:
            // For XHCI, we need to send a Stop Endpoint Command and wait for the command completion
            if (transfers[transfer_id].type == HAL_USB_TRANSFER_CONTROL) {
                cancelled = xhci_cancel_control_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_BULK) {
                cancelled = xhci_cancel_bulk_transfer(controller, transfer_id);
            } else if (transfers[transfer_id].type == HAL_USB_TRANSFER_INTERRUPT) {
                cancelled = xhci_cancel_interrupt_transfer(controller, transfer_id);
            } else {
                // Unsupported transfer type
                cancelled = false;
            }
            break;
            
        default:
            log_error("USB", "Unsupported controller type for cancelling transfer");
            cancelled = false;
    }
    
    // If we couldn't cancel the transfer through the controller, we can still
    // mark it as cancelled in our transfer table
    if (!cancelled) {
        log_warning("USB", "Failed to cancel transfer %d through controller, marking as cancelled", transfer_id);
    }
    
    // Mark the transfer as completed with cancelled status
    transfers[transfer_id].status = -2; // Cancelled
    transfers[transfer_id].actual_length = 0;
    
    // Call the callback if provided
    if (transfers[transfer_id].callback) {
        hal_usb_transfer_result_t result = {
            .status = transfers[transfer_id].status,
            .actual_length = transfers[transfer_id].actual_length
        };
        transfers[transfer_id].callback(&result, transfers[transfer_id].context);
    }
    
    // Free any DMA buffer if allocated
    // This would only be relevant if the transfer uses an internal DMA buffer different from
    // the user-provided buffer, and we'd need to store that information in the transfer structure
    
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