/**
 * Windows Driver Manager for uintOS
 *
 * This component provides an interface for the kernel to load, unload,
 * and interact with Windows drivers through the WDM compatibility layer.
 * 
 * Version: 1.0
 * Date: May 1, 2025
 */

#ifndef UINTOS_DRIVER_MANAGER_H
#define UINTOS_DRIVER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "wdm.h"

// Max drivers supported by driver manager
#define DRV_MGR_MAX_DRIVERS 16

// Driver type categories
typedef enum {
    DRV_TYPE_UNKNOWN = 0,
    DRV_TYPE_STORAGE,      // Storage controllers, disk drivers
    DRV_TYPE_NETWORK,      // Network interfaces
    DRV_TYPE_DISPLAY,      // Graphics adapters
    DRV_TYPE_INPUT,        // Keyboards, mice, etc.
    DRV_TYPE_AUDIO,        // Sound cards
    DRV_TYPE_USB,          // USB controllers and devices
    DRV_TYPE_SERIAL,       // Serial port drivers
    DRV_TYPE_PARALLEL,     // Parallel port drivers
    DRV_TYPE_SYSTEM        // System-level drivers
} driver_type_t;

// Driver states
typedef enum {
    DRV_STATE_UNLOADED = 0,
    DRV_STATE_LOADED,
    DRV_STATE_STARTED,
    DRV_STATE_PAUSED,
    DRV_STATE_STOPPED,
    DRV_STATE_ERROR
} driver_state_t;

// Driver information structure
typedef struct {
    char name[64];
    char description[256];
    char version[32];
    driver_type_t type;
    driver_state_t state;
    uint32_t flags;
    uint32_t device_count;
    uint32_t load_time;      // Time when driver was loaded (system ticks)
    uint32_t error_count;    // Number of errors reported by this driver
    PDRIVER_OBJECT driver_obj; // Windows driver object
} driver_info_t;

/**
 * Initialize the driver manager
 * 
 * @return 0 on success, error code on failure
 */
int driver_manager_init(void);

/**
 * Shutdown the driver manager
 */
void driver_manager_shutdown(void);

/**
 * Load a Windows driver
 * 
 * @param driver_path Path to the driver
 * @param driver_name Driver name
 * @param driver_type Driver type
 * @param flags Driver-specific flags
 * @return Driver ID on success, negative error code on failure
 */
int driver_manager_load(const char* driver_path, const char* driver_name, 
                       driver_type_t driver_type, uint32_t flags);

/**
 * Unload a Windows driver
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @return 0 on success, error code on failure
 */
int driver_manager_unload(int driver_id);

/**
 * Start a loaded driver
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @return 0 on success, error code on failure
 */
int driver_manager_start(int driver_id);

/**
 * Stop a running driver
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @return 0 on success, error code on failure
 */
int driver_manager_stop(int driver_id);

/**
 * Get information about a loaded driver
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @param info Pointer to receive driver information
 * @return 0 on success, error code on failure
 */
int driver_manager_get_info(int driver_id, driver_info_t* info);

/**
 * Get the number of loaded drivers
 * 
 * @return Number of loaded drivers
 */
int driver_manager_get_count(void);

/**
 * Send a control command to a driver
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @param command Command code
 * @param input Input buffer
 * @param input_size Size of input buffer
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @param bytes_returned Pointer to receive number of bytes returned
 * @return 0 on success, error code on failure
 */
int driver_manager_control(int driver_id, uint32_t command,
                          void* input, uint32_t input_size,
                          void* output, uint32_t output_size,
                          uint32_t* bytes_returned);

/**
 * Register a device with the system
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @param device_type Type of device
 * @param device_name Name of device
 * @return Device ID on success, negative error code on failure
 */
int driver_manager_register_device(int driver_id, uint32_t device_type, const char* device_name);

/**
 * Find a loaded driver by name
 * 
 * @param driver_name Name of the driver
 * @return Driver ID if found, -1 otherwise
 */
int driver_manager_find_driver(const char* driver_name);

/**
 * Find devices of a specific type
 * 
 * @param device_type Type of device to find
 * @param device_ids Array to receive device IDs
 * @param max_devices Maximum number of device IDs to return
 * @return Number of devices found
 */
int driver_manager_find_devices(uint32_t device_type, int* device_ids, int max_devices);

/**
 * Check if a driver is loaded
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @return true if loaded, false otherwise
 */
bool driver_manager_is_loaded(int driver_id);

/**
 * Get a handle to the Windows driver object
 * 
 * @param driver_id Driver ID from driver_manager_load
 * @return Pointer to driver object if found, NULL otherwise
 */
PDRIVER_OBJECT driver_manager_get_driver_object(int driver_id);

/**
 * Get information about a device
 * 
 * @param device_id Device ID from driver_manager_register_device
 * @param device_name Buffer to receive device name
 * @param name_size Size of device_name buffer
 * @param device_type Pointer to receive device type
 * @return 0 on success, error code on failure
 */
int driver_manager_get_device_info(int device_id, char* device_name, 
                                  int name_size, uint32_t* device_type);

/**
 * Get the driver ID associated with a device
 * 
 * @param device_id Device ID from driver_manager_register_device
 * @return Driver ID on success, -1 if not found
 */
int driver_manager_get_device_driver(int device_id);

#endif /* UINTOS_DRIVER_MANAGER_H */