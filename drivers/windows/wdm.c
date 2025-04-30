/**
 * Windows Driver Model (WDM) Compatibility Layer Implementation
 * 
 * This file implements the Windows driver compatibility layer for uintOS,
 * allowing limited support for Windows drivers.
 * 
 * Version: 1.0
 * Date: May 1, 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "wdm.h"
#include "../../hal/include/hal.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"

// Current IRQL level
static KIRQL current_irql = PASSIVE_LEVEL;

// Driver module status
static bool wdm_initialized = false;

// Maximum number of drivers that can be loaded
#define MAX_DRIVERS 16

// Array of loaded drivers
static PDRIVER_OBJECT loaded_drivers[MAX_DRIVERS];
static int num_loaded_drivers = 0;

// Driver loader status tracking
typedef struct {
    PDRIVER_OBJECT driver;
    const char* name;
    bool loaded;
    bool initialized;
} driver_status_t;

// Driver module tag for memory allocations
#define WDM_TAG 0x004D4457  // "WDM"

/**
 * Initialize the Windows Driver Model compatibility layer
 */
NTSTATUS WdmInitialize(void) {
    if (wdm_initialized) {
        log_warning("WDM", "Windows Driver Model compatibility layer already initialized");
        return STATUS_SUCCESS;
    }
    
    log_info("WDM", "Initializing Windows Driver Model compatibility layer");
    
    // Initialize driver tracking
    memset(loaded_drivers, 0, sizeof(loaded_drivers));
    num_loaded_drivers = 0;
    
    // Set initial IRQL
    current_irql = PASSIVE_LEVEL;
    
    wdm_initialized = true;
    log_info("WDM", "Windows Driver Model compatibility layer initialized");
    
    return STATUS_SUCCESS;
}

/**
 * Shutdown the Windows Driver Model compatibility layer
 */
void WdmShutdown(void) {
    if (!wdm_initialized) {
        log_warning("WDM", "Windows Driver Model compatibility layer not initialized");
        return;
    }
    
    log_info("WDM", "Shutting down Windows Driver Model compatibility layer");
    
    // Unload all drivers in reverse order of loading
    for (int i = num_loaded_drivers - 1; i >= 0; i--) {
        if (loaded_drivers[i] != NULL) {
            log_info("WDM", "Automatically unloading driver: %s", 
                     loaded_drivers[i]->DriverName ? loaded_drivers[i]->DriverName : "unnamed");
            
            // Call the driver's unload routine if available
            if (loaded_drivers[i]->DriverUnload != NULL) {
                typedef void (*UnloadFunc)(PDRIVER_OBJECT);
                UnloadFunc unload = (UnloadFunc)loaded_drivers[i]->DriverUnload;
                
                log_debug("WDM", "Calling driver unload routine");
                unload(loaded_drivers[i]);
            }
            
            // Free the driver object
            ExFreePoolWithTag(loaded_drivers[i], WDM_TAG);
            loaded_drivers[i] = NULL;
        }
    }
    
    num_loaded_drivers = 0;
    wdm_initialized = false;
    log_info("WDM", "Windows Driver Model compatibility layer shutdown complete");
}

/**
 * Load a Windows driver
 * 
 * Note: This is a simplified implementation. A real implementation would:
 * 1. Load the driver binary from disk
 * 2. Resolve imports
 * 3. Relocate the driver if needed
 * 4. Call the driver's entry point
 */
NTSTATUS WdmLoadDriver(wdm_driver_config_t* config, PDRIVER_OBJECT* driver_object) {
    NTSTATUS status = STATUS_SUCCESS;
    PDRIVER_OBJECT drv_obj = NULL;
    
    if (!wdm_initialized) {
        log_error("WDM", "Windows Driver Model compatibility layer not initialized");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (config == NULL || driver_object == NULL) {
        log_error("WDM", "Invalid parameters");
        return STATUS_INVALID_PARAMETER;
    }
    
    log_info("WDM", "Loading driver: %s", config->driver_name);
    
    // Check if we have room for another driver
    if (num_loaded_drivers >= MAX_DRIVERS) {
        log_error("WDM", "Maximum number of drivers already loaded");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Allocate the driver object
    drv_obj = (PDRIVER_OBJECT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DRIVER_OBJECT), WDM_TAG);
    if (drv_obj == NULL) {
        log_error("WDM", "Failed to allocate driver object");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize the driver object
    memset(drv_obj, 0, sizeof(DRIVER_OBJECT));
    drv_obj->Size = sizeof(DRIVER_OBJECT);
    drv_obj->DriverName = config->driver_name;
    drv_obj->Flags = config->driver_flags;
    
    // In a real implementation, we would:
    // 1. Load the driver image from disk
    // 2. Find the entry point
    // 3. Call the entry point with the driver object
    
    // For now, we'll simulate a successful load
    log_info("WDM", "Driver loaded successfully: %s", config->driver_name);
    
    // Add to the list of loaded drivers
    loaded_drivers[num_loaded_drivers++] = drv_obj;
    
    // Return the driver object to the caller
    *driver_object = drv_obj;
    
    return STATUS_SUCCESS;
}

/**
 * Unload a Windows driver
 */
NTSTATUS WdmUnloadDriver(PDRIVER_OBJECT driver_object) {
    if (!wdm_initialized) {
        log_error("WDM", "Windows Driver Model compatibility layer not initialized");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (driver_object == NULL) {
        log_error("WDM", "Invalid parameters");
        return STATUS_INVALID_PARAMETER;
    }
    
    log_info("WDM", "Unloading driver: %s", 
             driver_object->DriverName ? driver_object->DriverName : "unnamed");
    
    // Find the driver in our list
    int index = -1;
    for (int i = 0; i < num_loaded_drivers; i++) {
        if (loaded_drivers[i] == driver_object) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        log_error("WDM", "Driver not found in loaded drivers list");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Call the driver's unload routine if available
    if (driver_object->DriverUnload != NULL) {
        typedef void (*UnloadFunc)(PDRIVER_OBJECT);
        UnloadFunc unload = (UnloadFunc)driver_object->DriverUnload;
        
        log_debug("WDM", "Calling driver unload routine");
        unload(driver_object);
    }
    
    // Remove from the list of loaded drivers
    loaded_drivers[index] = NULL;
    
    // Compact the list
    for (int i = index; i < num_loaded_drivers - 1; i++) {
        loaded_drivers[i] = loaded_drivers[i + 1];
    }
    loaded_drivers[--num_loaded_drivers] = NULL;
    
    // Free the driver object
    ExFreePoolWithTag(driver_object, WDM_TAG);
    
    log_info("WDM", "Driver unloaded successfully");
    
    return STATUS_SUCCESS;
}

/**
 * Create a device object for a driver
 */
NTSTATUS WdmCreateDevice(
    PDRIVER_OBJECT driver_object,
    const char* device_name,
    uint32_t device_type,
    PDEVICE_OBJECT* device_object
) {
    PDEVICE_OBJECT dev_obj = NULL;
    
    if (!wdm_initialized) {
        log_error("WDM", "Windows Driver Model compatibility layer not initialized");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (driver_object == NULL || device_name == NULL || device_object == NULL) {
        log_error("WDM", "Invalid parameters");
        return STATUS_INVALID_PARAMETER;
    }
    
    log_info("WDM", "Creating device object: %s", device_name);
    
    // Allocate the device object
    dev_obj = (PDEVICE_OBJECT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DEVICE_OBJECT), WDM_TAG);
    if (dev_obj == NULL) {
        log_error("WDM", "Failed to allocate device object");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize the device object
    memset(dev_obj, 0, sizeof(DEVICE_OBJECT));
    dev_obj->Size = sizeof(DEVICE_OBJECT);
    dev_obj->Type = device_type;
    dev_obj->ReferenceCount = 1;
    dev_obj->DriverObject = driver_object;
    dev_obj->DeviceType = device_type;
    dev_obj->StackSize = 1;  // Default stack size
    
    // Add the device to the driver's device chain
    dev_obj->NextDevice = (PDEVICE_OBJECT)driver_object->DeviceObject;
    driver_object->DeviceObject = dev_obj;
    
    log_info("WDM", "Device object created successfully: %s", device_name);
    
    // Return the device object to the caller
    *device_object = dev_obj;
    
    return STATUS_SUCCESS;
}

/**
 * Send an IRP to a device
 */
NTSTATUS WdmSubmitIrp(
    PDEVICE_OBJECT device_object,
    uint32_t major_function,
    uint32_t minor_function,
    void* buffer,
    uint32_t buffer_length,
    PIO_STATUS_BLOCK io_status_block
) {
    PIRP irp = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    
    if (!wdm_initialized) {
        log_error("WDM", "Windows Driver Model compatibility layer not initialized");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (device_object == NULL || io_status_block == NULL) {
        log_error("WDM", "Invalid parameters");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Get the driver object
    PDRIVER_OBJECT driver_object = device_object->DriverObject;
    if (driver_object == NULL) {
        log_error("WDM", "Device object has no associated driver");
        return STATUS_INVALID_PARAMETER;
    }
    
    log_debug("WDM", "Submitting IRP: major=%u, minor=%u", 
              major_function, minor_function);
    
    // Allocate an IRP
    irp = (PIRP)ExAllocatePoolWithTag(NonPagedPool, sizeof(IRP), WDM_TAG);
    if (irp == NULL) {
        log_error("WDM", "Failed to allocate IRP");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize the IRP
    memset(irp, 0, sizeof(IRP));
    irp->Type = 1;  // IRP type
    irp->Size = sizeof(IRP);
    irp->UserBuffer = buffer;
    irp->Tail.Parameters.MajorFunction = major_function;
    irp->Tail.Parameters.MinorFunction = minor_function;
    irp->UserIosb = io_status_block;
    
    // Initialize the status block
    io_status_block->Status = STATUS_PENDING;
    io_status_block->Information = 0;
    
    // Check if the driver has a handler for this major function
    if (major_function >= 28 || driver_object->MajorFunction[major_function] == NULL) {
        log_error("WDM", "Driver has no handler for major function %u", major_function);
        ExFreePoolWithTag(irp, WDM_TAG);
        return STATUS_NOT_IMPLEMENTED;
    }
    
    // Call the driver's dispatch routine
    typedef NTSTATUS (*DispatchFunc)(PDEVICE_OBJECT, PIRP);
    DispatchFunc dispatch = (DispatchFunc)driver_object->MajorFunction[major_function];
    
    log_debug("WDM", "Calling driver dispatch routine");
    status = dispatch(device_object, irp);
    
    // Check if the IRP is pending
    if (status == STATUS_PENDING) {
        log_debug("WDM", "IRP is pending, waiting for completion");
        
        // In a real implementation, we would wait for the IRP to complete
        // For now, we'll assume it completes immediately with success
        io_status_block->Status = STATUS_SUCCESS;
        io_status_block->Information = buffer_length;
        status = STATUS_SUCCESS;
    }
    
    // Copy the status block back to the caller
    io_status_block->Status = status;
    
    // Free the IRP
    ExFreePoolWithTag(irp, WDM_TAG);
    
    log_debug("WDM", "IRP completed with status %08Xh", status);
    
    return status;
}

/**
 * Windows Kernel API Emulation Functions
 */

/**
 * Allocate memory (Windows compatibility)
 */
void* ExAllocatePoolWithTag(uint32_t pool_type, uint32_t size, uint32_t tag) {
    void* memory = NULL;
    
    // Allocate memory using uintOS heap functions
    memory = malloc(size);
    
    if (memory != NULL) {
        // In a real implementation, we would store the tag and pool type
        // for debugging and memory tracking purposes
        log_debug("WDM", "Allocated %u bytes with tag %c%c%c%c",
                  size,
                  (tag >> 24) & 0xFF,
                  (tag >> 16) & 0xFF,
                  (tag >> 8) & 0xFF,
                  tag & 0xFF);
    }
    
    return memory;
}

/**
 * Free memory (Windows compatibility)
 */
void ExFreePoolWithTag(void* memory, uint32_t tag) {
    if (memory != NULL) {
        // Free memory using uintOS heap functions
        free(memory);
        
        log_debug("WDM", "Freed memory with tag %c%c%c%c",
                  (tag >> 24) & 0xFF,
                  (tag >> 16) & 0xFF,
                  (tag >> 8) & 0xFF,
                  tag & 0xFF);
    }
}

/**
 * Raise IRQL (Windows compatibility)
 */
void KeRaiseIrql(KIRQL new_irql, KIRQL* old_irql) {
    if (old_irql != NULL) {
        *old_irql = current_irql;
    }
    
    // In a real implementation, this would disable interrupts based on the IRQL level
    log_debug("WDM", "Raising IRQL from %u to %u", current_irql, new_irql);
    
    current_irql = new_irql;
    
    // For IRQL levels above DISPATCH_LEVEL, we would disable interrupts
    if (new_irql >= DISPATCH_LEVEL) {
        // Disable interrupts
        hal_interrupt_disable();
    }
}

/**
 * Lower IRQL (Windows compatibility)
 */
void KeLowerIrql(KIRQL new_irql) {
    if (new_irql > current_irql) {
        log_warning("WDM", "Attempt to lower IRQL to a higher level (current=%u, new=%u)",
                   current_irql, new_irql);
        return;
    }
    
    log_debug("WDM", "Lowering IRQL from %u to %u", current_irql, new_irql);
    
    // For IRQL levels below DISPATCH_LEVEL, we would enable interrupts
    if (current_irql >= DISPATCH_LEVEL && new_irql < DISPATCH_LEVEL) {
        // Enable interrupts
        hal_interrupt_enable();
    }
    
    current_irql = new_irql;
}

/**
 * Get current IRQL (Windows compatibility)
 */
KIRQL KeGetCurrentIrql(void) {
    return current_irql;
}

/**
 * Map device registers to memory (Windows compatibility)
 */
void* MmMapIoSpace(uint64_t base_address, uint32_t length) {
    void* virtual_address = NULL;
    
    log_debug("WDM", "Mapping I/O space: physical=0x%llx, length=%u", base_address, length);
    
    // Use HAL functions to map physical memory
    hal_status_t status = hal_memory_map_physical(
        base_address,
        length,
        HAL_MEMORY_UNCACHEABLE,
        &virtual_address
    );
    
    if (status != HAL_SUCCESS) {
        log_error("WDM", "Failed to map I/O space: error=%d", status);
        return NULL;
    }
    
    log_debug("WDM", "I/O space mapped: physical=0x%llx, virtual=0x%p", base_address, virtual_address);
    
    return virtual_address;
}

/**
 * Unmap device registers (Windows compatibility)
 */
void MmUnmapIoSpace(void* virtual_address) {
    if (virtual_address == NULL) {
        return;
    }
    
    log_debug("WDM", "Unmapping I/O space: virtual=0x%p", virtual_address);
    
    // Use HAL functions to unmap memory
    hal_status_t status = hal_memory_unmap(virtual_address);
    
    if (status != HAL_SUCCESS) {
        log_warning("WDM", "Failed to unmap I/O space: error=%d", status);
    }
}