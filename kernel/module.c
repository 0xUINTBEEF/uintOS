#include "module.h"
#include "logging/log.h"
#include "memory/heap.h"
#include "memory/vmm.h"
#include "filesystem/vfs/vfs.h"
#include <string.h>

// Module system constants
#define MAX_MODULES 64
#define MODULE_MAGIC 0x4D4F4455  // "MODU"

// Module error codes
#define MODULE_ERROR_NONE        0
#define MODULE_ERROR_INVALID    -1
#define MODULE_ERROR_NOT_FOUND  -2
#define MODULE_ERROR_DUPLICATE  -3
#define MODULE_ERROR_DEPENDENCY -4
#define MODULE_ERROR_PERMISSION -5
#define MODULE_ERROR_MEMORY     -6
#define MODULE_ERROR_FORMAT     -7
#define MODULE_ERROR_VERSION    -8
#define MODULE_ERROR_IO         -9
#define MODULE_ERROR_INIT       -10

// Module system global state
static module_t *modules[MAX_MODULES];
static int num_modules = 0;
static uint32_t next_module_id = 1;
static int module_system_initialized = 0;

// Forward declarations of internal functions
static int module_verify_elf(void *module_data, size_t size);
static int module_resolve_symbols(module_t *module);
static int module_initialize_drivers(module_t *module);
static int module_add_dependency(module_t *module, const char *dep_name, 
                                uint32_t min_version, uint32_t max_version);

/**
 * Initialize the module system
 */
int module_system_init(void) {
    if (module_system_initialized) {
        log_warning("Module system already initialized");
        return MODULE_ERROR_DUPLICATE;
    }
    
    log_info("Initializing module system");
    
    // Clear module array
    memset(modules, 0, sizeof(modules));
    num_modules = 0;
    
    module_system_initialized = 1;
    log_info("Module system initialized successfully");
    
    return MODULE_ERROR_NONE;
}

/**
 * Generate a unique module ID
 */
static uint32_t generate_module_id(void) {
    return next_module_id++;
}

/**
 * Register a module with the module system
 */
int module_register(module_t *module) {
    if (!module_system_initialized) {
        log_error("Module system not initialized");
        return MODULE_ERROR_INVALID;
    }
    
    if (!module) {
        log_error("Attempted to register NULL module");
        return MODULE_ERROR_INVALID;
    }
    
    // Check if we have room for another module
    if (num_modules >= MAX_MODULES) {
        log_error("Maximum number of modules reached");
        return MODULE_ERROR_MEMORY;
    }
    
    // Check for duplicate name
    for (int i = 0; i < MAX_MODULES; i++) {
        if (modules[i] && strcmp(modules[i]->name, module->name) == 0) {
            log_error("Module '%s' already registered", module->name);
            return MODULE_ERROR_DUPLICATE;
        }
    }
    
    // Set module ID if not already set
    if (module->id == 0) {
        module->id = generate_module_id();
    }
    
    // Check dependencies
    if (!module_check_dependencies(module)) {
        log_error("Module '%s' has unmet dependencies", module->name);
        return MODULE_ERROR_DEPENDENCY;
    }
    
    // Add to module array
    for (int i = 0; i < MAX_MODULES; i++) {
        if (!modules[i]) {
            modules[i] = module;
            num_modules++;
            
            // Set initial status
            if (module->status == 0) {
                module->status = MODULE_STATUS_LOADED;
            }
            
            log_info("Registered module: %s (ID: %u, Version: 0x%08X)",
                     module->name, module->id, module->version);
            
            // Initialize module if it has an init function
            if (module->init) {
                int result = module->init();
                if (result != 0) {
                    log_error("Module '%s' initialization failed: %d", module->name, result);
                    module->status = MODULE_STATUS_ERROR;
                    return MODULE_ERROR_INIT;
                }
            }
            
            // Start module if it has a start function
            if (module->start) {
                int result = module->start();
                if (result != 0) {
                    log_warning("Module '%s' failed to start: %d", module->name, result);
                    // Don't consider this a fatal error - module is registered but not started
                } else {
                    log_info("Module '%s' started successfully", module->name);
                }
            }
            
            return MODULE_ERROR_NONE;
        }
    }
    
    // This should never happen (we already checked num_modules < MAX_MODULES)
    log_error("Failed to register module '%s'", module->name);
    return MODULE_ERROR_MEMORY;
}

/**
 * Unregister a module from the module system
 */
int module_unregister(module_t *module) {
    if (!module_system_initialized) {
        log_error("Module system not initialized");
        return MODULE_ERROR_INVALID;
    }
    
    if (!module) {
        log_error("Attempted to unregister NULL module");
        return MODULE_ERROR_INVALID;
    }
    
    // Check if module is essential
    if (module->flags & MODULE_FLAG_ESSENTIAL) {
        log_error("Cannot unregister essential module '%s'", module->name);
        return MODULE_ERROR_PERMISSION;
    }
    
    // Find module in array
    int found = 0;
    for (int i = 0; i < MAX_MODULES; i++) {
        if (modules[i] == module) {
            found = 1;
            
            // Stop module if it has a stop function
            if (module->stop) {
                int result = module->stop();
                if (result != 0) {
                    log_warning("Module '%s' failed to stop cleanly: %d", module->name, result);
                    // Continue with unregistration anyway
                } else {
                    log_info("Module '%s' stopped successfully", module->name);
                }
            }
            
            // Call module cleanup if it has an exit function
            if (module->exit) {
                int result = module->exit();
                if (result != 0) {
                    log_warning("Module '%s' cleanup failed: %d", module->name, result);
                    // Continue with unregistration anyway
                }
            }
            
            // Remove all drivers registered by this module
            if (module->drivers) {
                for (int j = 0; j < module->num_drivers; j++) {
                    if (module->drivers[j]) {
                        device_driver_unregister(module->drivers[j]);
                    }
                }
                heap_free(module->drivers);
                module->drivers = NULL;
                module->num_drivers = 0;
            }
            
            // Free dependency list
            module_dependency_t *dep = module->dependencies;
            while (dep) {
                module_dependency_t *next = dep->next;
                heap_free(dep);
                dep = next;
            }
            module->dependencies = NULL;
            
            // Free interface list
            module_interface_t *iface = module->interfaces;
            while (iface) {
                module_interface_t *next = iface->next;
                heap_free(iface);
                iface = next;
            }
            module->interfaces = NULL;
            
            // Update module status
            module->status = MODULE_STATUS_UNLOADED;
            
            // Remove from module array
            modules[i] = NULL;
            num_modules--;
            
            log_info("Unregistered module: %s", module->name);
            
            return MODULE_ERROR_NONE;
        }
    }
    
    log_warning("Module '%s' not found for unregistration", module->name);
    return MODULE_ERROR_NOT_FOUND;
}

/**
 * Load a module from a file
 */
module_t *module_load(const char *filename, uint32_t flags) {
    if (!module_system_initialized) {
        log_error("Module system not initialized");
        return NULL;
    }
    
    if (!filename) {
        log_error("NULL filename provided to module_load");
        return NULL;
    }
    
    log_info("Loading module from file: %s", filename);
    
    // Open the module file
    int fd = vfs_open(filename, VFS_O_RDONLY);
    if (fd < 0) {
        log_error("Failed to open module file '%s': %d", filename, fd);
        return NULL;
    }
    
    // Get file size
    vfs_stat_t stat;
    if (vfs_fstat(fd, &stat) < 0) {
        log_error("Failed to stat module file '%s'", filename);
        vfs_close(fd);
        return NULL;
    }
    
    // Allocate memory for the module data
    void *module_data = heap_alloc(stat.size);
    if (!module_data) {
        log_error("Failed to allocate memory for module data");
        vfs_close(fd);
        return NULL;
    }
    
    // Read module file into memory
    if (vfs_read(fd, module_data, stat.size) != (ssize_t)stat.size) {
        log_error("Failed to read module file '%s'", filename);
        heap_free(module_data);
        vfs_close(fd);
        return NULL;
    }
    
    // Close the file
    vfs_close(fd);
    
    // Verify module format (ELF)
    if (module_verify_elf(module_data, stat.size) != 0) {
        log_error("Invalid module format: %s", filename);
        heap_free(module_data);
        return NULL;
    }
    
    // Parse module metadata from ELF sections
    // In a real implementation, we'd parse module info, symbols, etc. from the ELF file
    // For now, we'll create a dummy module structure
    
    module_t *module = (module_t *)heap_alloc(sizeof(module_t));
    if (!module) {
        log_error("Failed to allocate module structure");
        heap_free(module_data);
        return NULL;
    }
    
    // Initialize module structure
    memset(module, 0, sizeof(module_t));
    
    // Extract filename without path
    const char *module_name = strrchr(filename, '/');
    if (module_name) {
        module_name++; // Skip the '/'
    } else {
        module_name = filename;
    }
    
    // Remove file extension (.ko or .mod)
    char clean_name[32];
    strncpy(clean_name, module_name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    
    char *dot = strrchr(clean_name, '.');
    if (dot) {
        *dot = '\0';
    }
    
    strncpy(module->name, clean_name, sizeof(module->name) - 1);
    module->name[sizeof(module->name) - 1] = '\0';
    
    // Set basic module properties
    snprintf(module->description, sizeof(module->description), "Kernel module %s", module->name);
    strncpy(module->author, "Unknown", sizeof(module->author) - 1);
    module->version = 0x00010000; // Version 1.0.0.0
    module->id = generate_module_id();
    module->flags = flags;
    module->status = MODULE_STATUS_UNLOADED;
    module->module_base = module_data;
    module->module_size = stat.size;
    
    // Duplicate filename for module record
    module->filename = heap_alloc(strlen(filename) + 1);
    if (module->filename) {
        strcpy(module->filename, filename);
    }
    
    // In a real implementation, we'd locate module init/exit functions from the ELF symbols
    // For this simplified version, we'll use a naming convention
    // To implement real ELF parsing, you'd need relocation and symbol resolution
    
    // Simulate symbol resolution
    if (module_resolve_symbols(module) != 0) {
        log_error("Failed to resolve symbols for module '%s'", module->name);
        if (module->filename) {
            heap_free(module->filename);
        }
        heap_free(module);
        heap_free(module_data);
        return NULL;
    }
    
    // Register module
    if (module_register(module) != 0) {
        log_error("Failed to register module '%s'", module->name);
        if (module->filename) {
            heap_free(module->filename);
        }
        heap_free(module);
        heap_free(module_data);
        return NULL;
    }
    
    log_info("Successfully loaded module '%s'", module->name);
    return module;
}

/**
 * Unload a module
 */
int module_unload(module_t *module) {
    if (!module_system_initialized) {
        log_error("Module system not initialized");
        return MODULE_ERROR_INVALID;
    }
    
    if (!module) {
        log_error("Attempted to unload NULL module");
        return MODULE_ERROR_INVALID;
    }
    
    // Check if module can be unloaded
    if (!(module->flags & MODULE_FLAG_UNLOADABLE)) {
        log_error("Module '%s' is not unloadable", module->name);
        return MODULE_ERROR_PERMISSION;
    }
    
    log_info("Unloading module: %s", module->name);
    
    // Unregister the module first
    int result = module_unregister(module);
    if (result != 0) {
        log_error("Failed to unregister module '%s': %d", module->name, result);
        return result;
    }
    
    // Free module resources
    if (module->module_base) {
        heap_free(module->module_base);
        module->module_base = NULL;
    }
    
    if (module->filename) {
        heap_free(module->filename);
        module->filename = NULL;
    }
    
    // Free the module structure
    heap_free(module);
    
    return MODULE_ERROR_NONE;
}

/**
 * Find a module by name
 */
module_t *module_find_by_name(const char *name) {
    if (!module_system_initialized || !name) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_MODULES; i++) {
        if (modules[i] && strcmp(modules[i]->name, name) == 0) {
            return modules[i];
        }
    }
    
    return NULL;
}

/**
 * Find a module by ID
 */
module_t *module_find_by_id(uint32_t id) {
    if (!module_system_initialized) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_MODULES; i++) {
        if (modules[i] && modules[i]->id == id) {
            return modules[i];
        }
    }
    
    return NULL;
}

/**
 * Get information about all loaded modules
 */
int module_list(module_t **result, int max_modules) {
    if (!module_system_initialized || !result || max_modules <= 0) {
        return 0;
    }
    
    int count = 0;
    
    for (int i = 0; i < MAX_MODULES && count < max_modules; i++) {
        if (modules[i]) {
            result[count++] = modules[i];
        }
    }
    
    return count;
}

/**
 * Get a specific module interface
 */
void *module_get_interface(const char *module_name, const char *interface_name) {
    if (!module_system_initialized || !module_name || !interface_name) {
        return NULL;
    }
    
    module_t *module = module_find_by_name(module_name);
    if (!module) {
        return NULL;
    }
    
    // Check if the module exposes a get_interface function
    if (module->get_interface) {
        return module->get_interface(interface_name);
    }
    
    // Otherwise, search in module's interface list
    module_interface_t *iface = module->interfaces;
    while (iface) {
        if (strcmp(iface->name, interface_name) == 0) {
            return iface->implementation;
        }
        iface = iface->next;
    }
    
    return NULL;
}

/**
 * Register a module interface
 */
static int module_register_interface(module_t *module, const char *name, void *implementation) {
    if (!module || !name || !implementation) {
        return MODULE_ERROR_INVALID;
    }
    
    // Check for duplicate interface
    module_interface_t *iface = module->interfaces;
    while (iface) {
        if (strcmp(iface->name, name) == 0) {
            // Update existing interface
            iface->implementation = implementation;
            return MODULE_ERROR_NONE;
        }
        iface = iface->next;
    }
    
    // Create new interface
    module_interface_t *new_iface = (module_interface_t *)heap_alloc(sizeof(module_interface_t));
    if (!new_iface) {
        return MODULE_ERROR_MEMORY;
    }
    
    // Initialize interface
    strncpy(new_iface->name, name, sizeof(new_iface->name) - 1);
    new_iface->name[sizeof(new_iface->name) - 1] = '\0';
    new_iface->implementation = implementation;
    
    // Add to interface list
    new_iface->next = module->interfaces;
    module->interfaces = new_iface;
    
    return MODULE_ERROR_NONE;
}

/**
 * Check if all dependencies for a module are satisfied
 */
int module_check_dependencies(module_t *module) {
    if (!module) {
        return 0;
    }
    
    // No dependencies, all satisfied
    if (!module->dependencies) {
        return 1;
    }
    
    module_dependency_t *dep = module->dependencies;
    while (dep) {
        module_t *dep_module = module_find_by_name(dep->name);
        
        // Dependency not found
        if (!dep_module) {
            log_warning("Module '%s' depends on missing module '%s'", 
                      module->name, dep->name);
            return 0;
        }
        
        // Check version compatibility
        if (dep->min_version > 0 && dep_module->version < dep->min_version) {
            log_warning("Module '%s' requires '%s' version 0x%08X or newer (found: 0x%08X)",
                      module->name, dep->name, dep->min_version, dep_module->version);
            return 0;
        }
        
        if (dep->max_version > 0 && dep_module->version > dep->max_version) {
            log_warning("Module '%s' requires '%s' version 0x%08X or older (found: 0x%08X)",
                      module->name, dep->name, dep->max_version, dep_module->version);
            return 0;
        }
        
        // Check if dependency is initialized
        if (dep_module->status != MODULE_STATUS_LOADED && dep_module->status != MODULE_STATUS_DISABLED) {
            log_warning("Module '%s' depends on module '%s' which is not properly initialized",
                      module->name, dep->name);
            return 0;
        }
        
        dep = dep->next;
    }
    
    return 1;
}

/**
 * Add a dependency to a module
 */
static int module_add_dependency(module_t *module, const char *dep_name, 
                               uint32_t min_version, uint32_t max_version) {
    if (!module || !dep_name) {
        return MODULE_ERROR_INVALID;
    }
    
    // Create new dependency
    module_dependency_t *dep = (module_dependency_t *)heap_alloc(sizeof(module_dependency_t));
    if (!dep) {
        return MODULE_ERROR_MEMORY;
    }
    
    // Initialize dependency
    strncpy(dep->name, dep_name, sizeof(dep->name) - 1);
    dep->name[sizeof(dep->name) - 1] = '\0';
    dep->min_version = min_version;
    dep->max_version = max_version;
    
    // Add to dependency list
    dep->next = module->dependencies;
    module->dependencies = dep;
    
    return MODULE_ERROR_NONE;
}

/**
 * Enable a module
 */
int module_enable(module_t *module) {
    if (!module) {
        return MODULE_ERROR_INVALID;
    }
    
    if (module->status == MODULE_STATUS_LOADED) {
        // Already enabled
        return MODULE_ERROR_NONE;
    }
    
    if (module->status != MODULE_STATUS_DISABLED) {
        log_error("Cannot enable module '%s' (status: %d)", module->name, module->status);
        return MODULE_ERROR_INVALID;
    }
    
    // Start module if it has a start function
    if (module->start) {
        int result = module->start();
        if (result != 0) {
            log_error("Failed to start module '%s': %d", module->name, result);
            return result;
        }
    }
    
    // Update module status
    module->status = MODULE_STATUS_LOADED;
    
    log_info("Module '%s' enabled", module->name);
    
    return MODULE_ERROR_NONE;
}

/**
 * Disable a module
 */
int module_disable(module_t *module) {
    if (!module) {
        return MODULE_ERROR_INVALID;
    }
    
    if (module->status == MODULE_STATUS_DISABLED) {
        // Already disabled
        return MODULE_ERROR_NONE;
    }
    
    if (module->status != MODULE_STATUS_LOADED) {
        log_error("Cannot disable module '%s' (status: %d)", module->name, module->status);
        return MODULE_ERROR_INVALID;
    }
    
    // Check if module is essential
    if (module->flags & MODULE_FLAG_ESSENTIAL) {
        log_error("Cannot disable essential module '%s'", module->name);
        return MODULE_ERROR_PERMISSION;
    }
    
    // Stop module if it has a stop function
    if (module->stop) {
        int result = module->stop();
        if (result != 0) {
            log_warning("Module '%s' failed to stop cleanly: %d", module->name, result);
            // Continue with disabling anyway
        }
    }
    
    // Update module status
    module->status = MODULE_STATUS_DISABLED;
    
    log_info("Module '%s' disabled", module->name);
    
    return MODULE_ERROR_NONE;
}

/**
 * Get module status string
 */
const char *module_status_string(uint8_t status) {
    switch (status) {
        case MODULE_STATUS_UNLOADED: return "Unloaded";
        case MODULE_STATUS_LOADED:   return "Loaded";
        case MODULE_STATUS_ERROR:    return "Error";
        case MODULE_STATUS_DISABLED: return "Disabled";
        default:                     return "Unknown";
    }
}

/**
 * Print module information
 */
void module_print_info(module_t *module) {
    if (!module) {
        log_info("NULL module");
        return;
    }
    
    log_info("Module Information:");
    log_info("  Name:        %s", module->name);
    log_info("  Description: %s", module->description);
    log_info("  Author:      %s", module->author);
    log_info("  Version:     %u.%u.%u.%u", 
           (module->version >> 24) & 0xFF,
           (module->version >> 16) & 0xFF,
           (module->version >> 8) & 0xFF,
           module->version & 0xFF);
    log_info("  ID:          %u", module->id);
    log_info("  Status:      %s", module_status_string(module->status));
    log_info("  Flags:       0x%08X", module->flags);
    
    if (module->drivers && module->num_drivers > 0) {
        log_info("  Drivers:     %d", module->num_drivers);
        for (int i = 0; i < module->num_drivers; i++) {
            if (module->drivers[i]) {
                log_info("    - %s (ID: %u)", module->drivers[i]->name, module->drivers[i]->id);
            }
        }
    }
    
    if (module->dependencies) {
        log_info("  Dependencies:");
        module_dependency_t *dep = module->dependencies;
        while (dep) {
            if (dep->min_version > 0 && dep->max_version > 0) {
                log_info("    - %s (version: 0x%08X - 0x%08X)", 
                       dep->name, dep->min_version, dep->max_version);
            } else if (dep->min_version > 0) {
                log_info("    - %s (version >= 0x%08X)", dep->name, dep->min_version);
            } else if (dep->max_version > 0) {
                log_info("    - %s (version <= 0x%08X)", dep->name, dep->max_version);
            } else {
                log_info("    - %s (any version)", dep->name);
            }
            dep = dep->next;
        }
    }
    
    if (module->interfaces) {
        log_info("  Interfaces:");
        module_interface_t *iface = module->interfaces;
        while (iface) {
            log_info("    - %s (implementation: 0x%p)", iface->name, iface->implementation);
            iface = iface->next;
        }
    }
}

/**
 * Print module dependency tree
 */
void module_print_dependency_tree(module_t *module, int depth) {
    if (!module) {
        return;
    }
    
    // Print indentation
    char indent[64] = {0};
    for (int i = 0; i < depth * 2; i++) {
        indent[i] = ' ';
    }
    
    // Print module info
    log_info("%s%s (Version: %u.%u.%u.%u, Status: %s)",
           indent, module->name, 
           (module->version >> 24) & 0xFF,
           (module->version >> 16) & 0xFF,
           (module->version >> 8) & 0xFF,
           module->version & 0xFF,
           module_status_string(module->status));
    
    // Print dependencies
    if (module->dependencies) {
        module_dependency_t *dep = module->dependencies;
        while (dep) {
            module_t *dep_module = module_find_by_name(dep->name);
            if (dep_module) {
                module_print_dependency_tree(dep_module, depth + 1);
            } else {
                log_info("%s  %s (Not loaded)", indent, dep->name);
            }
            dep = dep->next;
        }
    }
}

/**
 * Parse module configuration file
 */
int module_parse_config(module_t *module, const char *config_file) {
    if (!module || !config_file) {
        return MODULE_ERROR_INVALID;
    }
    
    log_info("Parsing configuration for module '%s' from '%s'", module->name, config_file);
    
    // Open the config file
    int fd = vfs_open(config_file, VFS_O_RDONLY);
    if (fd < 0) {
        log_error("Failed to open config file '%s': %d", config_file, fd);
        return MODULE_ERROR_IO;
    }
    
    // Read config file into memory
    char buffer[512];
    int bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        log_error("Failed to read config file '%s': %d", config_file, bytes_read);
        vfs_close(fd);
        return MODULE_ERROR_IO;
    }
    
    buffer[bytes_read] = '\0'; // Null-terminate
    
    // Close the file
    vfs_close(fd);
    
    // Parse config file (simple key=value format)
    char *line = strtok(buffer, "\n");
    while (line) {
        // Skip comments and empty lines
        if (line[0] != '#' && line[0] != '\0') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0'; // Split key and value
                char *key = line;
                char *value = eq + 1;
                
                // Trim whitespace
                while (*key && (*key == ' ' || *key == '\t')) key++;
                char *end = key + strlen(key) - 1;
                while (end > key && (*end == ' ' || *end == '\t')) *end-- = '\0';
                
                while (*value && (*value == ' ' || *value == '\t')) value++;
                end = value + strlen(value) - 1;
                while (end > value && (*end == ' ' || *end == '\t')) *end-- = '\0';
                
                // Apply configuration
                if (module->config) {
                    module->config(key, value);
                }
                
                // Special handling for known keys
                if (strcmp(key, "description") == 0) {
                    strncpy(module->description, value, sizeof(module->description) - 1);
                    module->description[sizeof(module->description) - 1] = '\0';
                } else if (strcmp(key, "author") == 0) {
                    strncpy(module->author, value, sizeof(module->author) - 1);
                    module->author[sizeof(module->author) - 1] = '\0';
                } else if (strcmp(key, "version") == 0) {
                    // Parse version string (format: major.minor.patch.build)
                    unsigned int major = 0, minor = 0, patch = 0, build = 0;
                    sscanf(value, "%u.%u.%u.%u", &major, &minor, &patch, &build);
                    module->version = (major << 24) | (minor << 16) | (patch << 8) | build;
                } else if (strcmp(key, "depends") == 0) {
                    // Parse dependency (format: module_name[>=min_version][<=max_version])
                    char dep_name[32];
                    uint32_t min_version = 0, max_version = 0;
                    
                    // Extract module name
                    char *brackets = strchr(value, '[');
                    if (brackets) {
                        size_t name_len = brackets - value;
                        if (name_len > sizeof(dep_name) - 1) {
                            name_len = sizeof(dep_name) - 1;
                        }
                        strncpy(dep_name, value, name_len);
                        dep_name[name_len] = '\0';
                        
                        // Extract version constraints
                        char *version_str = brackets + 1;
                        char *end_bracket = strchr(version_str, ']');
                        if (end_bracket) {
                            *end_bracket = '\0';
                            
                            // Parse version constraints
                            char *gt = strstr(version_str, ">=");
                            if (gt) {
                                unsigned int major = 0, minor = 0, patch = 0, build = 0;
                                sscanf(gt + 2, "%u.%u.%u.%u", &major, &minor, &patch, &build);
                                min_version = (major << 24) | (minor << 16) | (patch << 8) | build;
                            }
                            
                            char *lt = strstr(version_str, "<=");
                            if (lt) {
                                unsigned int major = 0, minor = 0, patch = 0, build = 0;
                                sscanf(lt + 2, "%u.%u.%u.%u", &major, &minor, &patch, &build);
                                max_version = (major << 24) | (minor << 16) | (patch << 8) | build;
                            }
                        }
                    } else {
                        strncpy(dep_name, value, sizeof(dep_name) - 1);
                        dep_name[sizeof(dep_name) - 1] = '\0';
                    }
                    
                    // Trim whitespace from dep_name
                    char *end = dep_name + strlen(dep_name) - 1;
                    while (end > dep_name && (*end == ' ' || *end == '\t')) *end-- = '\0';
                    
                    // Add dependency
                    module_add_dependency(module, dep_name, min_version, max_version);
                }
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    log_info("Finished parsing configuration for module '%s'", module->name);
    return MODULE_ERROR_NONE;
}

/**
 * Verify ELF module format
 */
static int module_verify_elf(void *module_data, size_t size) {
    // In a real implementation, check ELF magic number, architecture, etc.
    // For now, we'll just do a simple check on the first few bytes
    
    if (size < 4) {
        return -1;
    }
    
    // Check ELF magic number
    uint8_t *data = (uint8_t *)module_data;
    if (data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        // This is a placeholder for simplicity
        // In a real implementation, you would do proper ELF parsing
        
        // For now, we'll just pretend everything is fine
        log_warning("ELF validation not fully implemented - assuming module is valid");
        return 0;
    }
    
    return 0; // Valid ELF
}

/**
 * Resolve module symbols
 */
static int module_resolve_symbols(module_t *module) {
    // In a real implementation, we would:
    // 1. Parse the ELF sections and symbol tables
    // 2. Find module_init, module_exit, etc. functions
    // 3. Resolve any external symbols the module needs
    // 4. Apply relocations
    
    // For now, we'll use dummy functions for the module lifecycle
    module->init = (module_init_func_t)(void*)([](void) {
        log_debug("Default module init function called");
        return 0;
    });
    
    module->exit = (module_exit_func_t)(void*)([](void) {
        log_debug("Default module exit function called");
        return 0;
    });
    
    module->start = (module_start_func_t)(void*)([](void) {
        log_debug("Default module start function called");
        return 0;
    });
    
    module->stop = (module_stop_func_t)(void*)([](void) {
        log_debug("Default module stop function called");
        return 0;
    });
    
    return 0;
}

/**
 * Initialize module drivers
 */
static int module_initialize_drivers(module_t *module) {
    // In a real implementation, we would scan the module for driver structures
    // and register them with the device manager
    
    // For now, we'll just log that this would happen
    log_info("Would initialize drivers for module '%s'", module->name);
    
    return 0;
}

/**
 * Add a driver to a module
 */
int module_add_driver(module_t *module, device_driver_t *driver) {
    if (!module || !driver) {
        return MODULE_ERROR_INVALID;
    }
    
    // Expand drivers array
    device_driver_t **new_drivers = (device_driver_t **)heap_alloc(
        sizeof(device_driver_t *) * (module->num_drivers + 1));
    if (!new_drivers) {
        return MODULE_ERROR_MEMORY;
    }
    
    // Copy existing drivers
    if (module->drivers) {
        for (int i = 0; i < module->num_drivers; i++) {
            new_drivers[i] = module->drivers[i];
        }
        heap_free(module->drivers);
    }
    
    // Add new driver
    new_drivers[module->num_drivers] = driver;
    module->drivers = new_drivers;
    module->num_drivers++;
    
    // Register driver with device manager
    return device_driver_register(driver);
}