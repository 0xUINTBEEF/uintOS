/**
 * @file usb_mass_storage.h
 * @brief USB Mass Storage Driver
 * 
 * USB Mass Storage Driver implementation for uintOS, supporting basic SCSI operations
 * on USB storage devices (flash drives, external hard drives, etc.)
 */

#ifndef USB_MASS_STORAGE_H
#define USB_MASS_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "../../hal/include/hal_usb.h"

// USB Mass Storage Class (MSC) specific constants
#define USB_CLASS_MASS_STORAGE      0x08
#define USB_SUBCLASS_SCSI           0x06
#define USB_PROTOCOL_BULK_ONLY      0x50

// MSC-specific request codes
#define MSC_REQUEST_GET_MAX_LUN     0xFE
#define MSC_REQUEST_RESET           0xFF

// SCSI command opcodes
#define SCSI_CMD_TEST_UNIT_READY    0x00
#define SCSI_CMD_REQUEST_SENSE      0x03
#define SCSI_CMD_INQUIRY            0x12
#define SCSI_CMD_READ_CAPACITY      0x25
#define SCSI_CMD_READ_10            0x28
#define SCSI_CMD_WRITE_10           0x2A

// USB Mass Storage device information
typedef struct {
    uint8_t device_addr;           // USB device address
    uint8_t interface_num;         // Interface number
    uint8_t bulk_in_ep;            // Bulk IN endpoint
    uint8_t bulk_out_ep;           // Bulk OUT endpoint
    uint8_t max_lun;               // Maximum logical unit number
    uint32_t block_size;           // Block size in bytes
    uint32_t num_blocks;           // Number of blocks
    char vendor[16];               // Vendor ID string
    char product[32];              // Product ID string
    char revision[8];              // Product revision string
    bool mounted;                  // Whether the device is mounted
    int vfs_handle;                // Handle for VFS integration
} usb_mass_storage_device_t;

// CBW (Command Block Wrapper) structure for Bulk-Only Mass Storage
typedef struct __attribute__((packed)) {
    uint32_t signature;            // 'USBC' (0x43425355)
    uint32_t tag;                  // Command tag
    uint32_t data_transfer_length; // Number of bytes to transfer
    uint8_t flags;                 // Direction flag (bit 7)
    uint8_t lun;                   // Logical Unit Number
    uint8_t cb_length;             // Command block length (1-16)
    uint8_t command_block[16];     // Command block
} usb_msc_cbw_t;

// CSW (Command Status Wrapper) structure for Bulk-Only Mass Storage
typedef struct __attribute__((packed)) {
    uint32_t signature;            // 'USBS' (0x53425355)
    uint32_t tag;                  // Same tag as CBW
    uint32_t data_residue;         // Difference between expected and actual length
    uint8_t status;                // Command status
} usb_msc_csw_t;

// CBW Signature 'USBC'
#define USB_MSC_CBW_SIGNATURE      0x43425355

// CSW Signature 'USBS'
#define USB_MSC_CSW_SIGNATURE      0x53425355

// Direction flags for CBW
#define USB_MSC_DIR_OUT            0x00    // Host to device
#define USB_MSC_DIR_IN             0x80    // Device to host

// Status values for CSW
#define USB_MSC_STATUS_PASSED      0x00
#define USB_MSC_STATUS_FAILED      0x01
#define USB_MSC_STATUS_PHASE_ERROR 0x02

/**
 * Initialize the USB Mass Storage driver
 * 
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_init(void);

/**
 * Shut down the USB Mass Storage driver
 */
void usb_mass_storage_shutdown(void);

/**
 * Detect and initialize USB Mass Storage devices
 * 
 * @return Number of devices found, negative value on error
 */
int usb_mass_storage_detect_devices(void);

/**
 * Get information about USB Mass Storage devices
 * 
 * @param devices Array to store device information
 * @param max_devices Maximum number of devices to return
 * @return Number of devices found, negative value on error
 */
int usb_mass_storage_get_devices(usb_mass_storage_device_t* devices, int max_devices);

/**
 * Get capacity information from a Mass Storage device
 * 
 * @param device_addr Device address
 * @param block_size Pointer to store block size
 * @param num_blocks Pointer to store number of blocks
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_get_capacity(uint8_t device_addr, uint32_t* block_size, uint32_t* num_blocks);

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
                                 void* buffer, uint32_t num_blocks);

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
                                 const void* buffer, uint32_t num_blocks);

/**
 * Test if a USB Mass Storage device is ready
 * 
 * @param device_addr Device address
 * @param lun Logical unit number
 * @return 1 if ready, 0 if not ready, negative value on error
 */
int usb_mass_storage_test_unit_ready(uint8_t device_addr, uint8_t lun);

/**
 * Mount a USB Mass Storage device as a filesystem
 * 
 * @param device_addr Device address
 * @param mount_point Mount point path
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_mount(uint8_t device_addr, const char* mount_point);

/**
 * Unmount a USB Mass Storage device
 * 
 * @param device_addr Device address
 * @return 0 on success, negative value on error
 */
int usb_mass_storage_unmount(uint8_t device_addr);

#endif // USB_MASS_STORAGE_H