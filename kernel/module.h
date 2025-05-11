#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>
#include <stddef.h>
#include "device_manager.h"

/**
 * Module Status Values
 */
#define MODULE_STATUS_UNLOADED   0  // Module is not loaded
#define MODULE_STATUS_LOADED     1  // Module is loaded and initialized
#define MODULE_STATUS_ERROR      2  // Error occurred during module operation
#define MODULE_STATUS_DISABLED   3  // Module is loaded but disabled

/**
 * Module Flags
 */
#define MODULE_FLAG_NONE         0x00000000  // No flags
#define MODULE_FLAG_BUILTIN      0x00000001  // Built-in module (cannot be unloaded)
#define MODULE_FLAG_AUTOLOAD     0x00000002  // Automatically load during system boot
#define MODULE_FLAG_ESSENTIAL    0x00000004  // System requires this module to function
#define MODULE_FLAG_LOADABLE     0x00000008  // Module can be dynamically loaded
#define MODULE_FLAG_UNLOADABLE   0x00000010  // Module can be unloaded
#define MODULE_FLAG_RELOADABLE   0x00000020  // Module can be reloaded without system reset
#define MODULE_FLAG_DEBUG        0x00000040  // Module has debug features enabled
#define MODULE_FLAG_EXPERIMENTAL 0x00000080  // Module is experimental/unstable
#define MODULE_FLAG_DEPRECATED   0x00000100  // Module is deprecated and may be removed

/**
 * Module Dependency Structure
 */
typedef struct module_dependency {
    char name[32];                   // Name of the dependency module
    uint32_t min_version;            // Minimum required version
    uint32_t max_version;            // Maximum compatible version (0 = any)
    struct module_dependency *next;  // Next dependency in the list
} module_dependency_t;

/**
 * Function pointer types used by modules
 */
typedef int (*module_init_func_t)(void);
typedef int (*module_exit_func_t)(void);
typedef int (*module_start_func_t)(void);
typedef int (*module_stop_func_t)(void);
typedef int (*module_config_func_t)(const char *key, const char *value);
typedef void* (*module_get_interface_func_t)(const char *interface_name);
typedef int (*module_event_handler_t)(uint32_t event_type, void *event_data);

/**
 * Module interface structure
 */
typedef struct module_interface {
    char name[32];                   // Interface name
    void *implementation;            // Pointer to implementation
    struct module_interface *next;   // Next interface in the list
} module_interface_t;

/**
 * Module Structure
 */
typedef struct module {
    char name[32];                   // Module name
    char description[128];           // Module description
    char author[64];                 // Module author
    uint32_t version;                // Module version (format: 0xMMNNPPBB for major.minor.patch.build)
    uint32_t id;                     // Unique module ID
    uint32_t flags;                  // Module flags
    uint8_t status;                  // Current status
    
    // Module lifecycle functions
    module_init_func_t init;         // Initialize module
    module_exit_func_t exit;         // Cleanup module
    module_start_func_t start;       // Start module functionality
    module_stop_func_t stop;         // Stop module functionality
    module_config_func_t config;     // Configure module
    module_get_interface_func_t get_interface; // Get module interface
    module_event_handler_t event_handler; // Handle system events
    
    // Module dependencies
    module_dependency_t *dependencies; // List of module dependencies
    
    // Module interfaces
    module_interface_t *interfaces;  // List of provided interfaces
    
    // Physical ELF module representation (for loadable modules)
    void *module_base;               // Base memory address of loaded module
    size_t module_size;              // Size of loaded module in bytes
    char *filename;                  // Path to module file (for loadable modules)
    
    // Related devices and drivers
    device_driver_t **drivers;       // Array of drivers provided by this module
    int num_drivers;                 // Number of drivers
    
    // System integration
    void *private_data;              // Private module data
    
    // Module list management
    struct module *next;             // Next module in the list
} module_t;

/**
 * Initialize the module system
 *
 * @return 0 on success, error code on failure
 */
int module_system_init(void);

/**
 * Register a module with the module system
 *
 * @param module Pointer to the module structure
 * @return 0 on success, error code on failure
 */
int module_register(module_t *module);

/**
 * Unregister a module from the module system
 *
 * @param module Pointer to the module structure
 * @return 0 on success, error code on failure
 */
int module_unregister(module_t *module);

/**
 * Load a module from a file
 *
 * @param filename Path to the module file
 * @param flags Module flags to apply
 * @return Pointer to the loaded module, or NULL on failure
 */
module_t *module_load(const char *filename, uint32_t flags);

/**
 * Unload a module
 *
 * @param module Pointer to the module structure
 * @return 0 on success, error code on failure
 */
int module_unload(module_t *module);

/**
 * Find a module by name
 *
 * @param name Module name
 * @return Pointer to the module, or NULL if not found
 */
module_t *module_find_by_name(const char *name);

/**
 * Find a module by ID
 *
 * @param id Module ID
 * @return Pointer to the module, or NULL if not found
 */
module_t *module_find_by_id(uint32_t id);

/**
 * Get information about all loaded modules
 *
 * @param modules Array to store module pointers
 * @param max_modules Maximum number of modules to return
 * @return Number of modules returned
 */
int module_list(module_t **modules, int max_modules);

/**
 * Get a specific module interface
 *
 * @param module_name Name of the module
 * @param interface_name Name of the interface
 * @return Pointer to the interface implementation, or NULL if not found
 */
void *module_get_interface(const char *module_name, const char *interface_name);

/**
 * Check if all dependencies for a module are satisfied
 *
 * @param module Pointer to the module structure
 * @return 1 if all dependencies are satisfied, 0 otherwise
 */
int module_check_dependencies(module_t *module);

/**
 * Enable a module
 *
 * @param module Pointer to the module structure
 * @return 0 on success, error code on failure
 */
int module_enable(module_t *module);

/**
 * Disable a module
 *
 * @param module Pointer to the module structure
 * @return 0 on success, error code on failure
 */
int module_disable(module_t *module);

/**
 * Get module status string
 *
 * @param status Module status code
 * @return String representation of the status
 */
const char *module_status_string(uint8_t status);

/**
 * Print module information (for debugging)
 *
 * @param module Pointer to the module structure
 */
void module_print_info(module_t *module);

/**
 * Print module dependency tree (for debugging)
 *
 * @param module Pointer to the module structure
 * @param depth Current depth in dependency tree
 */
void module_print_dependency_tree(module_t *module, int depth);

/**
 * Parse module configuration file
 * 
 * @param module Pointer to the module structure
 * @param config_file Path to configuration file
 * @return 0 on success, error code on failure
 */
int module_parse_config(module_t *module, const char *config_file);

#endif /* MODULE_H */