/**
 * @file pci.h
 * @brief PCI bus driver framework for uintOS
 *
 * This module provides a framework for detecting, enumerating,
 * and managing PCI devices in the system.
 */

#ifndef UINTOS_PCI_H
#define UINTOS_PCI_H

#include <stdint.h>
#include <stdbool.h>
#include "../../kernel/device_manager.h"
#include "../../hal/include/hal_io.h"

// PCI configuration space registers
#define PCI_REG_VENDOR_ID          0x00
#define PCI_REG_DEVICE_ID          0x02
#define PCI_REG_COMMAND            0x04
#define PCI_REG_STATUS             0x06
#define PCI_REG_REVISION_ID        0x08
#define PCI_REG_PROG_IF            0x09
#define PCI_REG_SUBCLASS           0x0A
#define PCI_REG_CLASS              0x0B
#define PCI_REG_CACHE_LINE_SIZE    0x0C
#define PCI_REG_LATENCY_TIMER      0x0D
#define PCI_REG_HEADER_TYPE        0x0E
#define PCI_REG_BIST               0x0F
#define PCI_REG_BAR0               0x10
#define PCI_REG_BAR1               0x14
#define PCI_REG_BAR2               0x18
#define PCI_REG_BAR3               0x1C
#define PCI_REG_BAR4               0x20
#define PCI_REG_BAR5               0x24
#define PCI_REG_CARDBUS_CIS_PTR    0x28
#define PCI_REG_SUBSYSTEM_VENDOR   0x2C
#define PCI_REG_SUBSYSTEM_ID       0x2E
#define PCI_REG_EXPANSION_ROM      0x30
#define PCI_REG_CAPABILITIES       0x34
#define PCI_REG_INTERRUPT_LINE     0x3C
#define PCI_REG_INTERRUPT_PIN      0x3D
#define PCI_REG_MIN_GRANT          0x3E
#define PCI_REG_MAX_LATENCY        0x3F

// PCI command register bits
#define PCI_CMD_IO_SPACE           0x0001
#define PCI_CMD_MEMORY_SPACE       0x0002
#define PCI_CMD_BUS_MASTER         0x0004
#define PCI_CMD_SPECIAL_CYCLES     0x0008
#define PCI_CMD_MEM_WRITE_ENABLE   0x0010
#define PCI_CMD_VGA_PALETTE_SNOOP  0x0020
#define PCI_CMD_PARITY_ERROR       0x0040
#define PCI_CMD_SERR_ENABLE        0x0100
#define PCI_CMD_FAST_BACK_TO_BACK  0x0200
#define PCI_CMD_INTERRUPT_DISABLE  0x0400

// PCI status register bits
#define PCI_STATUS_CAPABILITY_LIST 0x0010
#define PCI_STATUS_66MHZ           0x0020
#define PCI_STATUS_FAST_BACK_TO_BACK 0x0080
#define PCI_STATUS_MASTER_PARITY_ERROR 0x0100
#define PCI_STATUS_DEVSEL_MASK     0x0600
#define PCI_STATUS_SIGNALED_TARGET_ABORT 0x0800
#define PCI_STATUS_RECEIVED_TARGET_ABORT 0x1000
#define PCI_STATUS_RECEIVED_MASTER_ABORT 0x2000
#define PCI_STATUS_SIGNALED_SYSTEM_ERROR 0x4000
#define PCI_STATUS_DETECTED_PARITY_ERROR 0x8000

// PCI header types
#define PCI_HEADER_TYPE_NORMAL     0x00
#define PCI_HEADER_TYPE_BRIDGE     0x01
#define PCI_HEADER_TYPE_CARDBUS    0x02
#define PCI_HEADER_TYPE_MULTI_FUNCTION 0x80

// PCI class codes
#define PCI_CLASS_UNCLASSIFIED     0x00
#define PCI_CLASS_MASS_STORAGE     0x01
#define PCI_CLASS_NETWORK          0x02
#define PCI_CLASS_DISPLAY          0x03
#define PCI_CLASS_MULTIMEDIA       0x04
#define PCI_CLASS_MEMORY           0x05
#define PCI_CLASS_BRIDGE           0x06
#define PCI_CLASS_COMMUNICATION    0x07
#define PCI_CLASS_SYSTEM           0x08
#define PCI_CLASS_INPUT            0x09
#define PCI_CLASS_DOCKING          0x0A
#define PCI_CLASS_PROCESSOR        0x0B
#define PCI_CLASS_SERIAL_BUS       0x0C
#define PCI_CLASS_WIRELESS         0x0D
#define PCI_CLASS_INTELLIGENT_IO   0x0E
#define PCI_CLASS_SATELLITE        0x0F
#define PCI_CLASS_ENCRYPTION       0x10
#define PCI_CLASS_ACQUISITION      0x11
#define PCI_CLASS_UNDEFINED        0xFF

// Mass Storage subclasses
#define PCI_SUBCLASS_STORAGE_SCSI  0x00
#define PCI_SUBCLASS_STORAGE_IDE   0x01
#define PCI_SUBCLASS_STORAGE_FLOPPY 0x02
#define PCI_SUBCLASS_STORAGE_IPI   0x03
#define PCI_SUBCLASS_STORAGE_RAID  0x04
#define PCI_SUBCLASS_STORAGE_ATA   0x05
#define PCI_SUBCLASS_STORAGE_SATA  0x06
#define PCI_SUBCLASS_STORAGE_SAS   0x07
#define PCI_SUBCLASS_STORAGE_NVM   0x08
#define PCI_SUBCLASS_STORAGE_OTHER 0x80

// Network subclasses
#define PCI_SUBCLASS_NETWORK_ETHERNET 0x00
#define PCI_SUBCLASS_NETWORK_TOKEN_RING 0x01
#define PCI_SUBCLASS_NETWORK_FDDI   0x02
#define PCI_SUBCLASS_NETWORK_ATM    0x03
#define PCI_SUBCLASS_NETWORK_ISDN   0x04
#define PCI_SUBCLASS_NETWORK_WORLDFIP 0x05
#define PCI_SUBCLASS_NETWORK_PICMG  0x06
#define PCI_SUBCLASS_NETWORK_OTHER  0x80

// Display subclasses
#define PCI_SUBCLASS_DISPLAY_VGA   0x00
#define PCI_SUBCLASS_DISPLAY_XGA   0x01
#define PCI_SUBCLASS_DISPLAY_3D    0x02
#define PCI_SUBCLASS_DISPLAY_OTHER 0x80

// Serial Bus subclasses
#define PCI_SUBCLASS_SERIAL_FIREWIRE 0x00
#define PCI_SUBCLASS_SERIAL_ACCESS   0x01
#define PCI_SUBCLASS_SERIAL_SSA      0x02
#define PCI_SUBCLASS_SERIAL_USB      0x03
#define PCI_SUBCLASS_SERIAL_FIBRE    0x04
#define PCI_SUBCLASS_SERIAL_SMBUS    0x05
#define PCI_SUBCLASS_SERIAL_INFINIBAND 0x06
#define PCI_SUBCLASS_SERIAL_IPMI     0x07
#define PCI_SUBCLASS_SERIAL_SERCOS   0x08
#define PCI_SUBCLASS_SERIAL_CANBUS   0x09
#define PCI_SUBCLASS_SERIAL_OTHER    0x80

// Maximum PCI buses, devices, and functions
#define PCI_MAX_BUSES              256
#define PCI_MAX_DEVICES            32
#define PCI_MAX_FUNCTIONS          8

/**
 * PCI device identification structure
 */
typedef struct {
    uint8_t bus;               // Bus number
    uint8_t device;            // Device number
    uint8_t function;          // Function number
    uint16_t vendor_id;        // Vendor ID
    uint16_t device_id;        // Device ID
    uint8_t class_code;        // Class code
    uint8_t subclass;          // Subclass code
    uint8_t prog_if;           // Programming interface
    uint8_t revision;          // Revision ID
    uint8_t header_type;       // Header type
    uint8_t interrupt_line;    // Interrupt line
    uint8_t interrupt_pin;     // Interrupt pin
    uint32_t bar[6];           // Base Address Registers
    bool bar_is_io[6];         // BAR is I/O space (not memory)
    uint32_t bar_size[6];      // Size of BAR regions
} pci_device_id_t;

/**
 * PCI device structure
 */
typedef struct pci_device {
    pci_device_id_t id;        // Device identification
    char name[32];             // Device name
    device_t* os_device;       // Pointer to OS device structure
    struct pci_driver* driver; // Associated PCI driver
    void* private_data;        // Driver-specific private data
} pci_device_t;

/**
 * PCI driver operations
 */
typedef struct {
    int (*probe)(pci_device_t* dev);  // Check if this driver can handle the device
    int (*init)(pci_device_t* dev);   // Initialize the device
    int (*remove)(pci_device_t* dev); // Remove the device
    int (*suspend)(pci_device_t* dev);// Suspend the device
    int (*resume)(pci_device_t* dev); // Resume the device
} pci_driver_ops_t;

/**
 * PCI driver structure
 */
typedef struct pci_driver {
    char name[32];                   // Driver name
    uint16_t* vendor_ids;            // Supported vendor IDs (NULL = any)
    uint16_t* device_ids;            // Supported device IDs (NULL = any)
    uint8_t* class_codes;            // Supported class codes (NULL = any)
    uint8_t* subclasses;             // Supported subclasses (NULL = any)
    int num_supported_devices;       // Number of supported devices
    pci_driver_ops_t ops;            // Driver operations
    struct pci_driver* next;         // Next driver in list
} pci_driver_t;

/**
 * Initialize the PCI subsystem
 * 
 * @return 0 on success, negative error code on failure
 */
int pci_init(void);

/**
 * Shutdown the PCI subsystem
 */
void pci_shutdown(void);

/**
 * Register a PCI driver with the system
 * 
 * @param driver The driver to register
 * @return 0 on success, negative error code on failure
 */
int pci_register_driver(pci_driver_t* driver);

/**
 * Unregister a PCI driver from the system
 * 
 * @param driver The driver to unregister
 * @return 0 on success, negative error code on failure
 */
int pci_unregister_driver(pci_driver_t* driver);

/**
 * Find a PCI device by vendor and device ID
 * 
 * @param vendor_id The vendor ID to search for
 * @param device_id The device ID to search for
 * @param start Start searching from this device (NULL to start from beginning)
 * @return Pointer to found device, or NULL if not found
 */
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* start);

/**
 * Find a PCI device by class, subclass and programming interface
 * 
 * @param class_code The class code to search for
 * @param subclass The subclass to search for
 * @param prog_if The programming interface to search for (-1 for any)
 * @param start Start searching from this device (NULL to start from beginning)
 * @return Pointer to found device, or NULL if not found
 */
pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, pci_device_t* start);

/**
 * Find a PCI device by its bus, device, and function numbers
 * 
 * @param bus The bus number
 * @param device The device number
 * @param function The function number
 * @return Pointer to found device, or NULL if not found
 */
pci_device_t* pci_find_by_location(uint8_t bus, uint8_t device, uint8_t function);

/**
 * Enable PCI bus mastering for a device
 * 
 * @param dev The PCI device
 */
void pci_enable_bus_mastering(pci_device_t* dev);

/**
 * Enable memory space access for a device
 * 
 * @param dev The PCI device
 */
void pci_enable_memory_space(pci_device_t* dev);

/**
 * Enable I/O space access for a device
 * 
 * @param dev The PCI device
 */
void pci_enable_io_space(pci_device_t* dev);

/**
 * Get the base address and size of a BAR
 * 
 * @param dev The PCI device
 * @param bar_num BAR number (0-5)
 * @param base Pointer to receive the base address
 * @param size Pointer to receive the size
 * @param is_io Pointer to receive whether the BAR is I/O (true) or memory (false)
 * @return 0 on success, negative error code on failure
 */
int pci_get_bar_info(pci_device_t* dev, int bar_num, uint32_t* base, uint32_t* size, bool* is_io);

/**
 * Read 8-bit value from PCI configuration space
 * 
 * @param dev The PCI device
 * @param offset Offset in configuration space
 * @return The value read
 */
uint8_t pci_read_config8(pci_device_t* dev, uint8_t offset);

/**
 * Read 16-bit value from PCI configuration space
 * 
 * @param dev The PCI device
 * @param offset Offset in configuration space
 * @return The value read
 */
uint16_t pci_read_config16(pci_device_t* dev, uint8_t offset);

/**
 * Read 32-bit value from PCI configuration space
 * 
 * @param dev The PCI device
 * @param offset Offset in configuration space
 * @return The value read
 */
uint32_t pci_read_config32(pci_device_t* dev, uint8_t offset);

/**
 * Write 8-bit value to PCI configuration space
 * 
 * @param dev The PCI device
 * @param offset Offset in configuration space
 * @param value The value to write
 */
void pci_write_config8(pci_device_t* dev, uint8_t offset, uint8_t value);

/**
 * Write 16-bit value to PCI configuration space
 * 
 * @param dev The PCI device
 * @param offset Offset in configuration space
 * @param value The value to write
 */
void pci_write_config16(pci_device_t* dev, uint8_t offset, uint16_t value);

/**
 * Write 32-bit value to PCI configuration space
 * 
 * @param dev The PCI device
 * @param offset Offset in configuration space
 * @param value The value to write
 */
void pci_write_config32(pci_device_t* dev, uint8_t offset, uint32_t value);

#endif /* UINTOS_PCI_H */