/**
 * @file kernel/config.h
 * @brief Kernel Configuration Parameters
 *
 * This file defines the kernel configuration structure and related 
 * functions for retrieving and managing kernel settings.
 */

#ifndef KERNEL_CONFIG_H
#define KERNEL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Configuration parameters for the kernel
 */
typedef struct {
    // System information
    char system_name[32];           // System name
    char system_version[16];        // System version
    
    // Memory management
    uint32_t physical_mem_limit;    // Physical memory limit in KB (0 = no limit)
    uint32_t kernel_heap_size;      // Kernel heap size in KB
    uint32_t page_cache_size;       // Page cache size in KB
    
    // Process management
    uint32_t max_processes;         // Maximum number of processes
    uint32_t max_threads;           // Maximum number of threads
    uint32_t default_stack_size;    // Default stack size in KB
    uint32_t process_timeout_ms;    // Process execution timeout in milliseconds
    
    // Security options
    bool security_enabled;          // Whether security subsystem is enabled
    uint32_t security_level;        // Security level (0=none, 1=low, 2=medium, 3=high)
    
    // ASLR configuration
    bool has_aslr_config;           // Whether ASLR config is valid
    bool aslr_enabled;              // Whether ASLR is enabled
    uint8_t aslr_entropy_bits;      // Number of bits of entropy (8-24)
    uint32_t aslr_flags;            // Which memory regions to randomize
    
    // Virtual memory configuration
    bool kernel_protection;         // Whether kernel memory is protected
    bool use_nx_bit;                // Whether to use NX bit for data pages
    bool use_shared_page_tables;    // Whether to share page tables between processes
    
    // Boot options
    bool verbose_boot;              // Whether to display verbose boot messages
    bool debug_mode;                // Whether debug mode is enabled
    bool safe_mode;                 // Whether safe mode is enabled
    char boot_command_line[256];    // Boot command line
    
} kernel_config_t;

/**
 * Get the current kernel configuration
 *
 * @return Pointer to the current kernel configuration
 */
kernel_config_t* kernel_get_config(void);

/**
 * Set a kernel configuration parameter
 *
 * @param key   Configuration parameter key
 * @param value Configuration parameter value
 * @return 0 on success, -1 on failure
 */
int kernel_set_config(const char* key, const char* value);

/**
 * Load kernel configuration from a file
 *
 * @param filename Path to configuration file
 * @return 0 on success, -1 on failure
 */
int kernel_load_config(const char* filename);

/**
 * Save current kernel configuration to a file
 *
 * @param filename Path to configuration file
 * @return 0 on success, -1 on failure
 */
int kernel_save_config(const char* filename);

/**
 * Initialize the kernel configuration with default values
 */
void kernel_init_config(void);

#endif /* KERNEL_CONFIG_H */
