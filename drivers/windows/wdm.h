/**
 * Windows Driver Model (WDM) Compatibility Layer for uintOS
 * 
 * This header provides compatibility interfaces for loading and using
 * Windows drivers within uintOS. It implements key WDM structures and
 * functions that allow limited Windows driver support.
 * 
 * Version: 1.0
 * Date: May 1, 2025
 */

#ifndef UINTOS_WDM_H
#define UINTOS_WDM_H

#include <stdint.h>
#include <stdbool.h>
#include "../../hal/include/hal.h"
#include "../../kernel/logging/log.h"

/**
 * Windows driver status codes mapped to uintOS equivalents
 */
typedef enum {
    STATUS_SUCCESS                   = 0x00000000,
    STATUS_UNSUCCESSFUL              = 0xC0000001,
    STATUS_NOT_IMPLEMENTED           = 0xC0000002,
    STATUS_INVALID_PARAMETER         = 0xC000000D,
    STATUS_INSUFFICIENT_RESOURCES    = 0xC000009A,
    STATUS_DEVICE_NOT_READY          = 0xC00000A3,
    STATUS_DEVICE_CONFIGURATION_ERROR = 0xC0000182
} NTSTATUS;

/**
 * IRQL (Interrupt Request Level) definitions
 */
typedef enum {
    PASSIVE_LEVEL    = 0,
    APC_LEVEL        = 1,
    DISPATCH_LEVEL   = 2,
    DIRQL_MINIMUM    = 3,
    DIRQL_MAXIMUM    = 12
} KIRQL;

/**
 * Basic Windows driver structures
 */
typedef struct _DRIVER_OBJECT {
    uint16_t            Size;
    void*               DriverStart;
    uint32_t            DriverSize;
    const char*         DriverName;
    void*               DriverInit;
    void*               DriverUnload;
    void*               MajorFunction[28];  // IRP major function handlers
    uint32_t            Flags;
    void*               DeviceObject;
} DRIVER_OBJECT, *PDRIVER_OBJECT;

typedef struct _DEVICE_OBJECT {
    uint16_t            Type;
    uint16_t            Size;
    int32_t             ReferenceCount;
    struct _DRIVER_OBJECT* DriverObject;
    struct _DEVICE_OBJECT* NextDevice;
    uint32_t            Characteristics;
    void*               DeviceExtension;
    uint32_t            DeviceType;
    uint8_t             StackSize;
    void*               Reserved[4];
} DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        void*    Pointer;
    };
    uint32_t Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _IRP {
    uint16_t            Type;
    uint16_t            Size;
    void*               MdlAddress;
    uint32_t            Flags;
    void*               AssociatedIrp;
    void*               ThreadListEntry;
    IO_STATUS_BLOCK     IoStatus;
    int8_t              RequestorMode;
    uint8_t             PendingReturned;
    int8_t              StackCount;
    int8_t              CurrentLocation;
    uint8_t             Cancel;
    uint8_t             CancelIrql;
    int8_t              ApcEnvironment;
    uint8_t             AllocationFlags;
    PIO_STATUS_BLOCK    UserIosb;
    void*               UserEvent;
    void*               Overlay;
    void*               CancelRoutine;
    void*               UserBuffer;
    struct {
        struct {
            uint32_t    MajorFunction;
            uint32_t    MinorFunction;
            uint32_t    Flags;
            uint32_t    Control;
        } Parameters;
    } Tail;
} IRP, *PIRP;

/**
 * Memory allocation types
 */
#define NonPagedPool    0
#define PagedPool       1

/**
 * Windows Driver Configuration
 */
typedef struct {
    const char* driver_path;       // Path to the Windows driver file
    const char* driver_name;       // Name of the driver
    uint32_t    driver_flags;      // Driver flags
    bool        enable_logging;    // Enable verbose logging
    uint32_t    timeout_ms;        // Timeout for operations in milliseconds
} wdm_driver_config_t;

/**
 * Public API for Windows driver support
 */

/**
 * Initialize the Windows Driver Model compatibility layer
 * 
 * @return STATUS_SUCCESS on success, error code otherwise
 */
NTSTATUS WdmInitialize(void);

/**
 * Shutdown the Windows Driver Model compatibility layer
 */
void WdmShutdown(void);

/**
 * Load a Windows driver
 * 
 * @param config Driver configuration
 * @param driver_object Pointer to receive the driver object
 * @return STATUS_SUCCESS on success, error code otherwise
 */
NTSTATUS WdmLoadDriver(wdm_driver_config_t* config, PDRIVER_OBJECT* driver_object);

/**
 * Unload a Windows driver
 * 
 * @param driver_object Driver object to unload
 * @return STATUS_SUCCESS on success, error code otherwise 
 */
NTSTATUS WdmUnloadDriver(PDRIVER_OBJECT driver_object);

/**
 * Create a device object for a driver
 * 
 * @param driver_object Driver object
 * @param device_name Name of the device
 * @param device_type Type of device
 * @param device_object Pointer to receive the device object
 * @return STATUS_SUCCESS on success, error code otherwise
 */
NTSTATUS WdmCreateDevice(
    PDRIVER_OBJECT driver_object,
    const char* device_name,
    uint32_t device_type,
    PDEVICE_OBJECT* device_object
);

/**
 * Send an IRP to a device
 * 
 * @param device_object Target device
 * @param major_function Major function code
 * @param minor_function Minor function code
 * @param buffer I/O buffer
 * @param buffer_length Length of the buffer
 * @param io_status_block Status block to receive results
 * @return STATUS_SUCCESS on success, error code otherwise
 */
NTSTATUS WdmSubmitIrp(
    PDEVICE_OBJECT device_object,
    uint32_t major_function,
    uint32_t minor_function,
    void* buffer,
    uint32_t buffer_length,
    PIO_STATUS_BLOCK io_status_block
);

/**
 * Function exported for Windows drivers
 */

/**
 * Allocate memory (Windows compatibility)
 * 
 * @param pool_type Pool type (paged or non-paged)
 * @param size Size in bytes
 * @param tag Tag for the allocation
 * @return Pointer to allocated memory, NULL on failure
 */
void* ExAllocatePoolWithTag(uint32_t pool_type, uint32_t size, uint32_t tag);

/**
 * Free memory (Windows compatibility)
 * 
 * @param memory Pointer to memory
 * @param tag Tag used for allocation
 */
void ExFreePoolWithTag(void* memory, uint32_t tag);

/**
 * Raise IRQL (Windows compatibility)
 * 
 * @param new_irql New IRQL level
 * @param old_irql Pointer to receive old IRQL
 */
void KeRaiseIrql(KIRQL new_irql, KIRQL* old_irql);

/**
 * Lower IRQL (Windows compatibility)
 * 
 * @param new_irql New IRQL level
 */
void KeLowerIrql(KIRQL new_irql);

/**
 * Get current IRQL (Windows compatibility)
 * 
 * @return Current IRQL level
 */
KIRQL KeGetCurrentIrql(void);

/**
 * Map device registers to memory (Windows compatibility)
 * 
 * @param base_address Physical base address
 * @param length Length of memory region
 * @return Virtual address of mapped region, NULL on failure
 */
void* MmMapIoSpace(uint64_t base_address, uint32_t length);

/**
 * Unmap device registers (Windows compatibility)
 * 
 * @param virtual_address Virtual address from MmMapIoSpace
 */
void MmUnmapIoSpace(void* virtual_address);

#endif /* UINTOS_WDM_H */