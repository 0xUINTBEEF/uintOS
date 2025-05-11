/**
 * @file pci.c
 * @brief PCI bus driver framework implementation for uintOS
 *
 * This module implements the PCI bus driver framework for detecting,
 * enumerating, and managing PCI devices in the system.
 */

#include "pci.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include <string.h>
#include <stdio.h>

#define PCI_TAG "PCI"

/**
 * Maximum number of PCI devices we can handle
 */
#define MAX_PCI_DEVICES 256

// PCI device storage
static pci_device_t* pci_devices = NULL;
static int num_pci_devices = 0;

// PCI driver list
static pci_driver_t* pci_drivers = NULL;

// Forward declarations
static int pci_enumerate_buses(void);
static int pci_enumerate_bus(uint8_t bus);
static int pci_enumerate_device(uint8_t bus, uint8_t device);
static int pci_enumerate_function(uint8_t bus, uint8_t device, uint8_t function);
static void pci_detect_bar_sizes(pci_device_id_t* id);
static int pci_match_device_to_driver(pci_device_t* dev);
static const char* pci_class_to_string(uint8_t class_code, uint8_t subclass);
static pci_device_t* pci_alloc_device(void);
static void pci_dump_device_info(pci_device_t* dev);

/**
 * Initialize the PCI subsystem
 */
int pci_init(void) {
    log_info(PCI_TAG, "Initializing PCI subsystem");
    
    // Allocate memory for PCI device array
    pci_devices = (pci_device_t*)heap_alloc(sizeof(pci_device_t) * MAX_PCI_DEVICES);
    if (!pci_devices) {
        log_error(PCI_TAG, "Failed to allocate memory for PCI devices");
        return -1;
    }
    
    // Initialize PCI device array
    memset(pci_devices, 0, sizeof(pci_device_t) * MAX_PCI_DEVICES);
    num_pci_devices = 0;
    
    // Scan all PCI buses
    int result = pci_enumerate_buses();
    if (result < 0) {
        log_error(PCI_TAG, "PCI bus enumeration failed: %d", result);
        heap_free(pci_devices);
        return result;
    }
    
    log_info(PCI_TAG, "Found %d PCI devices", num_pci_devices);
    
    // Debug dump of all detected devices
    for (int i = 0; i < num_pci_devices; i++) {
        pci_dump_device_info(&pci_devices[i]);
    }
    
    log_info(PCI_TAG, "PCI subsystem initialized successfully");
    return 0;
}

/**
 * Shutdown the PCI subsystem
 */
void pci_shutdown(void) {
    log_info(PCI_TAG, "Shutting down PCI subsystem");
    
    if (pci_devices) {
        // Free device resources
        for (int i = 0; i < num_pci_devices; i++) {
            if (pci_devices[i].driver && pci_devices[i].driver->ops.remove) {
                pci_devices[i].driver->ops.remove(&pci_devices[i]);
            }
        }
        
        heap_free(pci_devices);
        pci_devices = NULL;
        num_pci_devices = 0;
    }
}

/**
 * Register a PCI driver with the system
 */
int pci_register_driver(pci_driver_t* driver) {
    if (!driver) {
        log_error(PCI_TAG, "Attempted to register NULL driver");
        return -1;
    }
    
    log_info(PCI_TAG, "Registering PCI driver: %s", driver->name);
    
    // Add to driver list
    driver->next = pci_drivers;
    pci_drivers = driver;
    
    // Try to match this driver with already discovered devices
    int matched = 0;
    for (int i = 0; i < num_pci_devices; i++) {
        if (!pci_devices[i].driver) {
            if (pci_match_device_to_driver(&pci_devices[i]) == 0) {
                matched++;
            }
        }
    }
    
    log_info(PCI_TAG, "Driver %s matched %d devices", driver->name, matched);
    return 0;
}

/**
 * Unregister a PCI driver from the system
 */
int pci_unregister_driver(pci_driver_t* driver) {
    if (!driver) {
        log_error(PCI_TAG, "Attempted to unregister NULL driver");
        return -1;
    }
    
    log_info(PCI_TAG, "Unregistering PCI driver: %s", driver->name);
    
    // First remove driver from all devices using it
    for (int i = 0; i < num_pci_devices; i++) {
        if (pci_devices[i].driver == driver) {
            if (driver->ops.remove) {
                driver->ops.remove(&pci_devices[i]);
            }
            pci_devices[i].driver = NULL;
            pci_devices[i].private_data = NULL;
        }
    }
    
    // Then remove from driver list
    if (pci_drivers == driver) {
        // Driver is at the head of the list
        pci_drivers = driver->next;
    } else {
        // Find driver in the list
        pci_driver_t* prev = pci_drivers;
        while (prev && prev->next != driver) {
            prev = prev->next;
        }
        
        if (prev && prev->next == driver) {
            prev->next = driver->next;
        } else {
            log_warning(PCI_TAG, "Driver %s not found in driver list", driver->name);
        }
    }
    
    return 0;
}

/**
 * Find a PCI device by vendor and device ID
 */
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* start) {
    int start_idx = 0;
    
    // If a start point is specified, find its index
    if (start) {
        for (int i = 0; i < num_pci_devices; i++) {
            if (&pci_devices[i] == start) {
                start_idx = i + 1;  // Start from the next device
                break;
            }
        }
    }
    
    // Search for matching device
    for (int i = start_idx; i < num_pci_devices; i++) {
        if (pci_devices[i].id.vendor_id == vendor_id && 
            pci_devices[i].id.device_id == device_id) {
            return &pci_devices[i];
        }
    }
    
    return NULL;
}

/**
 * Find a PCI device by class, subclass and programming interface
 */
pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, pci_device_t* start) {
    int start_idx = 0;
    
    // If a start point is specified, find its index
    if (start) {
        for (int i = 0; i < num_pci_devices; i++) {
            if (&pci_devices[i] == start) {
                start_idx = i + 1;  // Start from the next device
                break;
            }
        }
    }
    
    // Search for matching device
    for (int i = start_idx; i < num_pci_devices; i++) {
        if (pci_devices[i].id.class_code == class_code && 
            pci_devices[i].id.subclass == subclass &&
            (prog_if < 0 || pci_devices[i].id.prog_if == (uint8_t)prog_if)) {
            return &pci_devices[i];
        }
    }
    
    return NULL;
}

/**
 * Find a PCI device by its bus, device, and function numbers
 */
pci_device_t* pci_find_by_location(uint8_t bus, uint8_t device, uint8_t function) {
    for (int i = 0; i < num_pci_devices; i++) {
        if (pci_devices[i].id.bus == bus && 
            pci_devices[i].id.device == device &&
            pci_devices[i].id.function == function) {
            return &pci_devices[i];
        }
    }
    
    return NULL;
}

/**
 * Enable PCI bus mastering for a device
 */
void pci_enable_bus_mastering(pci_device_t* dev) {
    uint16_t command = pci_read_config16(dev, PCI_REG_COMMAND);
    command |= PCI_CMD_BUS_MASTER;
    pci_write_config16(dev, PCI_REG_COMMAND, command);
}

/**
 * Enable memory space access for a device
 */
void pci_enable_memory_space(pci_device_t* dev) {
    uint16_t command = pci_read_config16(dev, PCI_REG_COMMAND);
    command |= PCI_CMD_MEMORY_SPACE;
    pci_write_config16(dev, PCI_REG_COMMAND, command);
}

/**
 * Enable I/O space access for a device
 */
void pci_enable_io_space(pci_device_t* dev) {
    uint16_t command = pci_read_config16(dev, PCI_REG_COMMAND);
    command |= PCI_CMD_IO_SPACE;
    pci_write_config16(dev, PCI_REG_COMMAND, command);
}

/**
 * Get the base address and size of a BAR
 */
int pci_get_bar_info(pci_device_t* dev, int bar_num, uint32_t* base, uint32_t* size, bool* is_io) {
    if (!dev || bar_num < 0 || bar_num > 5 || !base || !size || !is_io) {
        return -1;
    }
    
    *base = dev->id.bar[bar_num];
    *size = dev->id.bar_size[bar_num];
    *is_io = dev->id.bar_is_io[bar_num];
    
    return 0;
}

/**
 * Read 8-bit value from PCI configuration space
 */
uint8_t pci_read_config8(pci_device_t* dev, uint8_t offset) {
    uint32_t value = hal_pci_read_config(dev->id.bus, dev->id.device, dev->id.function, offset);
    return (uint8_t)(value >> ((offset & 3) * 8));
}

/**
 * Read 16-bit value from PCI configuration space
 */
uint16_t pci_read_config16(pci_device_t* dev, uint8_t offset) {
    uint32_t value = hal_pci_read_config(dev->id.bus, dev->id.device, dev->id.function, offset);
    return (uint16_t)(value >> ((offset & 2) * 8));
}

/**
 * Read 32-bit value from PCI configuration space
 */
uint32_t pci_read_config32(pci_device_t* dev, uint8_t offset) {
    return hal_pci_read_config(dev->id.bus, dev->id.device, dev->id.function, offset);
}

/**
 * Write 8-bit value to PCI configuration space
 */
void pci_write_config8(pci_device_t* dev, uint8_t offset, uint8_t value) {
    uint32_t old = hal_pci_read_config(dev->id.bus, dev->id.device, dev->id.function, offset & ~3);
    uint32_t shift = (offset & 3) * 8;
    uint32_t mask = ~(0xFF << shift);
    uint32_t new_value = (old & mask) | ((uint32_t)value << shift);
    
    hal_pci_write_config(dev->id.bus, dev->id.device, dev->id.function, offset & ~3, new_value);
}

/**
 * Write 16-bit value to PCI configuration space
 */
void pci_write_config16(pci_device_t* dev, uint8_t offset, uint16_t value) {
    uint32_t old = hal_pci_read_config(dev->id.bus, dev->id.device, dev->id.function, offset & ~3);
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = ~(0xFFFF << shift);
    uint32_t new_value = (old & mask) | ((uint32_t)value << shift);
    
    hal_pci_write_config(dev->id.bus, dev->id.device, dev->id.function, offset & ~3, new_value);
}

/**
 * Write 32-bit value to PCI configuration space
 */
void pci_write_config32(pci_device_t* dev, uint8_t offset, uint32_t value) {
    hal_pci_write_config(dev->id.bus, dev->id.device, dev->id.function, offset, value);
}

/**
 * Enumerate all PCI buses
 */
static int pci_enumerate_buses(void) {
    log_debug(PCI_TAG, "Enumerating PCI buses");
    
    // Check if we have a multi-function PCI host controller at 0:0:0
    uint32_t header_type = hal_pci_read_config(0, 0, 0, PCI_REG_HEADER_TYPE);
    
    if ((header_type & PCI_HEADER_TYPE_MULTI_FUNCTION) != 0) {
        // Multi-function host, check all possible buses
        for (int i = 0; i < PCI_MAX_BUSES; i++) {
            pci_enumerate_bus(i);
        }
    } else {
        // Single function host, check only bus 0
        pci_enumerate_bus(0);
    }
    
    return 0;
}

/**
 * Enumerate a single PCI bus
 */
static int pci_enumerate_bus(uint8_t bus) {
    for (int dev = 0; dev < PCI_MAX_DEVICES; dev++) {
        pci_enumerate_device(bus, dev);
    }
    return 0;
}

/**
 * Enumerate a single PCI device
 */
static int pci_enumerate_device(uint8_t bus, uint8_t device) {
    // Check if device exists by reading its vendor ID
    uint32_t vendor_device = hal_pci_read_config(bus, device, 0, 0);
    if ((vendor_device & 0xFFFF) == 0xFFFF) {
        return 0;  // No device at this position
    }
    
    // Check header type to determine if multi-function
    uint8_t header_type = (hal_pci_read_config(bus, device, 0, PCI_REG_HEADER_TYPE) & 0xFF);
    bool multi_function = (header_type & PCI_HEADER_TYPE_MULTI_FUNCTION) != 0;
    
    // Enumerate function 0
    pci_enumerate_function(bus, device, 0);
    
    // If multi-function, check additional functions
    if (multi_function) {
        for (uint8_t function = 1; function < PCI_MAX_FUNCTIONS; function++) {
            vendor_device = hal_pci_read_config(bus, device, function, 0);
            if ((vendor_device & 0xFFFF) != 0xFFFF) {
                pci_enumerate_function(bus, device, function);
            }
        }
    }
    
    return 0;
}

/**
 * Enumerate a single PCI function
 */
static int pci_enumerate_function(uint8_t bus, uint8_t device, uint8_t function) {
    // Allocate a new PCI device structure
    pci_device_t* dev = pci_alloc_device();
    if (!dev) {
        return -1;
    }
    
    // Fill in basic device information
    pci_device_id_t* id = &dev->id;
    id->bus = bus;
    id->device = device;
    id->function = function;
    
    // Read device identification
    uint32_t vendor_device = hal_pci_read_config(bus, device, function, 0);
    id->vendor_id = vendor_device & 0xFFFF;
    id->device_id = (vendor_device >> 16) & 0xFFFF;
    
    uint32_t class_rev = hal_pci_read_config(bus, device, function, 8);
    id->revision = class_rev & 0xFF;
    id->prog_if = (class_rev >> 8) & 0xFF;
    id->subclass = (class_rev >> 16) & 0xFF;
    id->class_code = (class_rev >> 24) & 0xFF;
    
    uint32_t header = hal_pci_read_config(bus, device, function, PCI_REG_HEADER_TYPE);
    id->header_type = (header & 0xFF);
    
    // Read interrupt information
    uint32_t interrupt_info = hal_pci_read_config(bus, device, function, PCI_REG_INTERRUPT_LINE);
    id->interrupt_line = interrupt_info & 0xFF;
    id->interrupt_pin = (interrupt_info >> 8) & 0xFF;
    
    // Read BAR information
    for (int i = 0; i < 6; i++) {
        uint8_t bar_offset = PCI_REG_BAR0 + (i * 4);
        id->bar[i] = hal_pci_read_config(bus, device, function, bar_offset);
        id->bar_is_io[i] = (id->bar[i] & 0x01) != 0;
    }
    
    // Detect BAR sizes
    pci_detect_bar_sizes(id);
    
    // Generate a name for the device
    const char* class_name = pci_class_to_string(id->class_code, id->subclass);
    snprintf(dev->name, sizeof(dev->name), "PCI %s (%04X:%04X)", 
             class_name, id->vendor_id, id->device_id);
    
    // Try to match this device with a driver
    pci_match_device_to_driver(dev);
    
    return 0;
}

/**
 * Detect the sizes of BAR regions
 */
static void pci_detect_bar_sizes(pci_device_id_t* id) {
    for (int i = 0; i < 6; i++) {
        uint8_t bar_offset = PCI_REG_BAR0 + (i * 4);
        uint32_t orig_bar = hal_pci_read_config(id->bus, id->device, id->function, bar_offset);
        
        if (orig_bar == 0) {
            // This BAR is not implemented
            id->bar_size[i] = 0;
            continue;
        }
        
        // Write all 1s to the BAR to get its size
        hal_pci_write_config(id->bus, id->device, id->function, bar_offset, 0xFFFFFFFF);
        
        // Read back the BAR value
        uint32_t size_bar = hal_pci_read_config(id->bus, id->device, id->function, bar_offset);
        
        // Restore original BAR value
        hal_pci_write_config(id->bus, id->device, id->function, bar_offset, orig_bar);
        
        // Calculate size based on the bits that can be modified
        if (id->bar_is_io[i]) {
            // I/O space BAR
            id->bar_size[i] = ~(size_bar & ~0x3) + 1;
        } else {
            // Memory space BAR
            uint8_t bar_type = (orig_bar >> 1) & 0x3;
            
            if (bar_type == 0) {  // 32-bit BAR
                id->bar_size[i] = ~(size_bar & ~0xF) + 1;
            } else if (bar_type == 2) {  // 64-bit BAR
                // For 64-bit BAR we need to combine with the next one
                // Just approximate for now
                id->bar_size[i] = ~(size_bar & ~0xF) + 1;
                
                // Skip the next BAR as it's the high 32 bits of this 64-bit BAR
                if (i + 1 < 6) {
                    id->bar_size[i + 1] = 0;
                    i++;
                }
            } else {
                // Unsupported BAR type
                id->bar_size[i] = 0;
            }
        }
    }
}

/**
 * Try to match a PCI device with a driver
 */
static int pci_match_device_to_driver(pci_device_t* dev) {
    if (!dev || dev->driver) {
        return 0;  // Device already has a driver
    }
    
    // Try to match with registered drivers
    pci_driver_t* driver = pci_drivers;
    while (driver) {
        bool match = false;
        
        if (driver->vendor_ids && driver->device_ids) {
            // Match by vendor and device ID
            for (int i = 0; i < driver->num_supported_devices; i++) {
                if (driver->vendor_ids[i] == dev->id.vendor_id &&
                    driver->device_ids[i] == dev->id.device_id) {
                    match = true;
                    break;
                }
            }
        } else if (driver->class_codes && driver->subclasses) {
            // Match by class and subclass
            for (int i = 0; i < driver->num_supported_devices; i++) {
                if (driver->class_codes[i] == dev->id.class_code &&
                    driver->subclasses[i] == dev->id.subclass) {
                    match = true;
                    break;
                }
            }
        }
        
        if (match && driver->ops.probe && driver->ops.probe(dev) == 0) {
            // Driver accepts this device
            dev->driver = driver;
            
            log_info(PCI_TAG, "Device %s matched with driver %s", dev->name, driver->name);
            
            // Initialize the device with the driver
            if (driver->ops.init) {
                int result = driver->ops.init(dev);
                if (result != 0) {
                    log_error(PCI_TAG, "Driver %s failed to initialize device %s: %d",
                             driver->name, dev->name, result);
                    dev->driver = NULL;
                    dev->private_data = NULL;
                    return -1;
                }
            }
            
            return 0;
        }
        
        driver = driver->next;
    }
    
    log_debug(PCI_TAG, "No driver found for device %s", dev->name);
    return -1;
}

/**
 * Convert PCI class and subclass codes to a string description
 */
static const char* pci_class_to_string(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED:
            return "Unclassified";
        
        case PCI_CLASS_MASS_STORAGE:
            switch (subclass) {
                case PCI_SUBCLASS_STORAGE_SCSI: return "SCSI Controller";
                case PCI_SUBCLASS_STORAGE_IDE: return "IDE Controller";
                case PCI_SUBCLASS_STORAGE_FLOPPY: return "Floppy Controller";
                case PCI_SUBCLASS_STORAGE_IPI: return "IPI Controller";
                case PCI_SUBCLASS_STORAGE_RAID: return "RAID Controller";
                case PCI_SUBCLASS_STORAGE_ATA: return "ATA Controller";
                case PCI_SUBCLASS_STORAGE_SATA: return "SATA Controller";
                case PCI_SUBCLASS_STORAGE_SAS: return "SAS Controller";
                case PCI_SUBCLASS_STORAGE_NVM: return "NVM Controller";
                default: return "Storage Controller";
            }
            
        case PCI_CLASS_NETWORK:
            switch (subclass) {
                case PCI_SUBCLASS_NETWORK_ETHERNET: return "Ethernet Controller";
                case PCI_SUBCLASS_NETWORK_TOKEN_RING: return "Token Ring Controller";
                case PCI_SUBCLASS_NETWORK_FDDI: return "FDDI Controller";
                case PCI_SUBCLASS_NETWORK_ATM: return "ATM Controller";
                case PCI_SUBCLASS_NETWORK_ISDN: return "ISDN Controller";
                default: return "Network Controller";
            }
            
        case PCI_CLASS_DISPLAY:
            switch (subclass) {
                case PCI_SUBCLASS_DISPLAY_VGA: return "VGA Controller";
                case PCI_SUBCLASS_DISPLAY_XGA: return "XGA Controller";
                case PCI_SUBCLASS_DISPLAY_3D: return "3D Controller";
                default: return "Display Controller";
            }
            
        case PCI_CLASS_MULTIMEDIA:
            return "Multimedia Controller";
            
        case PCI_CLASS_MEMORY:
            return "Memory Controller";
            
        case PCI_CLASS_BRIDGE:
            return "Bridge Device";
            
        case PCI_CLASS_COMMUNICATION:
            return "Communication Controller";
            
        case PCI_CLASS_SYSTEM:
            return "System Peripheral";
            
        case PCI_CLASS_INPUT:
            return "Input Device";
            
        case PCI_CLASS_DOCKING:
            return "Docking Station";
            
        case PCI_CLASS_PROCESSOR:
            return "Processor";
            
        case PCI_CLASS_SERIAL_BUS:
            switch (subclass) {
                case PCI_SUBCLASS_SERIAL_FIREWIRE: return "FireWire Controller";
                case PCI_SUBCLASS_SERIAL_ACCESS: return "ACCESS Controller";
                case PCI_SUBCLASS_SERIAL_SSA: return "SSA Controller";
                case PCI_SUBCLASS_SERIAL_USB: return "USB Controller";
                case PCI_SUBCLASS_SERIAL_FIBRE: return "Fibre Channel";
                case PCI_SUBCLASS_SERIAL_SMBUS: return "SMBus Controller";
                case PCI_SUBCLASS_SERIAL_INFINIBAND: return "InfiniBand Controller";
                case PCI_SUBCLASS_SERIAL_IPMI: return "IPMI Controller";
                case PCI_SUBCLASS_SERIAL_SERCOS: return "SERCOS Controller";
                case PCI_SUBCLASS_SERIAL_CANBUS: return "CANbus Controller";
                default: return "Serial Bus Controller";
            }
            
        case PCI_CLASS_WIRELESS:
            return "Wireless Controller";
            
        case PCI_CLASS_INTELLIGENT_IO:
            return "Intelligent I/O Controller";
            
        case PCI_CLASS_SATELLITE:
            return "Satellite Controller";
            
        case PCI_CLASS_ENCRYPTION:
            return "Encryption Controller";
            
        case PCI_CLASS_ACQUISITION:
            return "Signal Processing Controller";
            
        default:
            return "Unknown Device";
    }
}

/**
 * Allocate a new PCI device structure
 */
static pci_device_t* pci_alloc_device(void) {
    if (num_pci_devices >= MAX_PCI_DEVICES) {
        log_error(PCI_TAG, "Maximum number of PCI devices reached");
        return NULL;
    }
    
    pci_device_t* dev = &pci_devices[num_pci_devices++];
    memset(dev, 0, sizeof(pci_device_t));
    return dev;
}

/**
 * Print information about a PCI device (for debugging)
 */
static void pci_dump_device_info(pci_device_t* dev) {
    pci_device_id_t* id = &dev->id;
    
    log_debug(PCI_TAG, "PCI %02x:%02x.%x: %04X:%04X Class %02x.%02x [%s]",
           id->bus, id->device, id->function,
           id->vendor_id, id->device_id, 
           id->class_code, id->subclass, dev->name);
    
    // Print BAR information
    for (int i = 0; i < 6; i++) {
        if (id->bar[i] != 0) {
            log_debug(PCI_TAG, "  BAR%d: %s 0x%08X, size: %d bytes", 
                   i, 
                   id->bar_is_io[i] ? "I/O" : "MEM",
                   id->bar[i] & (id->bar_is_io[i] ? ~0x3 : ~0xF),
                   id->bar_size[i]);
        }
    }
    
    // Print interrupt information if present
    if (id->interrupt_pin != 0) {
        log_debug(PCI_TAG, "  IRQ: %d (Pin %c)", 
               id->interrupt_line, 
               'A' + id->interrupt_pin - 1);
    }
}