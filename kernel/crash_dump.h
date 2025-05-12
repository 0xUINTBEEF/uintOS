#ifndef CRASH_DUMP_H
#define CRASH_DUMP_H

#include <stdint.h>
#include <stdbool.h>
#include "panic.h"
#include "exception_handlers.h"
#include "debug_registers.h"

/**
 * @file crash_dump.h
 * @brief Crash dump functionality for post-mortem analysis
 *
 * This file provides structures and functions for recording system state
 * during a kernel panic and analyzing crash dumps after reboot.
 */

// Crash dump signature to identify valid dumps
#define CRASH_DUMP_SIGNATURE   0x504D5544  // "DUMP"
#define CRASH_DUMP_VERSION     0x00010000  // Version 1.0.0

// Maximum number of memory regions to include in crash dump
#define MAX_MEMORY_REGIONS     16

// Maximum length for kernel module name 
#define MODULE_NAME_MAX_LENGTH 32

// Maximum stack trace depth
#define CRASH_STACK_DEPTH      32

// Maximum size of stored memory regions for inspection
#define MEMORY_SAMPLE_SIZE     256

// Maximum length of panic message
#define MAX_PANIC_MESSAGE      256

// Maximum length of file path + function name
#define MAX_SOURCE_INFO        128

/**
 * Crash dump file header
 */
typedef struct {
    uint32_t signature;       // CRASH_DUMP_SIGNATURE
    uint32_t version;         // Format version
    uint64_t timestamp;       // Time of crash
    uint32_t panic_type;      // Type of panic that caused the dump
    char panic_message[MAX_PANIC_MESSAGE]; // Panic message
    char source_file[MAX_SOURCE_INFO];     // Source file of panic
    uint32_t source_line;     // Line number of panic
    char source_func[MAX_SOURCE_INFO];     // Function of panic
    uint64_t uptime_ms;       // System uptime in milliseconds
    
    // CPU state at the time of panic
    interrupt_frame_t cpu_state;
    debug_registers_t debug_regs;
    
    // Stack trace
    uint32_t stack_trace_count;
    uint32_t stack_trace[CRASH_STACK_DEPTH];
    
    // Memory region samples
    uint32_t memory_region_count;
    struct {
        uint32_t address;
        uint32_t size;
        uint32_t offset;      // Offset in the dump file
    } memory_regions[MAX_MEMORY_REGIONS];
    
    // Active tasks information
    uint32_t task_count;
    struct {
        uint32_t id;
        char name[32];
        uint32_t state;
        uint32_t stack_base;
        uint32_t stack_size;
    } tasks[16];
    
    // Module information
    uint32_t module_count;
    struct {
        char name[MODULE_NAME_MAX_LENGTH];
        uint32_t base_addr;
        uint32_t size;
    } modules[8];
    
} crash_dump_header_t;

/**
 * Initialize the crash dump subsystem
 * 
 * @return 0 on success, non-zero on error
 */
int crash_dump_init(void);

/**
 * Create a crash dump when a panic occurs
 * 
 * @param type Panic type
 * @param file Source file where panic occurred
 * @param line Line number where panic occurred
 * @param func Function where panic occurred
 * @param message Panic message
 * @param frame CPU register state at time of panic
 * @return true if dump was created successfully
 */
bool crash_dump_create(
    panic_type_t type,
    const char* file,
    int line,
    const char* func,
    const char* message,
    interrupt_frame_t* frame
);

/**
 * Analyze a crash dump from a previous panic
 * 
 * @param dump_id Identifier of crash dump to analyze
 * @return true if analysis was successful
 */
bool crash_dump_analyze(const char* dump_id);

/**
 * Check if a crash dump exists from previous run
 * 
 * @return true if a crash dump exists
 */
bool crash_dump_exists(void);

/**
 * List available crash dumps
 * 
 * @return Number of crash dumps found
 */
int crash_dump_list(void);

#endif /* CRASH_DUMP_H */
