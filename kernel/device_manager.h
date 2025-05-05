#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <stdint.h>
#include <stddef.h>

/**
 * Device Types
 */
#define DEVICE_TYPE_UNKNOWN     0x00  // Unknown device type
#define DEVICE_TYPE_BLOCK       0x01  // Block device (storage)
#define DEVICE_TYPE_CHAR        0x02  // Character device
#define DEVICE_TYPE_DISPLAY     0x03  // Display device
#define DEVICE_TYPE_INPUT       0x04  // Input device
#define DEVICE_TYPE_NETWORK     0x05  // Network device
#define DEVICE_TYPE_SOUND       0x06  // Sound device
#define DEVICE_TYPE_TIMER       0x07  // Timer device
#define DEVICE_TYPE_PROCESSOR   0x08  // CPU/processor
#define DEVICE_TYPE_BUS         0x09  // System bus
#define DEVICE_TYPE_MEMORY      0x0A  // Memory device
#define DEVICE_TYPE_DMA         0x0B  // DMA controller
#define DEVICE_TYPE_INTERRUPT   0x0C  // Interrupt controller
#define DEVICE_TYPE_PORT        0x0D  // I/O port
#define DEVICE_TYPE_BRIDGE      0x0E  // Bridge/adapter
#define DEVICE_TYPE_SERIAL      0x0F  // Serial device
#define DEVICE_TYPE_PARALLEL    0x10  // Parallel device
#define DEVICE_TYPE_STORAGE     0x11  // Storage controller
#define DEVICE_TYPE_USB         0x12  // USB controller or device
#define DEVICE_TYPE_PCI         0x13  // PCI device
#define DEVICE_TYPE_ACPI        0x14  // ACPI device
#define DEVICE_TYPE_VIRTUAL     0x15  // Virtual device

/**
 * Device Status Values
 */
#define DEVICE_STATUS_UNKNOWN   0x00  // Unknown status
#define DEVICE_STATUS_DISABLED  0x01  // Device is disabled
#define DEVICE_STATUS_ENABLED   0x02  // Device is enabled/active
#define DEVICE_STATUS_ERROR     0x03  // Device is in error state
#define DEVICE_STATUS_BUSY      0x04  // Device is busy
#define DEVICE_STATUS_STANDBY   0x05  // Device is in standby/low power
#define DEVICE_STATUS_OFFLINE   0x06  // Device is offline
#define DEVICE_STATUS_MISSING   0x07  // Device is missing

/**
 * Device Capability Flags
 */
#define DEVICE_CAP_NONE         0x0000  // No special capabilities
#define DEVICE_CAP_DMA          0x0001  // Supports DMA
#define DEVICE_CAP_IRQ          0x0002  // Supports interrupts
#define DEVICE_CAP_MMIO         0x0004  // Supports memory-mapped I/O
#define DEVICE_CAP_PIO          0x0008  // Supports port I/O
#define DEVICE_CAP_BUS_MASTER   0x0010  // Can be a bus master
#define DEVICE_CAP_POWER_MGMT   0x0020  // Supports power management
#define DEVICE_CAP_HOT_PLUG     0x0040  // Supports hot-plugging
#define DEVICE_CAP_SHAREABLE    0x0080  // Can be shared by multiple users
#define DEVICE_CAP_VIRTUALIZED  0x0100  // Device is virtualized

/**
 * Device Operation Return Codes
 */
#define DEVICE_OK               0      // Operation completed successfully
#define DEVICE_ERROR_GENERAL   -1      // General error
#define DEVICE_ERROR_INVALID   -2      // Invalid parameter
#define DEVICE_ERROR_BUSY      -3      // Device is busy
#define DEVICE_ERROR_TIMEOUT   -4      // Operation timed out
#define DEVICE_ERROR_UNSUPPORTED -5    // Operation not supported
#define DEVICE_ERROR_NO_MEDIA  -6      // No media present
#define DEVICE_ERROR_IO        -7      // I/O error
#define DEVICE_ERROR_NO_DEVICE -8      // Device not found
#define DEVICE_ERROR_ACCESS    -9      // Access denied
#define DEVICE_ERROR_RESOURCE  -10     // Resource allocation failed

/**
 * Device Flags
 */
#define DEVICE_FLAG_NONE        0x0000  // No flags
#define DEVICE_FLAG_ROOT        0x0001  // Root device
#define DEVICE_FLAG_VIRTUAL     0x0002  // Virtual device
#define DEVICE_FLAG_REMOVABLE   0x0004  // Removable device
#define DEVICE_FLAG_BOOT        0x0008  // Boot device
#define DEVICE_FLAG_RAW         0x0010  // Raw device access
#define DEVICE_FLAG_SYSTEM      0x0020  // System device
#define DEVICE_FLAG_LEGACY      0x0040  // Legacy device
#define DEVICE_FLAG_USER        0x0080  // User-accessible device
#define DEVICE_FLAG_ADMIN       0x0100  // Admin-only access

/**
 * Forward declarations
 */
struct device;
struct device_driver;
struct device_class;
struct device_operations;

/**
 * Device Operations Structure
 */
typedef struct device_operations {
    int (*probe)(struct device *dev);  // Probe device
    int (*init)(struct device *dev);   // Initialize device
    int (*shutdown)(struct device *dev); // Shutdown device
    int (*suspend)(struct device *dev); // Suspend device
    int (*resume)(struct device *dev);  // Resume device
    int (*remove)(struct device *dev);  // Remove device
    int (*open)(struct device *dev, uint32_t flags); // Open device
    int (*close)(struct device *dev);  // Close device
    int (*read)(struct device *dev, void *buffer, size_t size, uint64_t offset); // Read from device
    int (*write)(struct device *dev, const void *buffer, size_t size, uint64_t offset); // Write to device
    int (*ioctl)(struct device *dev, int request, void *arg); // I/O control
    int (*mmap)(struct device *dev, void *addr, size_t length, int prot, int flags, uint64_t offset); // Memory map
    int (*poll)(struct device *dev, int events); // Poll for events
} device_ops_t;

/**
 * Device Structure
 */
typedef struct device {
    char name[32];                 // Device name
    char path[64];                 // Device path in device tree
    uint32_t id;                   // Device ID
    uint8_t type;                  // Device type
    uint8_t status;                // Device status
    uint16_t flags;                // Device flags
    uint32_t capabilities;         // Device capabilities
    
    // Hardware information
    uint16_t vendor_id;            // Vendor ID
    uint16_t device_id;            // Device ID
    uint8_t class_code;            // Class code
    uint8_t subclass_code;         // Subclass code
    uint8_t prog_if;               // Programming Interface
    uint8_t revision;              // Revision ID
    
    // Resources
    uint32_t mem_base;             // Memory base address
    uint32_t mem_size;             // Memory size
    uint16_t io_base;              // I/O port base address
    uint16_t io_size;              // I/O port range size
    uint8_t irq;                   // IRQ number
    uint8_t dma_channel;           // DMA channel
    
    // Relationships
    struct device *parent;         // Parent device
    struct device **children;      // Child devices
    int num_children;              // Number of children
    struct device_driver *driver;  // Associated driver
    struct device_class *class;    // Device class
    
    // Operations
    device_ops_t *ops;             // Device operations
    
    // Private data
    void *private_data;            // Private driver data
    void *platform_data;           // Platform-specific data
} device_t;

/**
 * Device Driver Structure
 */
typedef struct device_driver {
    char name[32];                 // Driver name
    uint32_t id;                   // Driver ID
    uint32_t version;              // Driver version
    
    // Supported devices
    uint16_t *vendor_ids;          // Supported vendor IDs
    uint16_t *device_ids;          // Supported device IDs
    int num_supported_devices;     // Number of supported devices
    
    // Operations
    device_ops_t ops;              // Device operations
    
    // Driver management
    int (*probe)(struct device *dev); // Probe for devices
    int (*init)(void);              // Initialize driver
    int (*exit)(void);              // Clean up driver
    
    // List management
    struct device_driver *next;    // Next driver in list
} device_driver_t;

/**
 * Device Class Structure
 */
typedef struct device_class {
    char name[32];                 // Class name
    uint32_t id;                   // Class ID
    
    // Default operations for this class
    device_ops_t ops;              // Default operations
    
    // Class management
    int (*init)(struct device_class *class); // Initialize class
    int (*exit)(struct device_class *class); // Clean up class
    
    // List management
    struct device_class *next;     // Next class in list
} device_class_t;

/**
 * Initialize the device manager
 *
 * @return DEVICE_OK on success, error code on failure
 */
int device_manager_init(void);

/**
 * Register a device with the device manager
 *
 * @param dev Pointer to the device structure
 * @return DEVICE_OK on success, error code on failure
 */
int device_register(device_t *dev);

/**
 * Unregister a device from the device manager
 *
 * @param dev Pointer to the device structure
 * @return DEVICE_OK on success, error code on failure
 */
int device_unregister(device_t *dev);

/**
 * Find a device by name
 *
 * @param name Device name
 * @return Pointer to the device, or NULL if not found
 */
device_t *device_find_by_name(const char *name);

/**
 * Find a device by ID
 *
 * @param id Device ID
 * @return Pointer to the device, or NULL if not found
 */
device_t *device_find_by_id(uint32_t id);

/**
 * Find devices by type
 *
 * @param type Device type
 * @param devices Array to store found devices
 * @param max_devices Maximum number of devices to return
 * @return Number of devices found
 */
int device_find_by_type(uint8_t type, device_t **devices, int max_devices);

/**
 * Register a device driver
 *
 * @param driver Pointer to the driver structure
 * @return DEVICE_OK on success, error code on failure
 */
int device_driver_register(device_driver_t *driver);

/**
 * Unregister a device driver
 *
 * @param driver Pointer to the driver structure
 * @return DEVICE_OK on success, error code on failure
 */
int device_driver_unregister(device_driver_t *driver);

/**
 * Find a driver for a specific device
 *
 * @param dev Pointer to the device
 * @return Pointer to the driver, or NULL if no compatible driver found
 */
device_driver_t *device_find_driver(device_t *dev);

/**
 * Register a device class
 *
 * @param class Pointer to the class structure
 * @return DEVICE_OK on success, error code on failure
 */
int device_class_register(device_class_t *class);

/**
 * Unregister a device class
 *
 * @param class Pointer to the class structure
 * @return DEVICE_OK on success, error code on failure
 */
int device_class_unregister(device_class_t *class);

/**
 * Find a device class by name
 *
 * @param name Class name
 * @return Pointer to the class, or NULL if not found
 */
device_class_t *device_class_find_by_name(const char *name);

/**
 * Find a device class by ID
 *
 * @param id Class ID
 * @return Pointer to the class, or NULL if not found
 */
device_class_t *device_class_find_by_id(uint32_t id);

/**
 * Open a device
 *
 * @param dev Pointer to the device
 * @param flags Open flags
 * @return DEVICE_OK on success, error code on failure
 */
int device_open(device_t *dev, uint32_t flags);

/**
 * Close a device
 *
 * @param dev Pointer to the device
 * @return DEVICE_OK on success, error code on failure
 */
int device_close(device_t *dev);

/**
 * Read from a device
 *
 * @param dev Pointer to the device
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @param offset Offset in device
 * @return Number of bytes read, or negative error code
 */
int device_read(device_t *dev, void *buffer, size_t size, uint64_t offset);

/**
 * Write to a device
 *
 * @param dev Pointer to the device
 * @param buffer Buffer to write from
 * @param size Number of bytes to write
 * @param offset Offset in device
 * @return Number of bytes written, or negative error code
 */
int device_write(device_t *dev, const void *buffer, size_t size, uint64_t offset);

/**
 * Perform I/O control on a device
 *
 * @param dev Pointer to the device
 * @param request I/O control request
 * @param arg Argument for the request
 * @return DEVICE_OK on success, error code on failure
 */
int device_ioctl(device_t *dev, int request, void *arg);

/**
 * Create a device node
 *
 * @param name Device name
 * @param dev Pointer to the device
 * @param mode Access mode
 * @return DEVICE_OK on success, error code on failure
 */
int device_create_node(const char *name, device_t *dev, uint32_t mode);

/**
 * Remove a device node
 *
 * @param name Device name
 * @return DEVICE_OK on success, error code on failure
 */
int device_remove_node(const char *name);

/**
 * Print device tree (for debugging)
 */
void device_print_tree(void);

#endif /* DEVICE_MANAGER_H */