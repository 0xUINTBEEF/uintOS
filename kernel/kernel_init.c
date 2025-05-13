/**
 * @file kernel_init.c
 * @brief Kernel initialization routines
 *
 * This file contains the kernel initialization code including
 * startup of various subsystems like ASLR.
 */

#include "memory/aslr.h"
#include "memory/vmm.h"
#include "kernel/logging/log.h"
#include "hal/include/hal_io.h"
#include "kernel/config.h"

// Default kernel configuration values
#define DEFAULT_ASLR_ENABLED   1       // ASLR enabled by default
#define DEFAULT_ASLR_ENTROPY   16      // 16 bits of entropy (medium)
#define DEFAULT_ASLR_FLAGS     0x0000003F  // All randomization enabled

/**
 * Initialize kernel subsystems
 * This is called early in the boot process after basic
 * memory management is set up.
 */
void kernel_init_subsystems(void) {
    log_info("KERNEL", "Initializing kernel subsystems");
    
    // Initialize ASLR
    int aslr_enabled = DEFAULT_ASLR_ENABLED;
    uint8_t entropy_bits = DEFAULT_ASLR_ENTROPY;
    uint32_t aslr_flags = DEFAULT_ASLR_FLAGS;
    
    // Check if config has overridden the defaults
    kernel_config_t* config = kernel_get_config();
    if (config) {
        if (config->has_aslr_config) {
            aslr_enabled = config->aslr_enabled;
            entropy_bits = config->aslr_entropy_bits;
            aslr_flags = config->aslr_flags;
        }
    }
    
    // Initialize ASLR subsystem
    log_info("KERNEL", "Initializing ASLR: enabled=%d, entropy=%d bits", 
             aslr_enabled, entropy_bits);
    
    aslr_init(aslr_enabled, entropy_bits, aslr_flags);
    
    // Register other kernel subsystems here
    
    log_info("KERNEL", "Kernel subsystems initialized");
}

/**
 * Process command line parameters from the bootloader
 * This allows configuration via boot parameters
 * 
 * @param cmdline Command line string from bootloader
 */
void kernel_process_cmdline(const char* cmdline) {
    if (!cmdline) {
        return;
    }
    
    log_debug("KERNEL", "Processing command line: %s", cmdline);
    
    // Check for ASLR-related parameters
    if (strstr(cmdline, "aslr=off") || strstr(cmdline, "noaslr")) {
        log_info("KERNEL", "ASLR disabled via command line");
        aslr_set_enabled(0);
    }
    
    if (strstr(cmdline, "aslr=low")) {
        log_info("KERNEL", "ASLR entropy set to LOW via command line");
        aslr_set_entropy(ASLR_ENTROPY_LOW);
    } else if (strstr(cmdline, "aslr=high")) {
        log_info("KERNEL", "ASLR entropy set to HIGH via command line");
        aslr_set_entropy(ASLR_ENTROPY_HIGH);
    }
}
