/**
 * Windows Driver Manager
 * 
 * Implements driver loading, management, and communication for Windows drivers.
 *
 * Version: 1.0
 * Date: May 1, 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "driver_manager.h"
#include "pe_loader.h"
#include "wdm.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include "../../filesystem/vfs/vfs.h"
#include "../../hal/include/hal.h"

#define DRV_MGR_TAG "DRIVER_MGR"
#define MAX_DRIVERS 32

// Driver entry point function signature (DriverEntry)
typedef NTSTATUS (*driver_entry_t)(PDRIVER_OBJECT, PUNICODE_STRING);

// Driver structure
typedef struct {
    driver_info_t info;
    pe_image_t    image;
    driver_object_t driver_object;
    bool         in_use;
} driver_t;

// Driver manager state
static struct {
    bool initialized;
    driver_t drivers[MAX_DRIVERS];
    uint32_t driver_count;
    uint32_t next_device_id;
} driver_manager = {0};

// Import resolver function for Windows drivers
static void* wdm_import_resolver(const char* module_name, const char* function_name) {
    // Convert ordinal imports to integer for special handling
    if ((uintptr_t)function_name <= 0xFFFF) {
        uint16_t ordinal = (uint16_t)(uintptr_t)function_name;
        
        // Handle by ordinal - many Windows drivers use ordinal imports
        log_debug(DRV_MGR_TAG, "Resolving ordinal import %u from %s", ordinal, module_name);
        
        // Check for NTOSKRNL exports by ordinal
        if (strcmp(module_name, "ntoskrnl.exe") == 0) {
            return wdm_get_ntoskrnl_export_by_ordinal(ordinal);
        }
        
        // Check for HAL exports by ordinal
        if (strcmp(module_name, "hal.dll") == 0) {
            return wdm_get_hal_export_by_ordinal(ordinal);
        }
    } else {
        // Handle by name
        log_debug(DRV_MGR_TAG, "Resolving import %s from %s", function_name, module_name);
        
        // Check for NTOSKRNL exports
        if (strcmp(module_name, "ntoskrnl.exe") == 0) {
            return wdm_get_ntoskrnl_export(function_name);
        }
        
        // Check for HAL exports
        if (strcmp(module_name, "hal.dll") == 0) {
            return wdm_get_hal_export(function_name);
        }
    }
    
    log_warning(DRV_MGR_TAG, "Unresolved import %s from %s", 
               (uintptr_t)function_name <= 0xFFFF ? "#" : "", module_name);
    
    return NULL;
}

// Initialize the driver manager
int driver_manager_init(void) {
    if (driver_manager.initialized) {
        log_warning(DRV_MGR_TAG, "Driver manager already initialized");
        return 0;
    }
    
    // Initialize WDM subsystem
    int result = wdm_init();
    if (result != 0) {
        log_error(DRV_MGR_TAG, "Failed to initialize WDM subsystem: %d", result);
        return result;
    }
    
    // Initialize driver manager state
    memset(&driver_manager, 0, sizeof(driver_manager));
    driver_manager.initialized = true;
    driver_manager.next_device_id = 1;  // Start with ID 1
    
    log_info(DRV_MGR_TAG, "Driver manager initialized successfully");
    return 0;
}

// Shut down the driver manager
void driver_manager_shutdown(void) {
    if (!driver_manager.initialized) {
        return;
    }
    
    // Unload all drivers
    for (uint32_t i = 0; i < MAX_DRIVERS; i++) {
        driver_t* driver = &driver_manager.drivers[i];
        
        if (driver->in_use) {
            // Stop the driver if it's running
            if (driver->info.state == DRV_STATE_STARTED) {
                driver_manager_stop(i);
            }
            
            // Unload the driver
            driver_manager_unload(i);
        }
    }
    
    // Shut down WDM subsystem
    wdm_shutdown();
    
    // Clear driver manager state
    memset(&driver_manager, 0, sizeof(driver_manager));
    
    log_info(DRV_MGR_TAG, "Driver manager shut down successfully");
}

// Find a free driver slot
static int find_free_driver_slot(void) {
    for (uint32_t i = 0; i < MAX_DRIVERS; i++) {
        if (!driver_manager.drivers[i].in_use) {
            return i;
        }
    }
    
    return -1;  // No free slots
}

// Load a Windows driver
int driver_manager_load(const char* path, const char* name, driver_type_t type, uint32_t flags) {
    if (!driver_manager.initialized) {
        log_error(DRV_MGR_TAG, "Driver manager not initialized");
        return -1;
    }
    
    // Find a free driver slot
    int slot = find_free_driver_slot();
    if (slot < 0) {
        log_error(DRV_MGR_TAG, "No free driver slots available");
        return -2;
    }
    
    // Set up driver info structure
    driver_t* driver = &driver_manager.drivers[slot];
    memset(driver, 0, sizeof(driver_t));
    
    strncpy(driver->info.name, name, sizeof(driver->info.name) - 1);
    driver->info.type = type;
    driver->info.flags = flags;
    driver->info.state = DRV_STATE_UNLOADED;
    
    log_info(DRV_MGR_TAG, "Loading driver '%s' from '%s'", name, path);
    
    // Set up PE loader configuration
    pe_loader_config_t pe_config;
    memset(&pe_config, 0, sizeof(pe_config));
    
    pe_config.preferred_base_address = 0;  // Let the PE loader choose
    pe_config.relocate = true;             // Always apply relocations
    pe_config.resolve_imports = true;      // Resolve imports
    pe_config.import_resolver = wdm_import_resolver;
    pe_config.map_sections = true;
    pe_config.debug_info = false;
    
    // Load the PE file
    pe_error_t pe_result = pe_load_from_file(path, &pe_config, &driver->image);
    
    if (pe_result != PE_SUCCESS) {
        log_error(DRV_MGR_TAG, "Failed to load PE file: %d", pe_result);
        return -3;
    }
    
    // Get driver description from the PE image if available
    // This would typically be extracted from the file version info or resources
    strncpy(driver->info.description, "Windows Driver", sizeof(driver->info.description) - 1);
    
    // Get driver version from the PE image if available
    strncpy(driver->info.version, "1.0", sizeof(driver->info.version) - 1);
    
    // Set up the driver object
    memset(&driver->driver_object, 0, sizeof(driver_object_t));
    driver->driver_object.DriverUnload = NULL;  // Will be set by the driver entry point
    driver->driver_object.DriverExtension = NULL;
    driver->driver_object.DriverStart = driver->image.base_address;
    driver->driver_object.DriverSize = (uint32_t)driver->image.image_size;
    
    // Mark the slot as in use
    driver->in_use = true;
    driver->info.state = DRV_STATE_LOADED;
    driver_manager.driver_count++;
    
    log_info(DRV_MGR_TAG, "Driver '%s' loaded successfully at 0x%llX", 
             name, (unsigned long long)driver->image.base_address);
    
    return slot;
}

// Unload a Windows driver
int driver_manager_unload(int driver_id) {
    if (!driver_manager.initialized) {
        log_error(DRV_MGR_TAG, "Driver manager not initialized");
        return -1;
    }
    
    // Validate driver ID
    if (driver_id < 0 || driver_id >= MAX_DRIVERS) {
        log_error(DRV_MGR_TAG, "Invalid driver ID: %d", driver_id);
        return -2;
    }
    
    driver_t* driver = &driver_manager.drivers[driver_id];
    
    // Check if the driver is in use
    if (!driver->in_use) {
        log_error(DRV_MGR_TAG, "Driver slot %d not in use", driver_id);
        return -3;
    }
    
    // Check if the driver is running
    if (driver->info.state == DRV_STATE_STARTED) {
        log_warning(DRV_MGR_TAG, "Driver '%s' is still running, stopping first", driver->info.name);
        driver_manager_stop(driver_id);
    }
    
    log_info(DRV_MGR_TAG, "Unloading driver '%s'", driver->info.name);
    
    // Call the driver's unload routine if it has one
    if (driver->driver_object.DriverUnload != NULL) {
        driver->driver_object.DriverUnload(&driver->driver_object);
    }
    
    // Unload the PE image
    pe_error_t pe_result = pe_unload(&driver->image);
    if (pe_result != PE_SUCCESS) {
        log_warning(DRV_MGR_TAG, "Failed to unload PE image: %d", pe_result);
        // Continue anyway to clean up the driver slot
    }
    
    // Clean up the driver slot
    driver->in_use = false;
    driver_manager.driver_count--;
    
    log_info(DRV_MGR_TAG, "Driver '%s' unloaded successfully", driver->info.name);
    
    return 0;
}

// Start a Windows driver
int driver_manager_start(int driver_id) {
    if (!driver_manager.initialized) {
        log_error(DRV_MGR_TAG, "Driver manager not initialized");
        return -1;
    }
    
    // Validate driver ID
    if (driver_id < 0 || driver_id >= MAX_DRIVERS) {
        log_error(DRV_MGR_TAG, "Invalid driver ID: %d", driver_id);
        return -2;
    }
    
    driver_t* driver = &driver_manager.drivers[driver_id];
    
    // Check if the driver is in use
    if (!driver->in_use) {
        log_error(DRV_MGR_TAG, "Driver slot %d not in use", driver_id);
        return -3;
    }
    
    // Check if the driver is already running
    if (driver->info.state == DRV_STATE_STARTED) {
        log_warning(DRV_MGR_TAG, "Driver '%s' is already running", driver->info.name);
        return 0;
    }
    
    // Make sure the driver is in the LOADED state
    if (driver->info.state != DRV_STATE_LOADED && driver->info.state != DRV_STATE_STOPPED) {
        log_error(DRV_MGR_TAG, "Driver '%s' is not in a startable state: %d", 
                 driver->info.name, driver->info.state);
        return -4;
    }
    
    log_info(DRV_MGR_TAG, "Starting driver '%s'", driver->info.name);
    
    // Create registry path unicode string (for driver entry point)
    unicode_string_t registry_path;
    wchar_t registry_path_buffer[128];
    
    // Convert ASCII name to unicode and format registry path
    char temp_path[128];
    snprintf(temp_path, sizeof(temp_path), "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s", 
             driver->info.name);
    
    // Convert to unicode (simple ASCII to UTF-16 conversion)
    for (size_t i = 0; i < strlen(temp_path) + 1; i++) {
        registry_path_buffer[i] = (wchar_t)temp_path[i];
    }
    
    // Set up the unicode string
    registry_path.Length = (uint16_t)(strlen(temp_path) * 2);
    registry_path.MaximumLength = sizeof(registry_path_buffer);
    registry_path.Buffer = registry_path_buffer;
    
    // Find the driver entry point (DriverEntry)
    driver_entry_t driver_entry = NULL;
    
    // Try standard naming conventions for DriverEntry
    const char* entry_point_names[] = {
        "DriverEntry",
        "_DriverEntry@8",
        "DriverMain",
        "_DriverMain@8"
    };
    
    for (size_t i = 0; i < sizeof(entry_point_names) / sizeof(entry_point_names[0]); i++) {
        driver_entry = (driver_entry_t)pe_get_export(&driver->image, entry_point_names[i]);
        if (driver_entry != NULL) {
            break;
        }
    }
    
    // If no named entry point found, use the image entry point
    if (driver_entry == NULL) {
        driver_entry = (driver_entry_t)driver->image.entry_point;
    }
    
    if (driver_entry == NULL) {
        log_error(DRV_MGR_TAG, "Failed to find driver entry point for '%s'", driver->info.name);
        return -5;
    }
    
    log_debug(DRV_MGR_TAG, "Found driver entry point at 0x%llX", (unsigned long long)driver_entry);
    
    // Call the driver entry point
    NTSTATUS status = driver_entry(&driver->driver_object, &registry_path);
    
    if (!NT_SUCCESS(status)) {
        log_error(DRV_MGR_TAG, "Driver entry point returned error: 0x%08X", status);
        driver->info.state = DRV_STATE_ERROR;
        driver->info.error_count++;
        
        return -6;
    }
    
    // Driver started successfully
    driver->info.state = DRV_STATE_STARTED;
    
    log_info(DRV_MGR_TAG, "Driver '%s' started successfully", driver->info.name);
    
    return 0;
}

// Stop a Windows driver
int driver_manager_stop(int driver_id) {
    if (!driver_manager.initialized) {
        log_error(DRV_MGR_TAG, "Driver manager not initialized");
        return -1;
    }
    
    // Validate driver ID
    if (driver_id < 0 || driver_id >= MAX_DRIVERS) {
        log_error(DRV_MGR_TAG, "Invalid driver ID: %d", driver_id);
        return -2;
    }
    
    driver_t* driver = &driver_manager.drivers[driver_id];
    
    // Check if the driver is in use
    if (!driver->in_use) {
        log_error(DRV_MGR_TAG, "Driver slot %d not in use", driver_id);
        return -3;
    }
    
    // Check if the driver is running
    if (driver->info.state != DRV_STATE_STARTED) {
        log_warning(DRV_MGR_TAG, "Driver '%s' is not running (state: %d)", 
                   driver->info.name, driver->info.state);
        return 0;
    }
    
    log_info(DRV_MGR_TAG, "Stopping driver '%s'", driver->info.name);
    
    // Call the driver's unload routine if it has one
    if (driver->driver_object.DriverUnload != NULL) {
        driver->driver_object.DriverUnload(&driver->driver_object);
    } else {
        log_warning(DRV_MGR_TAG, "Driver '%s' has no unload routine", driver->info.name);
    }
    
    // Mark the driver as stopped
    driver->info.state = DRV_STATE_STOPPED;
    
    log_info(DRV_MGR_TAG, "Driver '%s' stopped successfully", driver->info.name);
    
    return 0;
}

// Get the number of loaded drivers
int driver_manager_get_count(void) {
    return driver_manager.driver_count;
}

// Get information about a driver
int driver_manager_get_info(int driver_id, driver_info_t* info) {
    if (!driver_manager.initialized) {
        log_error(DRV_MGR_TAG, "Driver manager not initialized");
        return -1;
    }
    
    // Validate driver ID
    if (driver_id < 0 || driver_id >= MAX_DRIVERS) {
        log_error(DRV_MGR_TAG, "Invalid driver ID: %d", driver_id);
        return -2;
    }
    
    driver_t* driver = &driver_manager.drivers[driver_id];
    
    // Check if the driver is in use
    if (!driver->in_use) {
        log_error(DRV_MGR_TAG, "Driver slot %d not in use", driver_id);
        return -3;
    }
    
    // Copy the driver info structure
    if (info != NULL) {
        memcpy(info, &driver->info, sizeof(driver_info_t));
    }
    
    return 0;
}

// Register a device for a driver
int driver_manager_register_device(int driver_id, const char* device_name, device_type_t device_type, void* device_extension) {
    if (!driver_manager.initialized) {
        log_error(DRV_MGR_TAG, "Driver manager not initialized");
        return -1;
    }
    
    // Validate driver ID
    if (driver_id < 0 || driver_id >= MAX_DRIVERS) {
        log_error(DRV_MGR_TAG, "Invalid driver ID: %d", driver_id);
        return -2;
    }
    
    driver_t* driver = &driver_manager.drivers[driver_id];
    
    // Check if the driver is in use
    if (!driver->in_use) {
        log_error(DRV_MGR_TAG, "Driver slot %d not in use", driver_id);
        return -3;
    }
    
    // Make sure the driver is in the STARTED state
    if (driver->info.state != DRV_STATE_STARTED) {
        log_error(DRV_MGR_TAG, "Driver '%s' is not started (state: %d)", 
                 driver->info.name, driver->info.state);
        return -4;
    }
    
    // Increment the device count for the driver
    driver->info.device_count++;
    
    // In a more complete implementation, we would store the device information
    // and create appropriate device objects in the uintOS device system
    
    log_info(DRV_MGR_TAG, "Registered device '%s' for driver '%s' (ID: %u)",
             device_name, driver->info.name, driver_manager.next_device_id);
    
    // Return a unique device ID
    return driver_manager.next_device_id++;
}

// Unregister a device
int driver_manager_unregister_device(int device_id) {
    // In a more complete implementation, we would look up the device
    // by its ID and remove it from the system
    
    log_info(DRV_MGR_TAG, "Unregistered device ID: %d", device_id);
    
    return 0;
}