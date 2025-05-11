/**
 * @file nvme.c
 * @brief NVMe (Non-Volatile Memory Express) driver implementation
 * 
 * This file implements the driver for NVMe storage controllers
 */

#include "nvme.h"
#include "../../../kernel/logging/log.h"
#include "../../../memory/heap.h"
#include "../../../hal/include/hal_io.h"
#include "../../../hal/include/hal_memory.h"
#include "../../../hal/include/hal_interrupt.h"
#include "../../../kernel/sync.h"
#include <string.h>

#define NVME_TAG "NVME"

// Maximum number of controllers
#define NVME_MAX_CONTROLLERS 4

// PCI class code and subclass for NVMe controllers
#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_SUBCLASS_NVME       0x08

// Storage for NVMe controllers
static nvme_controller_t controllers[NVME_MAX_CONTROLLERS];
static int num_controllers = 0;

// Forward declarations for internal functions
static int nvme_probe(pci_device_t* dev);
static int nvme_initialize(pci_device_t* dev);
static int nvme_remove(pci_device_t* dev);
static int nvme_suspend(pci_device_t* dev);
static int nvme_resume(pci_device_t* dev);

// Device operation functions
static int nvme_dev_open(device_t* dev, uint32_t flags);
static int nvme_dev_close(device_t* dev);
static int nvme_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset);
static int nvme_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset);
static int nvme_dev_ioctl(device_t* dev, int request, void* arg);

// PCI driver structure
static pci_driver_t nvme_driver = {
    .name = "nvme",
    .vendor_ids = NULL,  // Match any vendor ID
    .device_ids = NULL,  // Match any device ID
    .num_supported_devices = 0,  // Match based on class code instead
    .ops = {
        .probe = nvme_probe,
        .init = nvme_initialize,
        .remove = nvme_remove,
        .suspend = nvme_suspend,
        .resume = nvme_resume
    }
};

// Device operations
static device_ops_t nvme_dev_ops = {
    .open = nvme_dev_open,
    .close = nvme_dev_close,
    .read = nvme_dev_read,
    .write = nvme_dev_write,
    .ioctl = nvme_dev_ioctl
};

// Internal helper functions
static inline uint32_t nvme_read_reg32(nvme_controller_t* ctrl, uint32_t reg) {
    return *(volatile uint32_t*)(ctrl->mmio_base + reg);
}

static inline uint64_t nvme_read_reg64(nvme_controller_t* ctrl, uint32_t reg) {
    return *(volatile uint64_t*)(ctrl->mmio_base + reg);
}

static inline void nvme_write_reg32(nvme_controller_t* ctrl, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(ctrl->mmio_base + reg) = val;
}

static inline void nvme_write_reg64(nvme_controller_t* ctrl, uint32_t reg, uint64_t val) {
    *(volatile uint64_t*)(ctrl->mmio_base + reg) = val;
}

static inline void nvme_ring_doorbell(nvme_controller_t* ctrl, uint16_t qid, bool is_cq, uint16_t value) {
    uint32_t doorbell = ctrl->db_offset + (2 * qid + (is_cq ? 1 : 0)) * ctrl->doorbell_stride;
    nvme_write_reg32(ctrl, doorbell, value);
}

// Start controller enable sequence
static int nvme_enable_controller(nvme_controller_t* ctrl) {
    // Set configuration register to set page size, entry size, and enable controller
    uint32_t cc = nvme_read_reg32(ctrl, NVME_REG_CC);
    
    // Clear enable bit first if it's already set
    if (cc & NVME_CC_EN) {
        cc &= ~NVME_CC_EN;
        nvme_write_reg32(ctrl, NVME_REG_CC, cc);
        
        // Wait for controller to become not ready
        uint32_t timeout = 500;  // ms
        while (timeout > 0) {
            uint32_t csts = nvme_read_reg32(ctrl, NVME_REG_CSTS);
            if (!(csts & NVME_CSTS_RDY)) {
                break;
            }
            hal_timer_sleep(1);
            timeout--;
        }
        
        if (timeout == 0) {
            log_error(NVME_TAG, "Timeout waiting for controller to become not ready");
            return -1;
        }
    }
    
    // Set memory page size (4KB)
    uint32_t page_shift = 12;  // 4KB pages
    cc &= ~(0xF << NVME_CC_MPS_SHIFT);
    cc |= ((page_shift - 12) & 0xF) << NVME_CC_MPS_SHIFT;
    
    // Set I/O Submission Queue Entry Size and Completion Queue Entry Size (both 64 bytes, 2^6)
    cc &= ~(0xF << NVME_CC_IOSQES_SHIFT);
    cc |= (6 << NVME_CC_IOSQES_SHIFT);  // 2^6 = 64 bytes
    
    cc &= ~(0xF << NVME_CC_IOCQES_SHIFT);
    cc |= (6 << NVME_CC_IOCQES_SHIFT);  // 2^6 = 64 bytes
    
    // Set I/O Command Set
    cc &= ~0x7;  // Clear CSS bits
    cc |= NVME_CC_CSS_NVM;  // NVM command set
    
    // Set Arbitration Mechanism
    cc &= ~(0x7 << 11);  // Clear AMS bits
    cc |= NVME_CC_AMS_RR;  // Round Robin
    
    // Enable controller
    cc |= NVME_CC_EN;
    
    // Write configuration
    nvme_write_reg32(ctrl, NVME_REG_CC, cc);
    
    // Wait for controller to become ready
    uint32_t timeout = 500;  // ms
    while (timeout > 0) {
        uint32_t csts = nvme_read_reg32(ctrl, NVME_REG_CSTS);
        
        // Check for fatal error
        if (csts & NVME_CSTS_CFS) {
            log_error(NVME_TAG, "NVMe controller fatal error during initialization");
            return -1;
        }
        
        // Check for ready bit
        if (csts & NVME_CSTS_RDY) {
            return 0;  // Controller is ready
        }
        
        hal_timer_sleep(1);
        timeout--;
    }
    
    log_error(NVME_TAG, "Timeout waiting for controller to become ready");
    return -1;
}

// Create a queue
static int nvme_create_queue(nvme_controller_t* ctrl, nvme_queue_t* queue, uint16_t id, uint32_t size, bool is_cq) {
    // Size must be power of 2 and not exceed max size
    if (!is_power_of_2(size) || size > NVME_MAX_QUEUE_ENTRIES) {
        return -1;
    }
    
    // Allocate memory for queue entries (multiple of 4KB pages)
    uint32_t alloc_size = size * NVME_QUEUE_ENTRY_BYTES;
    alloc_size = ALIGN_UP(alloc_size, 4096);  // Align to 4KB pages
    
    uint64_t phys_addr;
    void* virt_addr = hal_memory_allocate_physical(alloc_size, 4096, HAL_MEMORY_CACHEABLE, &phys_addr);
    if (!virt_addr) {
        log_error(NVME_TAG, "Failed to allocate memory for queue %u", id);
        return -1;
    }
    
    // Zero out the memory
    memset(virt_addr, 0, alloc_size);
    
    // Initialize the queue structure
    queue->id = id;
    queue->head = 0;
    queue->tail = 0;
    queue->size = size;
    queue->stride = NVME_QUEUE_ENTRY_BYTES;
    queue->entries = virt_addr;
    queue->phys_addr = phys_addr;
    
    return 0;
}

// Create admin queue pair
static int nvme_create_admin_queues(nvme_controller_t* ctrl) {
    // Create admin submission queue
    if (nvme_create_queue(ctrl, &ctrl->admin_sq, ctrl->admin_sq_id, 32, false) != 0) {
        return -1;
    }
    
    // Create admin completion queue
    if (nvme_create_queue(ctrl, &ctrl->admin_cq, ctrl->admin_cq_id, 32, true) != 0) {
        hal_memory_free(ctrl->admin_sq.entries);
        return -1;
    }
    
    // Set admin queue attributes
    uint32_t aqa = ((ctrl->admin_cq.size - 1) << 16) | (ctrl->admin_sq.size - 1);
    nvme_write_reg32(ctrl, NVME_REG_AQA, aqa);
    
    // Set admin submission queue address
    nvme_write_reg64(ctrl, NVME_REG_ASQ, ctrl->admin_sq.phys_addr);
    
    // Set admin completion queue address
    nvme_write_reg64(ctrl, NVME_REG_ACQ, ctrl->admin_cq.phys_addr);
    
    return 0;
}

// Submit a command to a submission queue and wait for completion
static int nvme_submit_admin_cmd(nvme_controller_t* ctrl, nvme_cmd_t* cmd, void* buffer, uint32_t buffer_size) {
    mutex_lock(&ctrl->cmd_mutex);
    
    // Find a free request slot
    int req_idx = -1;
    for (int i = 0; i < NVME_MAX_REQUESTS; i++) {
        if (ctrl->requests[i].status == NVME_REQ_FREE) {
            req_idx = i;
            break;
        }
    }
    
    if (req_idx == -1) {
        mutex_unlock(&ctrl->cmd_mutex);
        return -1;  // No free request slots
    }
    
    // Prepare request
    nvme_request_t* req = &ctrl->requests[req_idx];
    req->status = NVME_REQ_PENDING;
    req->sq_id = ctrl->admin_sq_id;
    req->cmd_id = ctrl->next_cmd_id++;
    req->buffer = buffer;
    req->buffer_size = buffer_size;
    
    // Update command ID
    cmd->cid = req->cmd_id;
    
    // Queue the command
    nvme_queue_t* sq = &ctrl->admin_sq;
    nvme_cmd_t* cmd_entry = (nvme_cmd_t*)((uint8_t*)sq->entries + sq->tail * sq->stride);
    
    // Copy command to queue entry
    memcpy(cmd_entry, cmd, sizeof(nvme_cmd_t));
    
    // Update tail pointer
    sq->tail = (sq->tail + 1) % sq->size;
    
    // Ring doorbell
    nvme_ring_doorbell(ctrl, sq->id, false, sq->tail);
    
    // Wait for completion (polling)
    nvme_queue_t* cq = &ctrl->admin_cq;
    bool found = false;
    uint32_t timeout = 1000;  // ms
    
    while (timeout > 0) {
        // Check if any completions are available
        nvme_cpl_t* cpl = (nvme_cpl_t*)((uint8_t*)cq->entries + cq->head * cq->stride);
        
        // Check if this entry is new (phase bit check)
        if ((cpl->status & 1) == cq->head & 1) {
            // Check if this is our command
            if (cpl->cid == req->cmd_id) {
                // Update submission queue head
                sq->head = cpl->sq_head;
                
                // Update request status
                if ((cpl->status >> 1) == 0) {
                    req->status = NVME_REQ_COMPLETED;
                    req->result = cpl->result;
                } else {
                    req->status = NVME_REQ_FAILED;
                    log_error(NVME_TAG, "Admin command failed: status=%04X", cpl->status >> 1);
                }
                
                found = true;
                
                // Update completion queue head
                cq->head = (cq->head + 1) % cq->size;
                
                // Ring doorbell
                nvme_ring_doorbell(ctrl, cq->id, true, cq->head);
                
                break;
            }
        }
        
        hal_timer_sleep(1);
        timeout--;
    }
    
    // Handle timeout
    if (!found) {
        req->status = NVME_REQ_TIMEOUT;
        log_error(NVME_TAG, "Admin command timed out");
    }
    
    // Get result
    int status = (req->status == NVME_REQ_COMPLETED) ? 0 : -1;
    
    // Clean up request
    req->status = NVME_REQ_FREE;
    
    mutex_unlock(&ctrl->cmd_mutex);
    
    return status;
}

// Identify controller or namespace
static int nvme_identify(nvme_controller_t* ctrl, uint32_t nsid, uint32_t cns, void* buffer) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    // Set up the identify command
    cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    cmd.nsid = nsid;
    
    // Set up data buffer address
    uint64_t phys_addr;
    if (hal_memory_get_physical(buffer, &phys_addr) != HAL_SUCCESS) {
        return -1;
    }
    
    cmd.prp1 = phys_addr;
    cmd.prp2 = 0;
    
    // Set CNS (Controller or Namespace Structure)
    cmd.cdw10 = cns;
    
    // Submit command
    return nvme_submit_admin_cmd(ctrl, &cmd, buffer, 4096);
}

// Create I/O queue pair
static int nvme_create_io_queues(nvme_controller_t* ctrl) {
    // Create I/O completion queue first
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    // Create I/O completion queue
    ctrl->io_cq_id = 1;
    if (nvme_create_queue(ctrl, &ctrl->io_cq, ctrl->io_cq_id, 64, true) != 0) {
        return -1;
    }
    
    // Set up create I/O completion queue command
    cmd.opcode = NVME_ADMIN_CMD_CREATE_CQ;
    cmd.nsid = 0;
    
    // Set physical address of queue
    cmd.prp1 = ctrl->io_cq.phys_addr;
    cmd.prp2 = 0;
    
    // Set queue size and ID
    cmd.cdw10 = ((ctrl->io_cq.size - 1) << 16) | ctrl->io_cq_id;
    
    // Set queue attributes (physically contiguous and interrupts enabled)
    cmd.cdw11 = (1 << 1) | (1 << 0);
    
    // Submit command
    if (nvme_submit_admin_cmd(ctrl, &cmd, NULL, 0) != 0) {
        hal_memory_free(ctrl->io_cq.entries);
        return -1;
    }
    
    // Create I/O submission queue
    ctrl->io_sq_id = 1;
    if (nvme_create_queue(ctrl, &ctrl->io_sq, ctrl->io_sq_id, 64, false) != 0) {
        return -1;
    }
    
    // Set up create I/O submission queue command
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CMD_CREATE_SQ;
    cmd.nsid = 0;
    
    // Set physical address of queue
    cmd.prp1 = ctrl->io_sq.phys_addr;
    cmd.prp2 = 0;
    
    // Set queue size and ID
    cmd.cdw10 = ((ctrl->io_sq.size - 1) << 16) | ctrl->io_sq_id;
    
    // Set queue attributes (physically contiguous and associated with CQ 1)
    cmd.cdw11 = (ctrl->io_cq_id << 16) | (1 << 0);
    
    // Submit command
    if (nvme_submit_admin_cmd(ctrl, &cmd, NULL, 0) != 0) {
        hal_memory_free(ctrl->io_sq.entries);
        hal_memory_free(ctrl->io_cq.entries);
        return -1;
    }
    
    return 0;
}

// Discover namespaces
static int nvme_discover_namespaces(nvme_controller_t* ctrl) {
    uint32_t* ns_list = (uint32_t*)heap_alloc(4096);
    if (!ns_list) {
        return -1;
    }
    
    // Identify active namespaces
    if (nvme_identify(ctrl, 0, NVME_IDENTIFY_ACTIVE_NSIDS, ns_list) != 0) {
        heap_free(ns_list);
        return -1;
    }
    
    // Process the list of active namespaces
    uint32_t count = 0;
    
    for (int i = 0; i < 1024 && count < NVME_MAX_NAMESPACES; i++) {
        uint32_t nsid = ns_list[i];
        if (nsid == 0) {
            break;
        }
        
        // Identify this namespace
        uint8_t* ns_data = (uint8_t*)heap_alloc(4096);
        if (!ns_data) {
            continue;
        }
        
        if (nvme_identify(ctrl, nsid, NVME_IDENTIFY_NAMESPACE, ns_data) != 0) {
            heap_free(ns_data);
            continue;
        }
        
        // Extract namespace information
        uint64_t size = *(uint64_t*)(ns_data + 0);  // LBA size in blocks
        uint32_t lba_format = ns_data[26] & 0xF;    // Current LBA format
        uint32_t lba_size_offset = 128 + (lba_format * 4);  // Offset to LBA size data
        uint32_t lba_size_raw = *(uint32_t*)(ns_data + lba_size_offset);
        uint32_t lba_size = 1 << (lba_size_raw & 0xFF);     // LBA size in bytes
        
        // Store namespace information
        ctrl->namespaces[count].id = nsid;
        ctrl->namespaces[count].size = size;
        ctrl->namespaces[count].lba_size = lba_size;
        ctrl->namespaces[count].active = true;
        
        // Copy EUI-64 if available
        if ((ns_data[0] & 0x01) != 0) {
            memcpy(ctrl->namespaces[count].eui64, ns_data + 120, 8);
        } else {
            memset(ctrl->namespaces[count].eui64, 0, 8);
        }
        
        log_info(NVME_TAG, "Found namespace %u: size=%llu blocks, block size=%u bytes", 
                 nsid, size, lba_size);
        
        heap_free(ns_data);
        count++;
    }
    
    ctrl->num_namespaces = count;
    heap_free(ns_list);
    
    return (count > 0) ? 0 : -1;
}

/**
 * Initialize NVMe driver
 */
int nvme_init(void) {
    log_info(NVME_TAG, "Initializing NVMe driver");
    
    // Initialize controller array
    memset(controllers, 0, sizeof(controllers));
    num_controllers = 0;
    
    // Register with PCI subsystem
    int result = pci_register_driver(&nvme_driver);
    if (result != 0) {
        log_error(NVME_TAG, "Failed to register NVMe PCI driver: %d", result);
        return -1;
    }
    
    log_info(NVME_TAG, "NVMe driver initialized");
    return 0;
}

/**
 * Probe for NVMe devices
 */
static int nvme_probe(pci_device_t* dev) {
    // Check if this is an NVMe controller
    if (dev->id.class_code == PCI_CLASS_MASS_STORAGE && 
        dev->id.subclass == PCI_SUBCLASS_NVME) {
        log_info(NVME_TAG, "Found NVMe controller: VID=%04X, DID=%04X", 
                 dev->id.vendor_id, dev->id.device_id);
        return 0;  // Match found
    }
    
    return -1;  // Not an NVMe controller
}

/**
 * Initialize NVMe controller
 */
static int nvme_initialize(pci_device_t* dev) {
    log_info(NVME_TAG, "Initializing NVMe controller: VID=%04X, DID=%04X", 
             dev->id.vendor_id, dev->id.device_id);
    
    // Check if we have room for another controller
    if (num_controllers >= NVME_MAX_CONTROLLERS) {
        log_error(NVME_TAG, "Maximum number of NVMe controllers reached");
        return -1;
    }
    
    // Allocate a controller
    nvme_controller_t* ctrl = &controllers[num_controllers];
    memset(ctrl, 0, sizeof(nvme_controller_t));
    
    // Store PCI device information
    ctrl->vendor_id = dev->id.vendor_id;
    ctrl->device_id = dev->id.device_id;
    
    // Store private data in PCI device structure
    dev->private_data = ctrl;
    
    // Enable PCI bus mastering and memory space
    pci_enable_bus_mastering(dev);
    pci_enable_memory_space(dev);
    
    // Map BAR 0 (MMIO registers)
    uint32_t mmio_base;
    uint32_t mmio_size;
    bool is_io;
    
    if (pci_get_bar_info(dev, 0, &mmio_base, &mmio_size, &is_io) != 0 || is_io) {
        log_error(NVME_TAG, "Failed to get MMIO BAR information");
        return -1;
    }
    
    // Map the MMIO registers into virtual memory
    void* mmio_virt;
    if (hal_memory_map_physical(mmio_base, mmio_size, HAL_MEMORY_UNCACHEABLE, &mmio_virt) != HAL_SUCCESS) {
        log_error(NVME_TAG, "Failed to map MMIO registers");
        return -1;
    }
    
    ctrl->mmio_base = (uint32_t)mmio_virt;
    
    // Read controller capabilities
    uint64_t cap = nvme_read_reg64(ctrl, NVME_REG_CAP);
    ctrl->doorbell_stride = 4 << ((cap >> 32) & 0xF);
    ctrl->db_offset = 0x1000;  // Default doorbell offset
    
    // Initialize command mutex
    mutex_init(&ctrl->cmd_mutex);
    
    // Enable the controller
    if (nvme_enable_controller(ctrl) != 0) {
        log_error(NVME_TAG, "Failed to enable NVMe controller");
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Create admin queues
    if (nvme_create_admin_queues(ctrl) != 0) {
        log_error(NVME_TAG, "Failed to create admin queues");
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Identify controller
    uint8_t* id_data = (uint8_t*)heap_alloc(4096);
    if (!id_data) {
        log_error(NVME_TAG, "Failed to allocate memory for controller identification");
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    if (nvme_identify(ctrl, 0, NVME_IDENTIFY_CONTROLLER, id_data) != 0) {
        log_error(NVME_TAG, "Failed to identify controller");
        heap_free(id_data);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Extract controller information
    char model[41] = {0};
    char serial[21] = {0};
    char firmware[9] = {0};
    
    // Model number (bytes 24-63)
    memcpy(model, id_data + 24, 40);
    
    // Serial number (bytes 4-23)
    memcpy(serial, id_data + 4, 20);
    
    // Firmware revision (bytes 64-71)
    memcpy(firmware, id_data + 64, 8);
    
    // Trim whitespace
    model[40] = '\0';
    serial[20] = '\0';
    firmware[8] = '\0';
    
    log_info(NVME_TAG, "Controller: %s, SN: %s, FW: %s", model, serial, firmware);
    
    heap_free(id_data);
    
    // Create I/O queues
    if (nvme_create_io_queues(ctrl) != 0) {
        log_error(NVME_TAG, "Failed to create I/O queues");
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Discover namespaces
    if (nvme_discover_namespaces(ctrl) != 0) {
        log_error(NVME_TAG, "Failed to discover namespaces");
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Create devices for each namespace
    for (uint32_t i = 0; i < ctrl->num_namespaces; i++) {
        nvme_namespace_t* ns = &ctrl->namespaces[i];
        
        // Allocate device structure
        device_t* nvme_dev = (device_t*)heap_alloc(sizeof(device_t));
        if (!nvme_dev) {
            log_error(NVME_TAG, "Failed to allocate device structure for namespace %u", ns->id);
            continue;
        }
        
        // Allocate private data
        nvme_device_t* nvme_private = (nvme_device_t*)heap_alloc(sizeof(nvme_device_t));
        if (!nvme_private) {
            log_error(NVME_TAG, "Failed to allocate private data for namespace %u", ns->id);
            heap_free(nvme_dev);
            continue;
        }
        
        // Initialize private data
        nvme_private->controller = ctrl;
        nvme_private->namespace_id = ns->id;
        
        // Initialize device structure
        memset(nvme_dev, 0, sizeof(device_t));
        
        snprintf(nvme_dev->name, sizeof(nvme_dev->name), "nvme%dn%u", num_controllers, i);
        nvme_dev->type = DEVICE_TYPE_BLOCK;
        nvme_dev->status = DEVICE_STATUS_ENABLED;
        nvme_dev->vendor_id = ctrl->vendor_id;
        nvme_dev->device_id = ctrl->device_id;
        nvme_dev->private_data = nvme_private;
        nvme_dev->ops = &nvme_dev_ops;
        
        // Register the device
        if (device_register(nvme_dev) != DEVICE_OK) {
            log_error(NVME_TAG, "Failed to register device for namespace %u", ns->id);
            heap_free(nvme_private);
            heap_free(nvme_dev);
            continue;
        }
        
        log_info(NVME_TAG, "Registered device '%s' for namespace %u", nvme_dev->name, ns->id);
    }
    
    // Mark controller as initialized
    ctrl->initialized = true;
    num_controllers++;
    
    log_info(NVME_TAG, "NVMe controller initialized");
    return 0;
}

/**
 * Remove NVMe controller
 */
static int nvme_remove(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    nvme_controller_t* ctrl = (nvme_controller_t*)dev->private_data;
    
    log_info(NVME_TAG, "Removing NVMe controller");
    
    // Shutdown controller gracefully
    uint32_t cc = nvme_read_reg32(ctrl, NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    nvme_write_reg32(ctrl, NVME_REG_CC, cc);
    
    // Wait for controller to become not ready
    uint32_t timeout = 500;  // ms
    while (timeout > 0) {
        uint32_t csts = nvme_read_reg32(ctrl, NVME_REG_CSTS);
        if (!(csts & NVME_CSTS_RDY)) {
            break;
        }
        hal_timer_sleep(1);
        timeout--;
    }
    
    // Free queue resources
    if (ctrl->admin_sq.entries) {
        hal_memory_free(ctrl->admin_sq.entries);
    }
    
    if (ctrl->admin_cq.entries) {
        hal_memory_free(ctrl->admin_cq.entries);
    }
    
    if (ctrl->io_sq.entries) {
        hal_memory_free(ctrl->io_sq.entries);
    }
    
    if (ctrl->io_cq.entries) {
        hal_memory_free(ctrl->io_cq.entries);
    }
    
    // Unmap MMIO region
    if (ctrl->mmio_base) {
        hal_memory_unmap((void*)ctrl->mmio_base, 0);  // Size not known at this point
    }
    
    // Clear controller data
    dev->private_data = NULL;
    memset(ctrl, 0, sizeof(nvme_controller_t));
    
    // Decrease controller count
    if (num_controllers > 0) {
        num_controllers--;
    }
    
    return 0;
}

/**
 * Suspend NVMe controller
 */
static int nvme_suspend(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    nvme_controller_t* ctrl = (nvme_controller_t*)dev->private_data;
    
    log_info(NVME_TAG, "Suspending NVMe controller");
    
    // Initiate normal shutdown
    uint32_t cc = nvme_read_reg32(ctrl, NVME_REG_CC);
    cc |= NVME_CC_SHN_NORMAL;
    nvme_write_reg32(ctrl, NVME_REG_CC, cc);
    
    // Wait for shutdown to complete
    uint32_t timeout = 500;  // ms
    while (timeout > 0) {
        uint32_t csts = nvme_read_reg32(ctrl, NVME_REG_CSTS);
        if ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_CMPLT) {
            break;
        }
        hal_timer_sleep(1);
        timeout--;
    }
    
    return 0;
}

/**
 * Resume NVMe controller
 */
static int nvme_resume(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    nvme_controller_t* ctrl = (nvme_controller_t*)dev->private_data;
    
    log_info(NVME_TAG, "Resuming NVMe controller");
    
    // Re-enable controller
    return nvme_enable_controller(ctrl);
}

/**
 * Submit an I/O command and wait for completion
 */
static int nvme_submit_io_cmd(nvme_controller_t* ctrl, nvme_cmd_t* cmd, void* buffer, uint32_t buffer_size) {
    mutex_lock(&ctrl->cmd_mutex);
    
    // Find a free request slot
    int req_idx = -1;
    for (int i = 0; i < NVME_MAX_REQUESTS; i++) {
        if (ctrl->requests[i].status == NVME_REQ_FREE) {
            req_idx = i;
            break;
        }
    }
    
    if (req_idx == -1) {
        mutex_unlock(&ctrl->cmd_mutex);
        return -1;  // No free request slots
    }
    
    // Prepare request
    nvme_request_t* req = &ctrl->requests[req_idx];
    req->status = NVME_REQ_PENDING;
    req->sq_id = ctrl->io_sq_id;
    req->cmd_id = ctrl->next_cmd_id++;
    req->buffer = buffer;
    req->buffer_size = buffer_size;
    
    // Update command ID
    cmd->cid = req->cmd_id;
    
    // Queue the command
    nvme_queue_t* sq = &ctrl->io_sq;
    nvme_cmd_t* cmd_entry = (nvme_cmd_t*)((uint8_t*)sq->entries + sq->tail * sq->stride);
    
    // Copy command to queue entry
    memcpy(cmd_entry, cmd, sizeof(nvme_cmd_t));
    
    // Update tail pointer
    sq->tail = (sq->tail + 1) % sq->size;
    
    // Ring doorbell
    nvme_ring_doorbell(ctrl, sq->id, false, sq->tail);
    
    // Wait for completion (polling)
    nvme_queue_t* cq = &ctrl->io_cq;
    bool found = false;
    uint32_t timeout = 1000;  // ms
    
    while (timeout > 0) {
        // Check if any completions are available
        nvme_cpl_t* cpl = (nvme_cpl_t*)((uint8_t*)cq->entries + cq->head * cq->stride);
        
        // Check if this entry is new (phase bit check)
        if ((cpl->status & 1) == cq->head & 1) {
            // Check if this is our command
            if (cpl->cid == req->cmd_id) {
                // Update submission queue head
                sq->head = cpl->sq_head;
                
                // Update request status
                if ((cpl->status >> 1) == 0) {
                    req->status = NVME_REQ_COMPLETED;
                    req->result = cpl->result;
                } else {
                    req->status = NVME_REQ_FAILED;
                    log_error(NVME_TAG, "I/O command failed: status=%04X", cpl->status >> 1);
                }
                
                found = true;
                
                // Update completion queue head
                cq->head = (cq->head + 1) % cq->size;
                
                // Ring doorbell
                nvme_ring_doorbell(ctrl, cq->id, true, cq->head);
                
                break;
            }
        }
        
        hal_timer_sleep(1);
        timeout--;
    }
    
    // Handle timeout
    if (!found) {
        req->status = NVME_REQ_TIMEOUT;
        log_error(NVME_TAG, "I/O command timed out");
    }
    
    // Get result
    int status = (req->status == NVME_REQ_COMPLETED) ? 0 : -1;
    
    // Clean up request
    req->status = NVME_REQ_FREE;
    
    mutex_unlock(&ctrl->cmd_mutex);
    
    return status;
}

/**
 * Read sectors from NVMe device
 */
int nvme_read(device_t* dev, void* buffer, uint64_t start_sector, uint32_t sector_count) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    nvme_controller_t* ctrl = nvme_dev->controller;
    
    // Find namespace
    nvme_namespace_t* ns = NULL;
    for (uint32_t i = 0; i < ctrl->num_namespaces; i++) {
        if (ctrl->namespaces[i].id == nvme_dev->namespace_id) {
            ns = &ctrl->namespaces[i];
            break;
        }
    }
    
    if (!ns) {
        return DEVICE_ERROR_NO_DEVICE;
    }
    
    // Check if request is within bounds
    if (start_sector + sector_count > ns->size) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Prepare read command
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    cmd.opcode = NVME_IO_CMD_READ;
    cmd.nsid = nvme_dev->namespace_id;
    
    // Get physical address of buffer
    uint64_t phys_addr;
    if (hal_memory_get_physical(buffer, &phys_addr) != HAL_SUCCESS) {
        return DEVICE_ERROR_RESOURCE;
    }
    
    // Set data pointers
    cmd.prp1 = phys_addr;
    
    // If we need a second page for large transfers
    uint32_t buffer_size = sector_count * ns->lba_size;
    if (buffer_size > 4096) {
        // Calculate address of second page
        uint64_t second_page = phys_addr + 4096;
        cmd.prp2 = second_page;
    }
    
    // Set LBA range
    cmd.cdw10 = (uint32_t)start_sector;
    cmd.cdw11 = (uint32_t)(start_sector >> 32);
    cmd.cdw12 = sector_count - 1;  // 0-based
    
    // Submit command
    int result = nvme_submit_io_cmd(ctrl, &cmd, buffer, buffer_size);
    
    return (result == 0) ? sector_count : result;
}

/**
 * Write sectors to NVMe device
 */
int nvme_write(device_t* dev, const void* buffer, uint64_t start_sector, uint32_t sector_count) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    nvme_controller_t* ctrl = nvme_dev->controller;
    
    // Find namespace
    nvme_namespace_t* ns = NULL;
    for (uint32_t i = 0; i < ctrl->num_namespaces; i++) {
        if (ctrl->namespaces[i].id == nvme_dev->namespace_id) {
            ns = &ctrl->namespaces[i];
            break;
        }
    }
    
    if (!ns) {
        return DEVICE_ERROR_NO_DEVICE;
    }
    
    // Check if request is within bounds
    if (start_sector + sector_count > ns->size) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Prepare write command
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    cmd.opcode = NVME_IO_CMD_WRITE;
    cmd.nsid = nvme_dev->namespace_id;
    
    // Get physical address of buffer
    uint64_t phys_addr;
    if (hal_memory_get_physical((void*)buffer, &phys_addr) != HAL_SUCCESS) {
        return DEVICE_ERROR_RESOURCE;
    }
    
    // Set data pointers
    cmd.prp1 = phys_addr;
    
    // If we need a second page for large transfers
    uint32_t buffer_size = sector_count * ns->lba_size;
    if (buffer_size > 4096) {
        // Calculate address of second page
        uint64_t second_page = phys_addr + 4096;
        cmd.prp2 = second_page;
    }
    
    // Set LBA range
    cmd.cdw10 = (uint32_t)start_sector;
    cmd.cdw11 = (uint32_t)(start_sector >> 32);
    cmd.cdw12 = sector_count - 1;  // 0-based
    
    // Submit command
    int result = nvme_submit_io_cmd(ctrl, &cmd, (void*)buffer, buffer_size);
    
    return (result == 0) ? sector_count : result;
}

/**
 * Flush NVMe device cache
 */
int nvme_flush(device_t* dev) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    nvme_controller_t* ctrl = nvme_dev->controller;
    
    // Prepare flush command
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    cmd.opcode = NVME_IO_CMD_FLUSH;
    cmd.nsid = nvme_dev->namespace_id;
    
    // Submit command
    return nvme_submit_io_cmd(ctrl, &cmd, NULL, 0);
}

/**
 * Get NVMe device information
 */
int nvme_get_info(device_t* dev, uint64_t* size, uint32_t* sector_size) {
    if (!dev || !dev->private_data || !size || !sector_size) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    nvme_controller_t* ctrl = nvme_dev->controller;
    
    // Find namespace
    nvme_namespace_t* ns = NULL;
    for (uint32_t i = 0; i < ctrl->num_namespaces; i++) {
        if (ctrl->namespaces[i].id == nvme_dev->namespace_id) {
            ns = &ctrl->namespaces[i];
            break;
        }
    }
    
    if (!ns) {
        return DEVICE_ERROR_NO_DEVICE;
    }
    
    // Return size and sector size
    *size = ns->size;
    *sector_size = ns->lba_size;
    
    return 0;
}

/**
 * Open NVMe device
 */
static int nvme_dev_open(device_t* dev, uint32_t flags) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Nothing special to do for NVMe device open
    return DEVICE_OK;
}

/**
 * Close NVMe device
 */
static int nvme_dev_close(device_t* dev) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Nothing special to do for NVMe device close
    return DEVICE_OK;
}

/**
 * Read from NVMe device
 */
static int nvme_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    nvme_controller_t* ctrl = nvme_dev->controller;
    
    // Find namespace
    nvme_namespace_t* ns = NULL;
    for (uint32_t i = 0; i < ctrl->num_namespaces; i++) {
        if (ctrl->namespaces[i].id == nvme_dev->namespace_id) {
            ns = &ctrl->namespaces[i];
            break;
        }
    }
    
    if (!ns) {
        return DEVICE_ERROR_NO_DEVICE;
    }
    
    // Convert byte offset to sector offset
    uint64_t start_sector = offset / ns->lba_size;
    
    // Calculate sector offset within first sector
    uint32_t sector_offset = offset % ns->lba_size;
    
    // Calculate number of whole sectors to read
    uint32_t sector_count = (sector_offset + size + ns->lba_size - 1) / ns->lba_size;
    
    // Allocate temporary buffer for sector-aligned I/O if needed
    void* temp_buffer = NULL;
    void* read_buffer = buffer;
    
    if (sector_offset != 0 || size % ns->lba_size != 0) {
        temp_buffer = heap_alloc(sector_count * ns->lba_size);
        if (!temp_buffer) {
            return DEVICE_ERROR_RESOURCE;
        }
        read_buffer = temp_buffer;
    }
    
    // Read sectors
    int result = nvme_read(dev, read_buffer, start_sector, sector_count);
    
    // Copy data from temporary buffer if used
    if (temp_buffer) {
        if (result > 0) {
            memcpy(buffer, (uint8_t*)temp_buffer + sector_offset, size);
            result = size;
        }
        heap_free(temp_buffer);
    } else if (result > 0) {
        // Convert sectors to bytes
        result *= ns->lba_size;
    }
    
    return result;
}

/**
 * Write to NVMe device
 */
static int nvme_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    nvme_controller_t* ctrl = nvme_dev->controller;
    
    // Find namespace
    nvme_namespace_t* ns = NULL;
    for (uint32_t i = 0; i < ctrl->num_namespaces; i++) {
        if (ctrl->namespaces[i].id == nvme_dev->namespace_id) {
            ns = &ctrl->namespaces[i];
            break;
        }
    }
    
    if (!ns) {
        return DEVICE_ERROR_NO_DEVICE;
    }
    
    // Convert byte offset to sector offset
    uint64_t start_sector = offset / ns->lba_size;
    
    // Calculate sector offset within first sector
    uint32_t sector_offset = offset % ns->lba_size;
    
    // Calculate number of whole sectors to write
    uint32_t sector_count = (sector_offset + size + ns->lba_size - 1) / ns->lba_size;
    
    // Handle unaligned writes
    if (sector_offset != 0 || size % ns->lba_size != 0) {
        // Allocate temporary buffer
        void* temp_buffer = heap_alloc(sector_count * ns->lba_size);
        if (!temp_buffer) {
            return DEVICE_ERROR_RESOURCE;
        }
        
        // Read original sectors for partial writes
        int read_result = nvme_read(dev, temp_buffer, start_sector, sector_count);
        if (read_result < 0) {
            heap_free(temp_buffer);
            return read_result;
        }
        
        // Copy user data to temporary buffer
        memcpy((uint8_t*)temp_buffer + sector_offset, buffer, size);
        
        // Write modified sectors
        int write_result = nvme_write(dev, temp_buffer, start_sector, sector_count);
        heap_free(temp_buffer);
        
        return (write_result > 0) ? size : write_result;
    } else {
        // Aligned write
        int result = nvme_write(dev, buffer, start_sector, sector_count);
        
        // Convert sectors to bytes
        if (result > 0) {
            result *= ns->lba_size;
        }
        
        return result;
    }
}

/**
 * IOCTL for NVMe device
 */
static int nvme_dev_ioctl(device_t* dev, int request, void* arg) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    nvme_device_t* nvme_dev = (nvme_device_t*)dev->private_data;
    
    switch (request) {
        case 0x7001:  // Get device info
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                struct {
                    uint64_t size;
                    uint32_t sector_size;
                } *info = (void*)arg;
                
                return nvme_get_info(dev, &info->size, &info->sector_size);
            }
            
        case 0x7002:  // Flush cache
            return nvme_flush(dev);
            
        default:
            return DEVICE_ERROR_UNSUPPORTED;
    }
}