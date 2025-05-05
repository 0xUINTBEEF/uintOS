#include "device_manager.h"
#include "logging/log.h"
#include "memory/heap.h"
#include <string.h>

/**
 * Device Manager Implementation for uintOS
 */

// Maximum number of devices the system can handle
#define MAX_DEVICES 256

// Device tree structures
static device_t *devices[MAX_DEVICES];
static int num_devices = 0;
static device_driver_t *drivers = NULL;
static device_class_t *classes = NULL;

// Root device in the device tree
static device_t *root_device = NULL;

// Internal functions
static void device_init_root(void);
static int device_add_child(device_t *parent, device_t *child);
static int device_remove_child(device_t *parent, device_t *child);
static uint32_t generate_device_id(void);

/**
 * Initialize the device manager
 */
int device_manager_init(void) {
    log_info("Initializing Device Manager");
    
    // Clear device array
    memset(devices, 0, sizeof(devices));
    num_devices = 0;
    
    // Create and register root device
    device_init_root();
    
    log_info("Device Manager initialized successfully");
    return DEVICE_OK;
}

/**
 * Initialize the root device
 */
static void device_init_root(void) {
    // Allocate root device
    root_device = (device_t*)heap_alloc(sizeof(device_t));
    if (!root_device) {
        log_error("Failed to allocate root device");
        return;
    }
    
    // Initialize root device
    memset(root_device, 0, sizeof(device_t));
    
    strncpy(root_device->name, "system", sizeof(root_device->name));
    strncpy(root_device->path, "/", sizeof(root_device->path));
    root_device->id = 0;  // Root device has ID 0
    root_device->type = DEVICE_TYPE_UNKNOWN;
    root_device->status = DEVICE_STATUS_ENABLED;
    root_device->flags = DEVICE_FLAG_ROOT | DEVICE_FLAG_SYSTEM;
    
    // Register root device
    device_register(root_device);
    
    log_debug("Root device initialized");
}

/**
 * Generate a unique device ID
 */
static uint32_t generate_device_id(void) {
    static uint32_t next_id = 1;  // 0 is reserved for root device
    return next_id++;
}

/**
 * Register a device with the device manager
 */
int device_register(device_t *dev) {
    if (!dev) {
        log_error("Attempted to register NULL device");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if we have room for another device
    if (num_devices >= MAX_DEVICES) {
        log_error("Maximum number of devices reached");
        return DEVICE_ERROR_RESOURCE;
    }
    
    // Set device ID if not already set
    if (dev->id == 0 && dev != root_device) {
        dev->id = generate_device_id();
    }
    
    // Check for name conflict
    for (int i = 0; i < num_devices; i++) {
        if (devices[i] && strcmp(devices[i]->name, dev->name) == 0) {
            log_warning("Device with name '%s' already registered, appending unique ID", dev->name);
            
            // Append ID to make name unique
            char temp[32];
            snprintf(temp, sizeof(temp), "%s_%u", dev->name, dev->id);
            strncpy(dev->name, temp, sizeof(dev->name));
        }
    }
    
    // Add to device array
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i]) {
            devices[i] = dev;
            num_devices++;
            
            log_info("Registered device: %s (ID: %u, Type: 0x%02X)",
                     dev->name, dev->id, dev->type);
            
            // Find driver for this device if needed
            if (!dev->driver) {
                device_driver_t *driver = device_find_driver(dev);
                if (driver) {
                    log_debug("Found driver '%s' for device '%s'",
                              driver->name, dev->name);
                    dev->driver = driver;
                    
                    // Set operations from driver if not already set
                    if (!dev->ops) {
                        dev->ops = &driver->ops;
                    }
                    
                    // Initialize device if driver provides init function
                    if (driver->ops.init) {
                        driver->ops.init(dev);
                    }
                }
            }
            
            // Add as child to parent device
            if (dev->parent) {
                device_add_child(dev->parent, dev);
            } else if (dev != root_device) {
                // If no parent specified, use root device
                dev->parent = root_device;
                device_add_child(root_device, dev);
            }
            
            // Generate device path
            if (dev->parent && dev != root_device) {
                // Combine parent path with device name
                snprintf(dev->path, sizeof(dev->path), "%s/%s",
                         dev->parent->path, dev->name);
            }
            
            return DEVICE_OK;
        }
    }
    
    log_error("Failed to register device '%s'", dev->name);
    return DEVICE_ERROR_RESOURCE;
}

/**
 * Unregister a device from the device manager
 */
int device_unregister(device_t *dev) {
    if (!dev) {
        log_error("Attempted to unregister NULL device");
        return DEVICE_ERROR_INVALID;
    }
    
    // Find device in array
    int found = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i] == dev) {
            found = 1;
            
            // Remove references from child devices
            if (dev->children) {
                for (int j = 0; j < dev->num_children; j++) {
                    if (dev->children[j]) {
                        if (dev->children[j]->parent == dev) {
                            dev->children[j]->parent = root_device;
                        }
                    }
                }
                
                // Free children array
                heap_free(dev->children);
            }
            
            // Remove from parent's children list
            if (dev->parent) {
                device_remove_child(dev->parent, dev);
            }
            
            // Call device cleanup if driver provides remove function
            if (dev->ops && dev->ops->remove) {
                dev->ops->remove(dev);
            }
            
            // Remove from device array
            devices[i] = NULL;
            num_devices--;
            
            log_info("Unregistered device: %s (ID: %u)",
                     dev->name, dev->id);
            
            return DEVICE_OK;
        }
    }
    
    log_warning("Device '%s' not found for unregistration", dev->name);
    return DEVICE_ERROR_NO_DEVICE;
}

/**
 * Add a child device to a parent device
 */
static int device_add_child(device_t *parent, device_t *child) {
    if (!parent || !child) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if child is already in parent's children array
    if (parent->children) {
        for (int i = 0; i < parent->num_children; i++) {
            if (parent->children[i] == child) {
                return DEVICE_OK;  // Already a child
            }
        }
    }
    
    // Allocate or expand children array
    if (!parent->children) {
        parent->children = (device_t**)heap_alloc(sizeof(device_t*) * 4);
        if (!parent->children) {
            log_error("Failed to allocate children array for device '%s'", parent->name);
            return DEVICE_ERROR_RESOURCE;
        }
        parent->num_children = 0;
    } else {
        // Check if we need to expand the array (grow by factor of 2)
        if (parent->num_children % 4 == 0) {
            device_t** new_children = (device_t**)heap_alloc(
                sizeof(device_t*) * (parent->num_children + 4));
            if (!new_children) {
                log_error("Failed to expand children array for device '%s'", parent->name);
                return DEVICE_ERROR_RESOURCE;
            }
            
            // Copy existing children
            memcpy(new_children, parent->children, sizeof(device_t*) * parent->num_children);
            heap_free(parent->children);
            parent->children = new_children;
        }
    }
    
    // Add child to array
    parent->children[parent->num_children++] = child;
    
    return DEVICE_OK;
}

/**
 * Remove a child device from a parent device
 */
static int device_remove_child(device_t *parent, device_t *child) {
    if (!parent || !child || !parent->children) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Find child in array
    int found = -1;
    for (int i = 0; i < parent->num_children; i++) {
        if (parent->children[i] == child) {
            found = i;
            break;
        }
    }
    
    if (found >= 0) {
        // Shift remaining children
        for (int i = found; i < parent->num_children - 1; i++) {
            parent->children[i] = parent->children[i + 1];
        }
        
        parent->num_children--;
        
        // Free array if empty
        if (parent->num_children == 0) {
            heap_free(parent->children);
            parent->children = NULL;
        }
        
        return DEVICE_OK;
    }
    
    return DEVICE_ERROR_NO_DEVICE;
}

/**
 * Find a device by name
 */
device_t *device_find_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i] && strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }
    
    return NULL;
}

/**
 * Find a device by ID
 */
device_t *device_find_by_id(uint32_t id) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i] && devices[i]->id == id) {
            return devices[i];
        }
    }
    
    return NULL;
}

/**
 * Find devices by type
 */
int device_find_by_type(uint8_t type, device_t **result, int max_devices) {
    if (!result || max_devices <= 0) {
        return 0;
    }
    
    int count = 0;
    
    for (int i = 0; i < MAX_DEVICES && count < max_devices; i++) {
        if (devices[i] && devices[i]->type == type) {
            result[count++] = devices[i];
        }
    }
    
    return count;
}

/**
 * Register a device driver
 */
int device_driver_register(device_driver_t *driver) {
    if (!driver) {
        log_error("Attempted to register NULL driver");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check for duplicate
    device_driver_t *curr = drivers;
    while (curr) {
        if (strcmp(curr->name, driver->name) == 0) {
            log_warning("Driver '%s' already registered", driver->name);
            return DEVICE_ERROR_GENERAL;
        }
        curr = curr->next;
    }
    
    // Generate ID if not set
    if (driver->id == 0) {
        static uint32_t next_driver_id = 1;
        driver->id = next_driver_id++;
    }
    
    // Add to drivers list
    driver->next = drivers;
    drivers = driver;
    
    log_info("Registered driver: %s (ID: %u, Version: 0x%08X)",
             driver->name, driver->id, driver->version);
    
    // Initialize the driver if it has an init function
    if (driver->init) {
        int result = driver->init();
        if (result != DEVICE_OK) {
            log_warning("Driver '%s' initialization failed: %d", driver->name, result);
        }
    }
    
    // Probe for devices that match this driver
    if (driver->probe) {
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (devices[i] && !devices[i]->driver) {
                // Check if driver supports this device
                for (int j = 0; j < driver->num_supported_devices; j++) {
                    if (driver->vendor_ids[j] == devices[i]->vendor_id &&
                        driver->device_ids[j] == devices[i]->device_id) {
                        // Driver matches device
                        if (driver->probe(devices[i]) == DEVICE_OK) {
                            devices[i]->driver = driver;
                            
                            // Set operations from driver
                            devices[i]->ops = &driver->ops;
                            
                            // Initialize device
                            if (driver->ops.init) {
                                driver->ops.init(devices[i]);
                            }
                            
                            log_info("Driver '%s' claimed device '%s'",
                                     driver->name, devices[i]->name);
                        }
                    }
                }
            }
        }
    }
    
    return DEVICE_OK;
}

/**
 * Unregister a device driver
 */
int device_driver_unregister(device_driver_t *driver) {
    if (!driver) {
        log_error("Attempted to unregister NULL driver");
        return DEVICE_ERROR_INVALID;
    }
    
    // Find driver in list
    device_driver_t **pprev = &drivers;
    device_driver_t *curr = drivers;
    
    while (curr) {
        if (curr == driver) {
            // Remove from linked list
            *pprev = curr->next;
            
            // Call driver cleanup function if it exists
            if (curr->exit) {
                curr->exit();
            }
            
            // Remove driver reference from devices
            for (int i = 0; i < MAX_DEVICES; i++) {
                if (devices[i] && devices[i]->driver == driver) {
                    devices[i]->driver = NULL;
                    devices[i]->ops = NULL;
                }
            }
            
            log_info("Unregistered driver: %s", driver->name);
            
            return DEVICE_OK;
        }
        
        pprev = &curr->next;
        curr = curr->next;
    }
    
    log_warning("Driver '%s' not found for unregistration", driver->name);
    return DEVICE_ERROR_NO_DEVICE;
}

/**
 * Find a driver for a specific device
 */
device_driver_t *device_find_driver(device_t *dev) {
    if (!dev) {
        return NULL;
    }
    
    // Check if a driver is already assigned
    if (dev->driver) {
        return dev->driver;
    }
    
    // Find a compatible driver
    device_driver_t *curr = drivers;
    
    while (curr) {
        // Check if driver supports this device
        for (int j = 0; j < curr->num_supported_devices; j++) {
            if (curr->vendor_ids[j] == dev->vendor_id &&
                curr->device_ids[j] == dev->device_id) {
                // Driver matches device
                if (curr->probe && curr->probe(dev) == DEVICE_OK) {
                    return curr;
                }
            }
        }
        
        curr = curr->next;
    }
    
    return NULL;
}

/**
 * Register a device class
 */
int device_class_register(device_class_t *class) {
    if (!class) {
        log_error("Attempted to register NULL class");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check for duplicate
    device_class_t *curr = classes;
    while (curr) {
        if (strcmp(curr->name, class->name) == 0) {
            log_warning("Class '%s' already registered", class->name);
            return DEVICE_ERROR_GENERAL;
        }
        curr = curr->next;
    }
    
    // Generate ID if not set
    if (class->id == 0) {
        static uint32_t next_class_id = 1;
        class->id = next_class_id++;
    }
    
    // Add to classes list
    class->next = classes;
    classes = class;
    
    log_info("Registered device class: %s (ID: %u)",
             class->name, class->id);
    
    // Initialize the class if it has an init function
    if (class->init) {
        int result = class->init(class);
        if (result != DEVICE_OK) {
            log_warning("Class '%s' initialization failed: %d", class->name, result);
        }
    }
    
    return DEVICE_OK;
}

/**
 * Unregister a device class
 */
int device_class_unregister(device_class_t *class) {
    if (!class) {
        log_error("Attempted to unregister NULL class");
        return DEVICE_ERROR_INVALID;
    }
    
    // Find class in list
    device_class_t **pprev = &classes;
    device_class_t *curr = classes;
    
    while (curr) {
        if (curr == class) {
            // Remove from linked list
            *pprev = curr->next;
            
            // Call class cleanup function if it exists
            if (curr->exit) {
                curr->exit(curr);
            }
            
            // Remove class reference from devices
            for (int i = 0; i < MAX_DEVICES; i++) {
                if (devices[i] && devices[i]->class == class) {
                    devices[i]->class = NULL;
                }
            }
            
            log_info("Unregistered class: %s", class->name);
            
            return DEVICE_OK;
        }
        
        pprev = &curr->next;
        curr = curr->next;
    }
    
    log_warning("Class '%s' not found for unregistration", class->name);
    return DEVICE_ERROR_NO_DEVICE;
}

/**
 * Find a device class by name
 */
device_class_t *device_class_find_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    device_class_t *curr = classes;
    
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    
    return NULL;
}

/**
 * Find a device class by ID
 */
device_class_t *device_class_find_by_id(uint32_t id) {
    device_class_t *curr = classes;
    
    while (curr) {
        if (curr->id == id) {
            return curr;
        }
        curr = curr->next;
    }
    
    return NULL;
}

/**
 * Open a device
 */
int device_open(device_t *dev, uint32_t flags) {
    if (!dev) {
        log_error("Attempted to open NULL device");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if device has open operation
    if (!dev->ops || !dev->ops->open) {
        log_error("Device '%s' does not support open operation", dev->name);
        return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Call device open operation
    return dev->ops->open(dev, flags);
}

/**
 * Close a device
 */
int device_close(device_t *dev) {
    if (!dev) {
        log_error("Attempted to close NULL device");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if device has close operation
    if (!dev->ops || !dev->ops->close) {
        log_error("Device '%s' does not support close operation", dev->name);
        return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Call device close operation
    return dev->ops->close(dev);
}

/**
 * Read from a device
 */
int device_read(device_t *dev, void *buffer, size_t size, uint64_t offset) {
    if (!dev || !buffer) {
        log_error("Invalid parameters for device read");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if device has read operation
    if (!dev->ops || !dev->ops->read) {
        log_error("Device '%s' does not support read operation", dev->name);
        return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Call device read operation
    return dev->ops->read(dev, buffer, size, offset);
}

/**
 * Write to a device
 */
int device_write(device_t *dev, const void *buffer, size_t size, uint64_t offset) {
    if (!dev || !buffer) {
        log_error("Invalid parameters for device write");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if device has write operation
    if (!dev->ops || !dev->ops->write) {
        log_error("Device '%s' does not support write operation", dev->name);
        return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Call device write operation
    return dev->ops->write(dev, buffer, size, offset);
}

/**
 * Perform I/O control on a device
 */
int device_ioctl(device_t *dev, int request, void *arg) {
    if (!dev) {
        log_error("Attempted ioctl on NULL device");
        return DEVICE_ERROR_INVALID;
    }
    
    // Check if device has ioctl operation
    if (!dev->ops || !dev->ops->ioctl) {
        log_error("Device '%s' does not support ioctl operation", dev->name);
        return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Call device ioctl operation
    return dev->ops->ioctl(dev, request, arg);
}

/**
 * Create a device node
 */
int device_create_node(const char *name, device_t *dev, uint32_t mode) {
    // This would typically create an entry in the filesystem
    // For now, just log that we would create it
    log_info("Would create device node '%s' for device '%s'", name, dev->name);
    return DEVICE_OK;
}

/**
 * Remove a device node
 */
int device_remove_node(const char *name) {
    // This would typically remove an entry from the filesystem
    // For now, just log that we would remove it
    log_info("Would remove device node '%s'", name);
    return DEVICE_OK;
}

/**
 * Print device tree (for debugging)
 */
void device_print_tree(void) {
    log_info("Device Tree:");
    
    if (!root_device) {
        log_info("  No root device");
        return;
    }
    
    // Helper function to recursively print device tree
    typedef void (*print_device_func)(device_t *dev, int depth);
    
    print_device_func print_device_recursive = NULL;
    print_device_recursive = (print_device_func)(void*)([](device_t *dev, int depth) {
        // Print indentation
        char indent[64] = {0};
        for (int i = 0; i < depth * 2; i++) {
            indent[i] = ' ';
        }
        
        // Print device info
        log_info("%s%s (ID: %u, Type: 0x%02X)",
                 indent, dev->name, dev->id, dev->type);
        
        // Print children
        if (dev->children) {
            for (int i = 0; i < dev->num_children; i++) {
                if (dev->children[i]) {
                    // Recursive call
                    ((void(*)(device_t*,int))print_device_recursive)(dev->children[i], depth + 1);
                }
            }
        }
    });
    
    // Start from root device with depth 0
    ((void(*)(device_t*,int))print_device_recursive)(root_device, 0);
}