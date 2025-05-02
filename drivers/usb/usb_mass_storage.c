/**
 * @file usb_mass_storage.c
 * @brief USB Mass Storage Driver implementation
 * 
 * Implements the USB Mass Storage protocol (Bulk-Only Transport)
 * to support USB flash drives and other storage devices.
 */

#include "usb_mass_storage.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include "../../filesystem/vfs/vfs.h"
#include <string.h>

// Maximum number of USB Mass Storage devices we can handle
#define MAX_USB_STORAGE_DEVICES 8

// Internal device tracking
static usb_mass_storage_device_t storage_devices[MAX_USB_STORAGE_DEVICES];
static int num_storage_devices = 0;
static bool driver_initialized = false;

// Command tag counter (incremented for each command)
static uint32_t current_tag = 1;

// Forward declarations for internal functions
static int send_mass_storage_command(uint8_t device_addr, uint8_t lun, uint8_t* cmd, 
                                     uint8_t cmd_len, uint8_t dir_in, void* data, 
                                     uint32_t data_len);
static int get_max_lun(uint8_t device_addr, uint8_t* max_lun);
static int reset_device(uint8_t device_addr);
static int perform_inquiry(uint8_t device_addr, uint8_t lun, 
                           usb_mass_storage_device_t* device_info);
static int usb_storage_get_device_by_addr(uint8_t device_addr);

/**
 * Initialize the USB Mass Storage driver
 * 
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_init(void) {
    if (driver_initialized) {
        return 0; // Already initialized
    }
    
    log_info("USBMS", "Initializing USB Mass Storage driver");
    
    // Initialize our device array
    memset(storage_devices, 0, sizeof(storage_devices));
    num_storage_devices = 0;
    
    driver_initialized = true;
    log_info("USBMS", "USB Mass Storage driver initialized");
    
    return 0;
}

/**
 * Shut down the USB Mass Storage driver
 */
void usb_mass_storage_shutdown(void) {
    if (!driver_initialized) {
        return;
    }
    
    log_info("USBMS", "Shutting down USB Mass Storage driver");
    
    // Unmount all devices
    for (int i = 0; i < num_storage_devices; i++) {
        if (storage_devices[i].mounted) {
            usb_mass_storage_unmount(storage_devices[i].device_addr);
        }
    }
    
    driver_initialized = false;
    log_info("USBMS", "USB Mass Storage driver shut down");
}

/**
 * Detect and initialize USB Mass Storage devices
 * 
 * @return Number of devices found, negative value on error
 */
int usb_mass_storage_detect_devices(void) {
    if (!driver_initialized) {
        return -1;
    }
    
    log_info("USBMS", "Scanning for USB Mass Storage devices");
    
    // Reset our device count
    num_storage_devices = 0;
    
    // Get the list of all USB devices
    hal_usb_device_info_t usb_devices[16];
    int num_devices = hal_usb_enumerate_devices(usb_devices, 16);
    
    if (num_devices <= 0) {
        log_info("USBMS", "No USB devices found");
        return 0;
    }
    
    log_info("USBMS", "Found %d USB devices, checking for Mass Storage class", num_devices);
    
    // Loop through devices to find Mass Storage ones
    for (int i = 0; i < num_devices && num_storage_devices < MAX_USB_STORAGE_DEVICES; i++) {
        // Check if this is a Mass Storage device
        if (usb_devices[i].device_class == USB_CLASS_MASS_STORAGE || 
            (usb_devices[i].device_class == 0 && 
             // Interface class would be checked here in full implementation
             1)) {
            
            log_info("USBMS", "Found Mass Storage device at address %d: %s %s", 
                     usb_devices[i].address, 
                     usb_devices[i].manufacturer, 
                     usb_devices[i].product);
            
            // Initialize a storage device structure
            usb_mass_storage_device_t* device = &storage_devices[num_storage_devices];
            device->device_addr = usb_devices[i].address;
            
            // Get the max LUN (Logical Unit Number)
            if (get_max_lun(device->device_addr, &device->max_lun) < 0) {
                log_warning("USBMS", "Failed to get max LUN for device %d", device->device_addr);
                device->max_lun = 0; // Assume single LUN
            }
            
            // Device setup: in a real implementation we would:
            // 1. Find the interface number for Mass Storage
            // 2. Find the bulk IN and OUT endpoints
            // 3. Get device capacity
            // For this simulation:
            device->interface_num = 0;
            device->bulk_in_ep = 0x81;  // Typical values, would be detected
            device->bulk_out_ep = 0x02; // in actual implementation
            
            // Get device details via INQUIRY command
            if (perform_inquiry(device->device_addr, 0, device) < 0) {
                log_warning("USBMS", "Failed to query device %d", device->device_addr);
                continue;
            }
            
            // Get capacity information
            if (usb_mass_storage_get_capacity(device->device_addr, &device->block_size, 
                                            &device->num_blocks) < 0) {
                log_warning("USBMS", "Failed to get capacity for device %d", device->device_addr);
                device->block_size = 512;  // Assume default
                device->num_blocks = 0;
            }
            
            log_info("USBMS", "Device %d: %s %s - %d blocks of %d bytes", 
                     device->device_addr, device->vendor, device->product, 
                     device->num_blocks, device->block_size);
            
            device->mounted = false;
            device->vfs_handle = -1;
            
            num_storage_devices++;
        }
    }
    
    log_info("USBMS", "Found %d USB Mass Storage devices", num_storage_devices);
    return num_storage_devices;
}

/**
 * Get information about USB Mass Storage devices
 * 
 * @param devices Array to store device information
 * @param max_devices Maximum number of devices to return
 * @return Number of devices found, negative value on error
 */
int usb_mass_storage_get_devices(usb_mass_storage_device_t* devices, int max_devices) {
    if (!driver_initialized || !devices || max_devices <= 0) {
        return -1;
    }
    
    // Copy device information
    int count = (num_storage_devices < max_devices) ? num_storage_devices : max_devices;
    for (int i = 0; i < count; i++) {
        memcpy(&devices[i], &storage_devices[i], sizeof(usb_mass_storage_device_t));
    }
    
    return count;
}

/**
 * Get capacity information from a Mass Storage device
 * 
 * @param device_addr Device address
 * @param block_size Pointer to store block size
 * @param num_blocks Pointer to store number of blocks
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_get_capacity(uint8_t device_addr, uint32_t* block_size, uint32_t* num_blocks) {
    if (!driver_initialized || !block_size || !num_blocks) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    uint8_t cmd[10] = {0};
    cmd[0] = SCSI_CMD_READ_CAPACITY;
    
    // Response buffer for READ CAPACITY command (8 bytes)
    uint8_t response[8];
    
    int result = send_mass_storage_command(device_addr, 0, cmd, 10, USB_MSC_DIR_IN, 
                                          response, sizeof(response));
    if (result < 0) {
        return result;
    }
    
    // Parse response (big-endian format)
    *num_blocks = ((uint32_t)response[0] << 24) | ((uint32_t)response[1] << 16) | 
                 ((uint32_t)response[2] << 8) | response[3];
    *block_size = ((uint32_t)response[4] << 24) | ((uint32_t)response[5] << 16) | 
                 ((uint32_t)response[6] << 8) | response[7];
    
    // Adjust number of blocks (READ CAPACITY returns the last LBA)
    *num_blocks += 1;
    
    return 0;
}

/**
 * Read blocks from a Mass Storage device
 * 
 * @param device_addr Device address
 * @param lun Logical unit number
 * @param block_addr Starting block address
 * @param buffer Buffer to store data
 * @param num_blocks Number of blocks to read
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_read_blocks(uint8_t device_addr, uint8_t lun, uint32_t block_addr, 
                                void* buffer, uint32_t num_blocks) {
    if (!driver_initialized || !buffer || num_blocks == 0) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Calculate data length
    uint32_t block_size = storage_devices[device_index].block_size;
    uint32_t data_len = num_blocks * block_size;
    
    // Set up READ(10) command
    uint8_t cmd[10] = {0};
    cmd[0] = SCSI_CMD_READ_10;
    
    // Block address (big-endian)
    cmd[2] = (block_addr >> 24) & 0xFF;
    cmd[3] = (block_addr >> 16) & 0xFF;
    cmd[4] = (block_addr >> 8) & 0xFF;
    cmd[5] = block_addr & 0xFF;
    
    // Transfer length in blocks (big-endian)
    cmd[7] = (num_blocks >> 8) & 0xFF;
    cmd[8] = num_blocks & 0xFF;
    
    int result = send_mass_storage_command(device_addr, lun, cmd, 10, USB_MSC_DIR_IN, 
                                          buffer, data_len);
    return result;
}

/**
 * Write blocks to a Mass Storage device
 * 
 * @param device_addr Device address
 * @param lun Logical unit number
 * @param block_addr Starting block address
 * @param buffer Buffer containing data to write
 * @param num_blocks Number of blocks to write
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_write_blocks(uint8_t device_addr, uint8_t lun, uint32_t block_addr, 
                                const void* buffer, uint32_t num_blocks) {
    if (!driver_initialized || !buffer || num_blocks == 0) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Calculate data length
    uint32_t block_size = storage_devices[device_index].block_size;
    uint32_t data_len = num_blocks * block_size;
    
    // Set up WRITE(10) command
    uint8_t cmd[10] = {0};
    cmd[0] = SCSI_CMD_WRITE_10;
    
    // Block address (big-endian)
    cmd[2] = (block_addr >> 24) & 0xFF;
    cmd[3] = (block_addr >> 16) & 0xFF;
    cmd[4] = (block_addr >> 8) & 0xFF;
    cmd[5] = block_addr & 0xFF;
    
    // Transfer length in blocks (big-endian)
    cmd[7] = (num_blocks >> 8) & 0xFF;
    cmd[8] = num_blocks & 0xFF;
    
    int result = send_mass_storage_command(device_addr, lun, cmd, 10, USB_MSC_DIR_OUT, 
                                          (void*)buffer, data_len);
    return result;
}

/**
 * Test if a USB Mass Storage device is ready
 * 
 * @param device_addr Device address
 * @param lun Logical unit number
 * @return 1 if ready, 0 if not ready, negative value on error
 */
int usb_mass_storage_test_unit_ready(uint8_t device_addr, uint8_t lun) {
    if (!driver_initialized) {
        return -1;
    }
    
    // Set up TEST UNIT READY command
    uint8_t cmd[6] = {0};
    cmd[0] = SCSI_CMD_TEST_UNIT_READY;
    
    int result = send_mass_storage_command(device_addr, lun, cmd, 6, USB_MSC_DIR_OUT, NULL, 0);
    
    // Zero or positive result means success (ready)
    return (result >= 0) ? 1 : 0;
}

/**
 * Mount a USB Mass Storage device as a filesystem
 * 
 * @param device_addr Device address
 * @param mount_point Mount point path
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_mount(uint8_t device_addr, const char* mount_point) {
    if (!driver_initialized || !mount_point) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // If already mounted, unmount first
    if (storage_devices[device_index].mounted) {
        usb_mass_storage_unmount(device_addr);
    }
    
    // Create a block device name for this USB device
    char block_device[16];
    snprintf(block_device, sizeof(block_device), "usb%d", device_addr);
    
    // In a real implementation, we would:
    // 1. Register the device with the block device layer
    // 2. Try to detect the filesystem type
    // 3. Mount the filesystem through VFS
    
    // For this simulation, we'll assume the filesystem is detected as FAT32
    const char* fs_type = "fat32";
    
    // Try to mount
    int result = vfs_mount(fs_type, block_device, mount_point, 0);
    if (result == VFS_SUCCESS) {
        storage_devices[device_index].mounted = true;
        storage_devices[device_index].vfs_handle = 0; // This would be a real handle
        
        log_info("USBMS", "Mounted USB device %d on %s as %s", device_addr, mount_point, fs_type);
        return 0;
    } else {
        log_error("USBMS", "Failed to mount USB device %d on %s: error %d", 
                 device_addr, mount_point, result);
        return -1;
    }
}

/**
 * Unmount a USB Mass Storage device
 * 
 * @param device_addr Device address
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_unmount(uint8_t device_addr) {
    if (!driver_initialized) {
        return -1;
    }
    
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Check if mounted
    if (!storage_devices[device_index].mounted) {
        return 0; // Already unmounted
    }
    
    // In a real implementation, we would:
    // 1. Unmount the filesystem through VFS
    // 2. Unregister from the block device layer
    
    // For this simulation, assume success
    storage_devices[device_index].mounted = false;
    storage_devices[device_index].vfs_handle = -1;
    
    log_info("USBMS", "Unmounted USB device %d", device_addr);
    return 0;
}

// ----------------- Internal helper functions -----------------

/**
 * Send a mass storage command to a device
 * 
 * @param device_addr Device address
 * @param lun Logical unit number
 * @param cmd Command buffer
 * @param cmd_len Command length
 * @param dir_in Direction flag (USB_MSC_DIR_IN or USB_MSC_DIR_OUT)
 * @param data Data buffer (NULL if none)
 * @param data_len Data length
 * @return 0 on success, negative value on error
 */
static int send_mass_storage_command(uint8_t device_addr, uint8_t lun, uint8_t* cmd, 
                                   uint8_t cmd_len, uint8_t dir_in, void* data, 
                                   uint32_t data_len) {
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Prepare Command Block Wrapper (CBW)
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    
    cbw.signature = USB_MSC_CBW_SIGNATURE;
    cbw.tag = current_tag++;
    cbw.data_transfer_length = data_len;
    cbw.flags = dir_in;
    cbw.lun = lun;
    cbw.cb_length = cmd_len;
    memcpy(cbw.command_block, cmd, cmd_len);
    
    // Send CBW to the device
    int result = hal_usb_bulk_transfer(device_addr, 
                                      storage_devices[device_index].bulk_out_ep,
                                      &cbw, sizeof(cbw), NULL, NULL);
    if (result < 0) {
        return -1;
    }
    
    // Transfer data if there is any
    if (data_len > 0 && data != NULL) {
        if (dir_in) {
            // Device to host (IN)
            result = hal_usb_bulk_transfer(device_addr, 
                                          storage_devices[device_index].bulk_in_ep,
                                          data, data_len, NULL, NULL);
        } else {
            // Host to device (OUT)
            result = hal_usb_bulk_transfer(device_addr, 
                                          storage_devices[device_index].bulk_out_ep,
                                          data, data_len, NULL, NULL);
        }
        
        if (result < 0) {
            return -1;
        }
    }
    
    // Read Command Status Wrapper (CSW)
    usb_msc_csw_t csw;
    result = hal_usb_bulk_transfer(device_addr, 
                                  storage_devices[device_index].bulk_in_ep,
                                  &csw, sizeof(csw), NULL, NULL);
    if (result < 0) {
        return -1;
    }
    
    // Check CSW validity
    if (csw.signature != USB_MSC_CSW_SIGNATURE) {
        return -1;
    }
    
    // Check if the command succeeded
    if (csw.status != USB_MSC_STATUS_PASSED) {
        return -1;
    }
    
    return 0;
}

/**
 * Get the maximum LUN (Logical Unit Number) from the device
 * 
 * @param device_addr Device address
 * @param max_lun Pointer to store the maximum LUN
 * @return 0 on success, negative value on error
 */
static int get_max_lun(uint8_t device_addr, uint8_t* max_lun) {
    if (!max_lun) {
        return -1;
    }
    
    // SET_INTERFACE 0 to ensure we're in the default state
    int result = hal_usb_control_transfer(device_addr,
                                         0x01, // bmRequestType: Host to device, Class, Interface
                                         0x0B, // bRequest: SET_INTERFACE
                                         0,    // wValue: Alternate setting
                                         0,    // wIndex: Interface number
                                         NULL, 0, NULL, NULL);
                                         
    if (result < 0) {
        // Some devices don't support this, so we'll continue anyway
        log_warning("USBMS", "SET_INTERFACE failed, continuing anyway");
    }
    
    // Send GET_MAX_LUN request
    result = hal_usb_control_transfer(device_addr,
                                     0xA1, // bmRequestType: Device to host, Class, Interface
                                     MSC_REQUEST_GET_MAX_LUN,
                                     0, // wValue: Zero
                                     0, // wIndex: Interface number (0 for this sample)
                                     max_lun, 1, NULL, NULL);
                                     
    if (result < 0) {
        // Some devices don't support this, so we'll assume a single LUN
        *max_lun = 0;
        return 0;
    }
    
    return 0;
}

/**
 * Reset a USB Mass Storage device
 * 
 * @param device_addr Device address
 * @return 0 on success, negative value on error
 */
static int reset_device(uint8_t device_addr) {
    // Find the device
    int device_index = usb_storage_get_device_by_addr(device_addr);
    if (device_index < 0) {
        return -1;
    }
    
    // Send a Mass Storage Reset
    int result = hal_usb_control_transfer(device_addr,
                                         0x21, // bmRequestType: Host to device, Class, Interface
                                         MSC_REQUEST_RESET,
                                         0, // wValue: Zero
                                         storage_devices[device_index].interface_num,
                                         NULL, 0, NULL, NULL);
                                         
    if (result < 0) {
        return -1;
    }
    
    // Reset the endpoints (clear all stall conditions)
    result = hal_usb_control_transfer(device_addr,
                                     0x02, // bmRequestType: Host to device, Standard, Endpoint
                                     0x01, // bRequest: CLEAR_FEATURE
                                     0,    // wValue: ENDPOINT_HALT
                                     storage_devices[device_index].bulk_in_ep,
                                     NULL, 0, NULL, NULL);
    
    if (result < 0) {
        return -1;
    }
    
    result = hal_usb_control_transfer(device_addr,
                                     0x02, // bmRequestType: Host to device, Standard, Endpoint
                                     0x01, // bRequest: CLEAR_FEATURE
                                     0,    // wValue: ENDPOINT_HALT
                                     storage_devices[device_index].bulk_out_ep,
                                     NULL, 0, NULL, NULL);
    
    if (result < 0) {
        return -1;
    }
    
    return 0;
}

/**
 * Perform an INQUIRY command to get device information
 * 
 * @param device_addr Device address
 * @param lun Logical unit number
 * @param device_info Device information structure to fill
 * @return 0 on success, negative value on error
 */
static int perform_inquiry(uint8_t device_addr, uint8_t lun, 
                          usb_mass_storage_device_t* device_info) {
    if (!device_info) {
        return -1;
    }
    
    // Set up INQUIRY command
    uint8_t cmd[6] = {0};
    cmd[0] = SCSI_CMD_INQUIRY;
    cmd[4] = 36; // Allocation length
    
    // Buffer for INQUIRY response
    uint8_t response[36] = {0};
    
    int result = send_mass_storage_command(device_addr, lun, cmd, 6, USB_MSC_DIR_IN, 
                                          response, sizeof(response));
                                          
    if (result < 0) {
        return result;
    }
    
    // Extract vendor and product information
    memcpy(device_info->vendor, &response[8], 8);
    device_info->vendor[8] = '\0';
    
    memcpy(device_info->product, &response[16], 16);
    device_info->product[16] = '\0';
    
    memcpy(device_info->revision, &response[32], 4);
    device_info->revision[4] = '\0';
    
    // Trim trailing spaces
    int i;
    for (i = 7; i >= 0 && device_info->vendor[i] == ' '; i--) {
        device_info->vendor[i] = '\0';
    }
    
    for (i = 15; i >= 0 && device_info->product[i] == ' '; i--) {
        device_info->product[i] = '\0';
    }
    
    for (i = 3; i >= 0 && device_info->revision[i] == ' '; i--) {
        device_info->revision[i] = '\0';
    }
    
    return 0;
}

/**
 * Find device index by address
 * 
 * @param device_addr Device address
 * @return Device index, or negative value if not found
 */
static int usb_storage_get_device_by_addr(uint8_t device_addr) {
    for (int i = 0; i < num_storage_devices; i++) {
        if (storage_devices[i].device_addr == device_addr) {
            return i;
        }
    }
    
    return -1; // Device not found
}