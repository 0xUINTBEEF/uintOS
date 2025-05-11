/**
 * @file nvme.h
 * @brief NVMe (Non-Volatile Memory Express) driver for uintOS
 * 
 * This file provides driver support for NVMe storage controllers
 * connecting PCIe-based solid-state drives.
 */

#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include <stdbool.h>
#include "../../../kernel/device_manager.h"
#include "../../../drivers/pci/pci.h"

// NVMe driver version
#define NVME_DRV_VERSION 0x00010000  // 1.0.0.0

// NVM Express Base Specification (1.4)

// NVMe controller registers (BAR0)
#define NVME_REG_CAP          0x0000  // Controller Capabilities
#define NVME_REG_VS           0x0008  // Version
#define NVME_REG_INTMS        0x000C  // Interrupt Mask Set
#define NVME_REG_INTMC        0x0010  // Interrupt Mask Clear
#define NVME_REG_CC           0x0014  // Controller Configuration
#define NVME_REG_CSTS         0x001C  // Controller Status
#define NVME_REG_NSSR         0x0020  // NVM Subsystem Reset
#define NVME_REG_AQA          0x0024  // Admin Queue Attributes
#define NVME_REG_ASQ          0x0028  // Admin Submission Queue Base Address
#define NVME_REG_ACQ          0x0030  // Admin Completion Queue Base Address
#define NVME_REG_CMBLOC       0x0038  // Controller Memory Buffer Location
#define NVME_REG_CMBSZ        0x003C  // Controller Memory Buffer Size

// NVMe controller configuration register (CC) bits
#define NVME_CC_EN            0x00000001  // Enable
#define NVME_CC_CSS_NVM       0x00000000  // I/O Command Set: NVM
#define NVME_CC_MPS_SHIFT     7           // Memory Page Size shift
#define NVME_CC_AMS_RR        0x00000000  // Arbitration Mechanism: Round Robin
#define NVME_CC_SHN_NONE      0x00000000  // Shutdown Notification: None
#define NVME_CC_SHN_NORMAL    0x00000001  // Shutdown Notification: Normal
#define NVME_CC_SHN_ABRUPT    0x00000002  // Shutdown Notification: Abrupt
#define NVME_CC_IOSQES_SHIFT  16          // I/O Submission Queue Entry Size shift
#define NVME_CC_IOCQES_SHIFT  20          // I/O Completion Queue Entry Size shift

// NVMe controller status register (CSTS) bits
#define NVME_CSTS_RDY         0x00000001  // Ready
#define NVME_CSTS_CFS         0x00000002  // Fatal Status
#define NVME_CSTS_SHST_MASK   0x0000000C  // Shutdown Status mask
#define NVME_CSTS_SHST_NONE   0x00000000  // Shutdown Status: None
#define NVME_CSTS_SHST_INPROG 0x00000004  // Shutdown Status: In progress
#define NVME_CSTS_SHST_CMPLT  0x00000008  // Shutdown Status: Complete

// NVMe queue entry sizes
#define NVME_QUEUE_ENTRY_BYTES 64  // Size of a submission or completion queue entry

// NVMe Admin command opcodes
#define NVME_ADMIN_CMD_DELETE_SQ      0x00  // Delete I/O Submission Queue
#define NVME_ADMIN_CMD_CREATE_SQ      0x01  // Create I/O Submission Queue
#define NVME_ADMIN_CMD_GET_LOG_PAGE   0x02  // Get Log Page
#define NVME_ADMIN_CMD_DELETE_CQ      0x04  // Delete I/O Completion Queue
#define NVME_ADMIN_CMD_CREATE_CQ      0x05  // Create I/O Completion Queue
#define NVME_ADMIN_CMD_IDENTIFY       0x06  // Identify
#define NVME_ADMIN_CMD_ABORT          0x08  // Abort
#define NVME_ADMIN_CMD_SET_FEATURES   0x09  // Set Features
#define NVME_ADMIN_CMD_GET_FEATURES   0x0A  // Get Features
#define NVME_ADMIN_CMD_ASYNC_EVENT    0x0C  // Asynchronous Event Request
#define NVME_ADMIN_CMD_FIRMWARE       0x10  // Firmware Image Download
#define NVME_ADMIN_CMD_FIRMWARE_COMMIT 0x11 // Firmware Commit
#define NVME_ADMIN_CMD_FORMAT_NVM     0x80  // Format NVM
#define NVME_ADMIN_CMD_SECURITY_SEND  0x81  // Security Send
#define NVME_ADMIN_CMD_SECURITY_RECV  0x82  // Security Receive

// NVMe I/O command opcodes
#define NVME_IO_CMD_FLUSH             0x00  // Flush
#define NVME_IO_CMD_WRITE             0x01  // Write
#define NVME_IO_CMD_READ              0x02  // Read
#define NVME_IO_CMD_WRITE_UNCORR      0x04  // Write Uncorrectable
#define NVME_IO_CMD_COMPARE           0x05  // Compare
#define NVME_IO_CMD_DATASET_MGMT      0x09  // Dataset Management

// NVMe Identify CNS values
#define NVME_IDENTIFY_NAMESPACE       0x00  // Identify Namespace
#define NVME_IDENTIFY_CONTROLLER      0x01  // Identify Controller
#define NVME_IDENTIFY_ACTIVE_NSIDS    0x02  // Identify Active Namespace IDs

// Maximum number of namespaces (per controller)
#define NVME_MAX_NAMESPACES 16

// Maximum queue entries (power of 2)
#define NVME_MAX_QUEUE_ENTRIES 256

// Maximum requests in flight
#define NVME_MAX_REQUESTS 32

// NVMe request status
typedef enum {
    NVME_REQ_FREE = 0,        // Request slot is free
    NVME_REQ_PENDING,         // Request is pending completion
    NVME_REQ_COMPLETED,       // Request completed successfully
    NVME_REQ_FAILED,          // Request failed
    NVME_REQ_TIMEOUT          // Request timed out
} nvme_req_status_t;

// NVMe command
typedef struct {
    // CDW0
    uint8_t  opcode;         // Command opcode
    uint8_t  fuse : 2;       // Fused operation
    uint8_t  reserved1 : 6;  // Reserved
    uint16_t cid;            // Command ID
    
    // CDW1-9
    uint32_t nsid;           // Namespace ID
    uint64_t reserved2;
    uint64_t metadata;       // Metadata pointer
    uint64_t prp1;           // PRP Entry 1
    uint64_t prp2;           // PRP Entry 2
    
    // CDW10-15
    uint32_t cdw10;          // Command-specific
    uint32_t cdw11;          // Command-specific
    uint32_t cdw12;          // Command-specific
    uint32_t cdw13;          // Command-specific
    uint32_t cdw14;          // Command-specific
    uint32_t cdw15;          // Command-specific
} nvme_cmd_t;

// NVMe completion
typedef struct {
    uint32_t result;         // Command-specific result
    uint32_t reserved;       // Reserved
    uint16_t sq_head;        // SQ head pointer
    uint16_t sq_id;          // SQ identifier
    uint16_t cid;            // Command ID
    uint16_t status;         // Status field
} nvme_cpl_t;

// NVMe completion status codes
#define NVME_SC_SUCCESS               0x000   // Successful completion
#define NVME_SC_INVALID_OPCODE        0x001   // Invalid command opcode
#define NVME_SC_INVALID_FIELD         0x002   // Invalid field in command
#define NVME_SC_COMMAND_ID_CONFLICT   0x003   // Command ID conflict
#define NVME_SC_DATA_TRANSFER_ERROR   0x004   // Data transfer error
#define NVME_SC_ABORTED_POWER_LOSS    0x005   // Command aborted due to power loss
#define NVME_SC_INTERNAL_ERROR        0x006   // Internal error
#define NVME_SC_COMMAND_ABORT         0x007   // Command abort requested
#define NVME_SC_COMMAND_ABORT_SQ_DEL  0x008   // Command aborted due to SQ deletion
#define NVME_SC_COMMAND_ABORT_FAIL_FUSE 0x009 // Command aborted due to failed fused command

// NVMe namespace structure
typedef struct {
    uint32_t id;                // Namespace ID
    uint64_t size;              // Size in logical blocks
    uint32_t lba_size;          // Size of LBA in bytes
    char     eui64[8];          // EUI-64 (if supported)
    bool     active;            // Whether this namespace is active
} nvme_namespace_t;

// NVMe queue structure
typedef struct {
    uint16_t id;                // Queue ID
    uint32_t head;              // Head pointer (consumer)
    uint32_t tail;              // Tail pointer (producer)
    uint32_t size;              // Size in entries
    uint32_t stride;            // Size of each entry
    void*    entries;           // Queue entries
    uint64_t phys_addr;         // Physical address of entries
} nvme_queue_t;

// NVMe request structure
typedef struct {
    uint16_t cmd_id;            // Command ID
    nvme_req_status_t status;   // Request status
    uint32_t result;            // Result
    uint16_t sq_id;             // Submission queue ID
    void*    buffer;            // Data buffer
    uint32_t buffer_size;       // Buffer size
    void     (*callback)(void* context, int status, uint32_t result); // Completion callback
    void*    context;           // Callback context
} nvme_request_t;

// NVMe controller structure
typedef struct {
    uint32_t vendor_id;         // PCI vendor ID
    uint32_t device_id;         // PCI device ID
    uint32_t mmio_base;         // Memory mapped register base
    uint32_t doorbell_stride;   // Doorbell register stride
    uint32_t db_offset;         // Doorbell offset
    uint32_t max_xfer;          // Maximum transfer size
    uint32_t stripe_size;       // Memory page size (stride)
    uint16_t max_qid;           // Maximum queue ID
    uint16_t admin_cq_id;       // Admin completion queue ID (typically 0)
    uint16_t admin_sq_id;       // Admin submission queue ID (typically 0)
    uint16_t io_cq_id;          // I/O completion queue ID
    uint16_t io_sq_id;          // I/O submission queue ID
    uint16_t    next_cmd_id;    // Next command ID
    
    nvme_queue_t admin_sq;      // Admin submission queue
    nvme_queue_t admin_cq;      // Admin completion queue
    nvme_queue_t io_sq;         // I/O submission queue
    nvme_queue_t io_cq;         // I/O completion queue
    
    nvme_namespace_t namespaces[NVME_MAX_NAMESPACES]; // Array of namespaces
    uint32_t num_namespaces;    // Number of namespaces
    
    nvme_request_t requests[NVME_MAX_REQUESTS]; // Request tracking
    mutex_t cmd_mutex;          // Command mutex
    
    bool initialized;           // Whether the controller is initialized
} nvme_controller_t;

// NVMe device private structure
typedef struct {
    nvme_controller_t* controller;  // NVMe controller
    uint32_t namespace_id;          // Namespace ID
} nvme_device_t;

/**
 * Initialize NVMe driver
 * 
 * @return 0 on success, negative error code on failure
 */
int nvme_init(void);

/**
 * Read sectors from NVMe device
 * 
 * @param dev Device pointer
 * @param buffer Buffer to read into
 * @param start_sector Starting LBA
 * @param sector_count Number of sectors to read
 * @return Number of sectors read, or negative error code
 */
int nvme_read(device_t* dev, void* buffer, uint64_t start_sector, uint32_t sector_count);

/**
 * Write sectors to NVMe device
 * 
 * @param dev Device pointer
 * @param buffer Buffer to write from
 * @param start_sector Starting LBA
 * @param sector_count Number of sectors to write
 * @return Number of sectors written, or negative error code
 */
int nvme_write(device_t* dev, const void* buffer, uint64_t start_sector, uint32_t sector_count);

/**
 * Flush NVMe device cache
 * 
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int nvme_flush(device_t* dev);

/**
 * Get NVMe device information
 * 
 * @param dev Device pointer
 * @param size Pointer to store device size in sectors
 * @param sector_size Pointer to store sector size in bytes
 * @return 0 on success, negative error code on failure
 */
int nvme_get_info(device_t* dev, uint64_t* size, uint32_t* sector_size);

#endif /* NVME_H */