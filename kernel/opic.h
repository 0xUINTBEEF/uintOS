#ifndef OPIC_H
#define OPIC_H

#include <inttypes.h>

/*
 * Open Programmable Interrupt Controller (OPIC) Interface
 *
 * The Open PIC architecture is designed to be platform-independent and is commonly
 * found in PowerPC systems, but can be implemented in others as a unified interface
 * for multiprocessor interrupt management. This implementation provides a consistent
 * interface that can wrap different underlying hardware (PIC, APIC, or custom).
 */

/* OPIC Register Offsets */
#define OPIC_FEATURE_REG          0x01000  // Feature reporting register
#define OPIC_GLOBAL_CONFIG        0x01020  // Global configuration
#define OPIC_VENDOR_ID            0x01080  // Vendor identification
#define OPIC_PROCESSOR_INIT       0x01090  // Processor initialization
#define OPIC_IPI_VECTOR_REGISTER  0x010A0  // IPI vector/priority register
#define OPIC_SPURIOUS_VECTOR      0x010E0  // Spurious vector register
#define OPIC_TIMER_FREQ           0x010F0  // Timer frequency register

/* OPIC Source Registers (per IRQ) */
#define OPIC_SOURCE_BASE          0x10000  // Base for source registers
#define OPIC_SOURCE_SIZE          0x20     // Size of each source register block
#define OPIC_SOURCE_VECTOR        0x00     // Vector/priority register
#define OPIC_SOURCE_DESTINATION   0x10     // Destination register

/* OPIC Global Configuration Register bits */
#define OPIC_GLOBAL_RESET         0x80000000  // Reset the controller
#define OPIC_GLOBAL_8259_ENABLE   0x20000000  // Enable 8259 pass-through mode
#define OPIC_GLOBAL_BASE_MASK     0x000FFFFF  // Mask for base address

/* OPIC Source Vector/Priority Register bits */
#define OPIC_VEC_MASK             0x80000000  // Mask interrupt
#define OPIC_VEC_ACTIVE_LOW       0x00800000  // Active low (vs active high)
#define OPIC_VEC_LEVEL_TRIGGER    0x00400000  // Level triggered (vs edge)
#define OPIC_VEC_PRIORITY_MASK    0x000F0000  // Priority level (0-15)
#define OPIC_VEC_PRIORITY_SHIFT   16          // Shift for priority field
#define OPIC_VEC_VECTOR_MASK      0x000000FF  // Vector number (0-255)

/* OPIC Destination Register bits */
#define OPIC_DEST_BROADCAST       0x80000000  // Send to all CPUs
#define OPIC_DEST_CPU_MASK        0x0000000F  // CPU number (0-15)

/* 
 * OPIC Delivery Modes
 * These define how an interrupt is delivered to the CPU(s)
 */
#define OPIC_DELIVERY_FIXED       0x00000000  // Deliver to specified CPU(s)
#define OPIC_DELIVERY_LOWEST      0x00100000  // Deliver to lowest priority CPU
#define OPIC_DELIVERY_NMI         0x00400000  // Deliver as Non-Maskable Interrupt
#define OPIC_DELIVERY_INIT        0x00500000  // Deliver as INIT signal
#define OPIC_DELIVERY_EXTINT      0x00700000  // Deliver as external interrupt

/*
 * OPIC Interrupt Types
 * These define different sources of interrupts in the system
 */
typedef enum {
    OPIC_EXTERNAL_INT = 0,    // External interrupt (e.g., device)
    OPIC_TIMER_INT = 1,       // Timer interrupt
    OPIC_IPI_INT = 2,         // Inter-processor interrupt
    OPIC_ERROR_INT = 3        // Error interrupt
} opic_interrupt_type_t;

/*
 * OPIC Interrupt Source Structure
 * Represents a single interrupt source in the OPIC system
 */
typedef struct {
    uint8_t  source_num;      // Source number/ID
    uint8_t  priority;        // Priority level (0-15, 15 highest)
    uint8_t  vector;          // Vector number for this interrupt
    uint8_t  destination;     // CPU destination mask
    uint8_t  is_level;        // 1 if level triggered, 0 if edge
    uint8_t  is_active_low;   // 1 if active low, 0 if active high
    uint8_t  is_masked;       // 1 if masked, 0 if enabled
} opic_source_t;

/*
 * OPIC IPI (Inter-Processor Interrupt) Structure
 * Used for communication between CPUs
 */
typedef struct {
    uint8_t  ipi_num;         // IPI number (0-3)
    uint8_t  priority;        // Priority level
    uint8_t  vector;          // Vector number
    uint8_t  destination;     // Target CPU(s)
} opic_ipi_t;

/* Function Prototypes */

/**
 * Initialize the OPIC subsystem
 *
 * @param base_addr: Physical base address of the OPIC registers
 * @param num_sources: Number of interrupt sources to support
 * @param num_cpus: Number of CPUs in the system
 * @return 0 on success, -1 on failure
 */
int opic_init(uint32_t base_addr, uint16_t num_sources, uint8_t num_cpus);

/**
 * Configure an OPIC interrupt source
 *
 * @param source: Pointer to source configuration structure
 * @return 0 on success, -1 on failure
 */
int opic_configure_source(const opic_source_t *source);

/**
 * Enable an OPIC interrupt source
 *
 * @param source_num: Source number to enable
 * @return 0 on success, -1 on failure
 */
int opic_enable_source(uint8_t source_num);

/**
 * Disable an OPIC interrupt source
 *
 * @param source_num: Source number to disable
 * @return 0 on success, -1 on failure
 */
int opic_disable_source(uint8_t source_num);

/**
 * Send an Inter-Processor Interrupt (IPI)
 *
 * @param ipi: Pointer to IPI configuration structure
 * @return 0 on success, -1 on failure
 */
int opic_send_ipi(const opic_ipi_t *ipi);

/**
 * Signal End-Of-Interrupt for an OPIC interrupt
 *
 * @param source_num: Source number that was serviced
 * @return 0 on success, -1 on failure
 */
int opic_eoi(uint8_t source_num);

/**
 * Check if an OPIC interrupt is pending
 *
 * @param source_num: Source number to check
 * @return 1 if pending, 0 if not pending, -1 on failure
 */
int opic_is_pending(uint8_t source_num);

/**
 * Set the priority of a CPU in the OPIC system
 *
 * @param cpu: CPU number
 * @param priority: Priority level (0-15)
 * @return 0 on success, -1 on failure
 */
int opic_set_cpu_priority(uint8_t cpu, uint8_t priority);

/**
 * Get the current interrupt vector being serviced by a CPU
 *
 * @param cpu: CPU number
 * @return Vector number if successful, -1 on failure
 */
int opic_get_current_vector(uint8_t cpu);

/**
 * Spurious interrupt handler for OPIC
 * This should be called when an unexpected interrupt occurs
 */
void opic_spurious_handler(void);

#endif /* OPIC_H */