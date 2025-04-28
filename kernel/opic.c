#include "opic.h"
#include "io.h"
#include <stddef.h>

/* --------- OPIC Global Variables ---------- */
static uint32_t opic_base_address = 0;
static uint16_t opic_num_sources = 0;
static uint8_t opic_num_cpus = 0;
static uint8_t opic_initialized = 0;

/**
 * Read from an OPIC register
 * 
 * @param reg: Register offset to read from
 * @return: Value read from the register
 */
static inline uint32_t opic_read_reg(uint32_t reg) {
    return *(volatile uint32_t*)(opic_base_address + reg);
}

/**
 * Write to an OPIC register
 * 
 * @param reg: Register offset to write to
 * @param value: Value to write
 */
static inline void opic_write_reg(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(opic_base_address + reg) = value;
}

/**
 * Get the register offset for a specific source
 * 
 * @param source_num: Source number
 * @param reg_offset: Register offset within the source block
 * @return: Full register offset
 */
static inline uint32_t opic_source_reg(uint8_t source_num, uint8_t reg_offset) {
    return OPIC_SOURCE_BASE + (source_num * OPIC_SOURCE_SIZE) + reg_offset;
}

/**
 * Initialize the OPIC subsystem
 *
 * @param base_addr: Physical base address of the OPIC registers
 * @param num_sources: Number of interrupt sources to support
 * @param num_cpus: Number of CPUs in the system
 * @return 0 on success, -1 on failure
 */
int opic_init(uint32_t base_addr, uint16_t num_sources, uint8_t num_cpus) {
    uint32_t global_config;
    
    // Validate parameters
    if (base_addr == 0 || num_sources == 0 || num_cpus == 0) {
        return -1;
    }
    
    // Store parameters
    opic_base_address = base_addr;
    opic_num_sources = num_sources;
    opic_num_cpus = num_cpus;
    
    // Reset the controller
    global_config = opic_read_reg(OPIC_GLOBAL_CONFIG);
    global_config |= OPIC_GLOBAL_RESET;
    opic_write_reg(OPIC_GLOBAL_CONFIG, global_config);
    
    // Wait for reset to complete (bit should self-clear)
    while (opic_read_reg(OPIC_GLOBAL_CONFIG) & OPIC_GLOBAL_RESET) {
        // Simple busy wait
    }
    
    // Read vendor ID and feature register for validation
    uint32_t vendor_id = opic_read_reg(OPIC_VENDOR_ID);
    uint32_t feature_reg = opic_read_reg(OPIC_FEATURE_REG);
    
    // In a real implementation, we would validate these values
    
    // Configure for standard operation
    global_config = 0; // Start with clean config
    
    // Enable 8259 pass-through if needed
    // global_config |= OPIC_GLOBAL_8259_ENABLE;
    
    opic_write_reg(OPIC_GLOBAL_CONFIG, global_config);
    
    // Set up spurious vector register
    opic_write_reg(OPIC_SPURIOUS_VECTOR, 0xFF); // Use vector 0xFF for spurious
    
    // Initialize all CPUs
    for (uint8_t cpu = 0; cpu < num_cpus; cpu++) {
        // Set all CPUs to lowest priority
        opic_set_cpu_priority(cpu, 0);
    }
    
    // Mask all interrupt sources initially
    for (uint16_t src = 0; src < num_sources; src++) {
        uint32_t reg_offset = opic_source_reg(src, OPIC_SOURCE_VECTOR);
        uint32_t vec_reg = opic_read_reg(reg_offset);
        vec_reg |= OPIC_VEC_MASK; // Set mask bit
        opic_write_reg(reg_offset, vec_reg);
    }
    
    // Mark as initialized
    opic_initialized = 1;
    
    return 0;
}

/**
 * Configure an OPIC interrupt source
 *
 * @param source: Pointer to source configuration structure
 * @return 0 on success, -1 on failure
 */
int opic_configure_source(const opic_source_t *source) {
    if (!opic_initialized || !source || source->source_num >= opic_num_sources) {
        return -1;
    }
    
    uint32_t vec_reg = 0;
    
    // Set vector number
    vec_reg |= (source->vector & OPIC_VEC_VECTOR_MASK);
    
    // Set priority
    uint32_t priority = source->priority & 0xF; // Ensure 4-bit value
    vec_reg |= (priority << OPIC_VEC_PRIORITY_SHIFT);
    
    // Set level/edge triggered mode
    if (source->is_level) {
        vec_reg |= OPIC_VEC_LEVEL_TRIGGER;
    }
    
    // Set active high/low
    if (source->is_active_low) {
        vec_reg |= OPIC_VEC_ACTIVE_LOW;
    }
    
    // Set mask bit
    if (source->is_masked) {
        vec_reg |= OPIC_VEC_MASK;
    }
    
    // Write to vector/priority register
    uint32_t reg_offset = opic_source_reg(source->source_num, OPIC_SOURCE_VECTOR);
    opic_write_reg(reg_offset, vec_reg);
    
    // Configure destination
    reg_offset = opic_source_reg(source->source_num, OPIC_SOURCE_DESTINATION);
    opic_write_reg(reg_offset, source->destination & OPIC_DEST_CPU_MASK);
    
    return 0;
}

/**
 * Enable an OPIC interrupt source
 *
 * @param source_num: Source number to enable
 * @return 0 on success, -1 on failure
 */
int opic_enable_source(uint8_t source_num) {
    if (!opic_initialized || source_num >= opic_num_sources) {
        return -1;
    }
    
    uint32_t reg_offset = opic_source_reg(source_num, OPIC_SOURCE_VECTOR);
    uint32_t vec_reg = opic_read_reg(reg_offset);
    
    // Clear the mask bit to enable
    vec_reg &= ~OPIC_VEC_MASK;
    
    opic_write_reg(reg_offset, vec_reg);
    
    return 0;
}

/**
 * Disable an OPIC interrupt source
 *
 * @param source_num: Source number to disable
 * @return 0 on success, -1 on failure
 */
int opic_disable_source(uint8_t source_num) {
    if (!opic_initialized || source_num >= opic_num_sources) {
        return -1;
    }
    
    uint32_t reg_offset = opic_source_reg(source_num, OPIC_SOURCE_VECTOR);
    uint32_t vec_reg = opic_read_reg(reg_offset);
    
    // Set the mask bit to disable
    vec_reg |= OPIC_VEC_MASK;
    
    opic_write_reg(reg_offset, vec_reg);
    
    return 0;
}

/**
 * Send an Inter-Processor Interrupt (IPI)
 *
 * @param ipi: Pointer to IPI configuration structure
 * @return 0 on success, -1 on failure
 */
int opic_send_ipi(const opic_ipi_t *ipi) {
    if (!opic_initialized || !ipi || ipi->ipi_num > 3) {
        return -1;
    }
    
    // Calculate the IPI vector/priority register for this IPI number
    uint32_t ipi_reg = OPIC_IPI_VECTOR_REGISTER + (ipi->ipi_num * 0x10);
    
    // Configure the IPI
    uint32_t ipi_val = 0;
    
    // Set vector number
    ipi_val |= (ipi->vector & OPIC_VEC_VECTOR_MASK);
    
    // Set priority
    uint32_t priority = ipi->priority & 0xF; // Ensure 4-bit value
    ipi_val |= (priority << OPIC_VEC_PRIORITY_SHIFT);
    
    // Write to IPI vector/priority register
    opic_write_reg(ipi_reg, ipi_val);
    
    // Write destination to IPI destination register
    opic_write_reg(ipi_reg + 0x10, ipi->destination & OPIC_DEST_CPU_MASK);
    
    // Writing to the destination register triggers the IPI
    
    return 0;
}

/**
 * Signal End-Of-Interrupt for an OPIC interrupt
 *
 * @param source_num: Source number that was serviced
 * @return 0 on success, -1 on failure
 */
int opic_eoi(uint8_t source_num) {
    if (!opic_initialized) {
        return -1;
    }
    
    // In OPIC, EOI is signaled by writing to the EOI register of the current CPU
    // Source number is typically ignored in OPIC
    // Each CPU has its own EOI register at a fixed offset from the base
    
    // This implementation assumes a simple offset for each CPU's EOI register
    // In a real implementation, this would be based on the CPU's register region
    
    // Get current CPU ID (in a real OS, this would use a proper CPU ID function)
    uint8_t current_cpu = 0; // Assume CPU 0 for simplicity
    
    // Calculate EOI register for this CPU
    uint32_t eoi_reg = 0x40000 + (current_cpu * 0x1000) + 0x80;
    
    // Writing any value signals EOI
    opic_write_reg(eoi_reg, 0);
    
    return 0;
}

/**
 * Check if an OPIC interrupt is pending
 *
 * @param source_num: Source number to check
 * @return 1 if pending, 0 if not pending, -1 on failure
 */
int opic_is_pending(uint8_t source_num) {
    if (!opic_initialized || source_num >= opic_num_sources) {
        return -1;
    }
    
    // In OPIC, pending status is typically available in a per-source register
    // or a global pending register that can be read by the CPU
    
    // This implementation assumes a simple pending register for each source
    // In a real implementation, this would depend on the specific OPIC hardware
    
    uint32_t pending_reg = opic_source_reg(source_num, 0x20); // Assumed offset for pending status
    uint32_t pending = opic_read_reg(pending_reg);
    
    return (pending & 1) ? 1 : 0;
}

/**
 * Set the priority of a CPU in the OPIC system
 *
 * @param cpu: CPU number
 * @param priority: Priority level (0-15)
 * @return 0 on success, -1 on failure
 */
int opic_set_cpu_priority(uint8_t cpu, uint8_t priority) {
    if (!opic_initialized || cpu >= opic_num_cpus) {
        return -1;
    }
    
    // Ensure priority is in valid range
    priority &= 0xF; // 4-bit value
    
    // Calculate CPU's priority register
    // In OPIC, each CPU has its own register set at a fixed offset
    
    // This implementation assumes a simple offset for each CPU's priority register
    uint32_t prio_reg = 0x40000 + (cpu * 0x1000) + 0x80;
    
    // Write the priority value
    opic_write_reg(prio_reg, priority);
    
    return 0;
}

/**
 * Get the current interrupt vector being serviced by a CPU
 *
 * @param cpu: CPU number
 * @return Vector number if successful, -1 on failure
 */
int opic_get_current_vector(uint8_t cpu) {
    if (!opic_initialized || cpu >= opic_num_cpus) {
        return -1;
    }
    
    // Calculate CPU's current task priority register
    // This typically contains information about the current interrupt
    
    uint32_t curr_reg = 0x40000 + (cpu * 0x1000) + 0xA0;
    uint32_t curr_val = opic_read_reg(curr_reg);
    
    // Extract vector from register (typically low 8 bits)
    return curr_val & 0xFF;
}

/**
 * Spurious interrupt handler for OPIC
 * This should be called when an unexpected interrupt occurs
 */
void opic_spurious_handler(void) {
    // Handle spurious interrupt
    // In a real OS, this might log the event or take other action
    
    // No EOI is needed for spurious interrupts
}