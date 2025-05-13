/**
 * @file kernel/config.c
 * @brief Kernel Configuration Implementation
 *
 * This file implements the kernel configuration functionality.
 */

#include "config.h"
#include "logging/log.h"
#include <string.h>

// The global kernel configuration instance
static kernel_config_t kernel_config;
static int config_initialized = 0;

/**
 * Get the current kernel configuration
 */
kernel_config_t* kernel_get_config(void) {
    // Initialize with defaults if needed
    if (!config_initialized) {
        kernel_init_config();
    }
    
    return &kernel_config;
}

/**
 * Set a kernel configuration parameter
 */
int kernel_set_config(const char* key, const char* value) {
    if (!key || !value) {
        return -1;
    }
    
    // Initialize with defaults if needed
    if (!config_initialized) {
        kernel_init_config();
    }
    
    // Handle ASLR configuration parameters
    if (strcmp(key, "aslr.enabled") == 0) {
        kernel_config.has_aslr_config = 1;
        kernel_config.aslr_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        log_debug("CONFIG", "Set aslr.enabled = %d", kernel_config.aslr_enabled);
        return 0;
    }
    
    if (strcmp(key, "aslr.entropy_bits") == 0) {
        kernel_config.has_aslr_config = 1;
        int entropy = atoi(value);
        if (entropy < 8) entropy = 8;
        if (entropy > 24) entropy = 24;
        kernel_config.aslr_entropy_bits = (uint8_t)entropy;
        log_debug("CONFIG", "Set aslr.entropy_bits = %d", kernel_config.aslr_entropy_bits);
        return 0;
    }
    
    if (strcmp(key, "aslr.flags") == 0) {
        kernel_config.has_aslr_config = 1;
        kernel_config.aslr_flags = strtoul(value, NULL, 0);
        log_debug("CONFIG", "Set aslr.flags = 0x%X", kernel_config.aslr_flags);
        return 0;
    }
    
    // Handle other configuration parameters
    // ...
    
    return -1;  // Unknown key
}

/**
 * Load kernel configuration from a file
 */
int kernel_load_config(const char* filename) {
    if (!filename) {
        return -1;
    }
    
    log_info("CONFIG", "Loading kernel configuration from '%s'", filename);
    
    // Open the configuration file
    // For now, since we don't have a complete filesystem implementation,
    // we'll just return success
    
    return 0;
}

/**
 * Save current kernel configuration to a file
 */
int kernel_save_config(const char* filename) {
    if (!filename) {
        return -1;
    }
    
    log_info("CONFIG", "Saving kernel configuration to '%s'", filename);
    
    // Save the configuration to a file
    // For now, since we don't have a complete filesystem implementation,
    // we'll just return success
    
    return 0;
}

/**
 * Initialize the kernel configuration with default values
 */
void kernel_init_config(void) {
    log_info("CONFIG", "Initializing kernel configuration with default values");
    
    // Clear the config structure
    memset(&kernel_config, 0, sizeof(kernel_config));
    
    // Set system information
    strncpy(kernel_config.system_name, "SampleOS", sizeof(kernel_config.system_name) - 1);
    strncpy(kernel_config.system_version, "1.0.0", sizeof(kernel_config.system_version) - 1);
    
    // Memory management defaults
    kernel_config.physical_mem_limit = 0;       // No limit
    kernel_config.kernel_heap_size = 16 * 1024; // 16MB
    kernel_config.page_cache_size = 4 * 1024;   // 4MB
    
    // Process management defaults
    kernel_config.max_processes = 256;
    kernel_config.max_threads = 1024;
    kernel_config.default_stack_size = 256;     // 256KB
    kernel_config.process_timeout_ms = 0;       // No timeout
    
    // Security options
    kernel_config.security_enabled = true;
    kernel_config.security_level = 2;           // Medium security
    
    // ASLR defaults
    kernel_config.has_aslr_config = true;
    kernel_config.aslr_enabled = true;
    kernel_config.aslr_entropy_bits = 16;       // Medium entropy
    kernel_config.aslr_flags = 0x3F;            // All memory regions
    
    // Virtual memory configuration
    kernel_config.kernel_protection = true;
    kernel_config.use_nx_bit = true;
    kernel_config.use_shared_page_tables = false;
    
    // Boot options
    kernel_config.verbose_boot = false;
    kernel_config.debug_mode = false;
    kernel_config.safe_mode = false;
    
    config_initialized = 1;
    
    log_info("CONFIG", "Kernel configuration initialized");
}
