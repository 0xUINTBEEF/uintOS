#include "crash_dump.h"
#include "panic.h"
#include "filesystem/vfs/vfs.h"
#include "logging/log.h"
#include "task.h"
#include "module.h"
#include <string.h>
#include <stdio.h>

/**
 * @file crash_dump.c
 * @brief Crash dump functionality for post-mortem analysis
 *
 * This file implements functions for recording system state during 
 * a kernel panic and analyzing crash dumps after reboot.
 */

// Crash dump directory path in filesystem
#define CRASH_DUMP_DIR "/sys/crash"

// Maximum number of crash dumps to keep
#define MAX_CRASH_DUMPS 5

// Crash dump file name prefix
#define DUMP_FILE_PREFIX "crash_"

// Crash dump extension
#define DUMP_FILE_EXT ".dmp"

// Flag to track if the crash dump system is ready
static bool crash_dump_ready = false;

/**
 * Initialize the crash dump subsystem
 */
int crash_dump_init(void) {
    // Create crash dump directory if it doesn't exist
    if (vfs_mkdir(CRASH_DUMP_DIR, 0755) != 0) {
        // Directory already exists or error creating it
        // Check if it's actually a directory
        vfs_stat_t stat_buf;
        if (vfs_stat(CRASH_DUMP_DIR, &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode)) {
            log_error("PANIC", "Failed to create crash dump directory");
            return -1;
        }
    }
    
    log_info("KERNEL", "Crash dump system initialized at %s", CRASH_DUMP_DIR);
    crash_dump_ready = true;
    
    // Check for existing crash dumps
    int count = crash_dump_list();
    if (count > 0) {
        log_warning("KERNEL", "Found %d crash dump(s) from previous sessions", count);
    }
    
    return 0;
}

/**
 * Generate a stack trace by walking the stack
 */
static int generate_stack_trace(uint32_t ebp, uint32_t* trace, int max_depth) {
    uint32_t* frame_ptr = (uint32_t*)ebp;
    int depth = 0;
    
    while (depth < max_depth && frame_ptr != NULL && (uint32_t)frame_ptr >= 0x1000) {
        uint32_t saved_eip = frame_ptr[1];
        if (saved_eip == 0) break;
        
        trace[depth++] = saved_eip;
        frame_ptr = (uint32_t*)(frame_ptr[0]);
    }
    
    return depth;
}

/**
 * Create a crash dump file name with timestamp
 */
static void create_dump_filename(char* buffer, size_t size) {
    // Get current system time or uptime
    extern uint64_t uptime_ticks; // Assuming this exists
    
    // Format: crash_TIMESTAMP.dmp
    snprintf(buffer, size, "%s/%s%llu%s", 
             CRASH_DUMP_DIR, DUMP_FILE_PREFIX, uptime_ticks, DUMP_FILE_EXT);
}

/**
 * Create a crash dump when a panic occurs
 */
bool crash_dump_create(
    panic_type_t type,
    const char* file,
    int line,
    const char* func,
    const char* message,
    interrupt_frame_t* frame
) {
    if (!crash_dump_ready || !frame) {
        return false;
    }
    
    // Create a crash dump file
    char filename[64];
    create_dump_filename(filename, sizeof(filename));
    
    // Try to open the file for writing
    vfs_file_t* dump_file = vfs_open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!dump_file) {
        log_error("PANIC", "Failed to create crash dump file: %s", filename);
        return false;
    }
    
    // Initialize the crash dump header
    crash_dump_header_t header;
    memset(&header, 0, sizeof(header));
    
    // Fill in header fields
    header.signature = CRASH_DUMP_SIGNATURE;
    header.version = CRASH_DUMP_VERSION;
    header.timestamp = uptime_ticks; // Assuming uptime_ticks is defined globally
    header.panic_type = type;
    
    // Copy panic message and source information
    strncpy(header.panic_message, message, MAX_PANIC_MESSAGE - 1);
    strncpy(header.source_file, file, MAX_SOURCE_INFO - 1);
    header.source_line = line;
    strncpy(header.source_func, func, MAX_SOURCE_INFO - 1);
    
    // Record system uptime
    header.uptime_ms = uptime_ticks;
    
    // Copy CPU state
    memcpy(&header.cpu_state, frame, sizeof(interrupt_frame_t));
    
    // Get debug registers state
    debug_registers_get_state(&header.debug_regs);
    
    // Generate stack trace from the crashed context
    header.stack_trace_count = generate_stack_trace(
        frame->ebp, header.stack_trace, CRASH_STACK_DEPTH);
    
    // Get active tasks information
    header.task_count = get_active_task_info(
        header.tasks, sizeof(header.tasks) / sizeof(header.tasks[0]));
    
    // Get loaded modules information
    header.module_count = get_loaded_modules_info(
        header.modules, sizeof(header.modules) / sizeof(header.modules[0]));
    
    // Collect memory regions of interest
    header.memory_region_count = 0;
    
    // Add stack memory region
    if (header.memory_region_count < MAX_MEMORY_REGIONS) {
        uint32_t stack_address = frame->esp & ~0xFF; // Align to 256-byte boundary
        header.memory_regions[header.memory_region_count].address = stack_address;
        header.memory_regions[header.memory_region_count].size = MEMORY_SAMPLE_SIZE;
        header.memory_regions[header.memory_region_count].offset = 
            sizeof(crash_dump_header_t) + 
            header.memory_region_count * MEMORY_SAMPLE_SIZE;
        header.memory_region_count++;
    }
    
    // Add code memory region (where the crash happened)
    if (header.memory_region_count < MAX_MEMORY_REGIONS) {
        uint32_t code_address = frame->eip & ~0xFF; // Align to 256-byte boundary
        header.memory_regions[header.memory_region_count].address = code_address;
        header.memory_regions[header.memory_region_count].size = MEMORY_SAMPLE_SIZE;
        header.memory_regions[header.memory_region_count].offset = 
            sizeof(crash_dump_header_t) + 
            header.memory_region_count * MEMORY_SAMPLE_SIZE;
        header.memory_region_count++;
    }
    
    // Write header to file
    if (vfs_write(dump_file, &header, sizeof(header)) != sizeof(header)) {
        vfs_close(dump_file);
        return false;
    }
    
    // Write memory regions data
    for (uint32_t i = 0; i < header.memory_region_count; i++) {
        uint32_t address = header.memory_regions[i].address;
        uint32_t size = header.memory_regions[i].size;
        
        // Check if address is valid before reading from it
        if (address >= 0x1000) { // Basic check to avoid null page
            if (vfs_write(dump_file, (void*)address, size) != size) {
                // Write failed, but continue with the dump
                log_error("PANIC", "Failed to write memory region at 0x%08x", address);
            }
        } else {
            // Write zeros for invalid memory
            char zeros[MEMORY_SAMPLE_SIZE] = {0};
            vfs_write(dump_file, zeros, size);
        }
    }
    
    // Close the file
    vfs_close(dump_file);
    
    log_info("PANIC", "Created crash dump: %s", filename);
    return true;
}

/**
 * Display register values in a human-readable format
 */
static void display_registers(const interrupt_frame_t* regs) {
    printf("Register dump:\n");
    printf("EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x\n", 
           regs->eax, regs->ebx, regs->ecx, regs->edx);
    printf("ESI: 0x%08x  EDI: 0x%08x  EBP: 0x%08x  ESP: 0x%08x\n", 
           regs->esi, regs->edi, regs->ebp, regs->esp);
    printf("EIP: 0x%08x  EFLAGS: 0x%08x\n", regs->eip, regs->eflags);
    printf("CS: 0x%04x  SS: 0x%04x\n", regs->cs, regs->ss);
}

/**
 * Display debug register values in a human-readable format
 */
static void display_debug_registers(const debug_registers_t* regs) {
    printf("\n===== Debug Register State =====\n");
    printf("DR0: 0x%08x  DR1: 0x%08x  DR2: 0x%08x  DR3: 0x%08x\n", 
           regs->dr0, regs->dr1, regs->dr2, regs->dr3);
    printf("DR6: 0x%08x  DR7: 0x%08x\n", regs->dr6, regs->dr7);
    
    // Show debug status register (DR6) details
    printf("\nDebug Status (DR6):\n");
    if (regs->dr6 & DR6_B0) printf("  - Breakpoint 0 triggered\n");
    if (regs->dr6 & DR6_B1) printf("  - Breakpoint 1 triggered\n");
    if (regs->dr6 & DR6_B2) printf("  - Breakpoint 2 triggered\n");
    if (regs->dr6 & DR6_B3) printf("  - Breakpoint 3 triggered\n");
    if (regs->dr6 & DR6_BD) printf("  - Debug register access detected\n");
    if (regs->dr6 & DR6_BS) printf("  - Single-step trap occurred\n");
    if (regs->dr6 & DR6_BT) printf("  - Task switch debug event\n");
    
    // Display debug control register (DR7) details
    printf("\nDebug Control (DR7):\n");
    if (regs->dr7 & DR7_LE) printf("  - Local exact breakpoint enabled\n");
    if (regs->dr7 & DR7_GE) printf("  - Global exact breakpoint enabled\n");
    if (regs->dr7 & DR7_GD) printf("  - General detect enabled\n");
    
    // Display active breakpoints in more detail
    if (regs->dr7 & 0xFF) {
        printf("\nActive Hardware Breakpoints:\n");
        printf("BP# | Address    | Type             | Size | Mode   | Triggered\n");
        printf("----+------------+------------------+------+--------+----------\n");
        
        for (int i = 0; i < 4; i++) {
            if ((regs->dr7 & (1 << (i*2))) || (regs->dr7 & (1 << (i*2+1)))) {
                uint32_t addr = 0;
                switch (i) {
                    case 0: addr = regs->dr0; break;
                    case 1: addr = regs->dr1; break;
                    case 2: addr = regs->dr2; break;
                    case 3: addr = regs->dr3; break;
                }
                
                uint32_t rw_bits = (regs->dr7 >> (16 + i*4)) & 3;
                uint32_t len_bits = (regs->dr7 >> (18 + i*4)) & 3;
                const char* type = "unknown";
                
                switch (rw_bits) {
                    case 0: type = "execution"; break;
                    case 1: type = "data write"; break;
                    case 2: type = "I/O access"; break;
                    case 3: type = "data read/write"; break;
                }
                
                uint32_t size = 1;
                switch (len_bits) {
                    case 0: size = 1; break;
                    case 1: size = 2; break;
                    case 2: size = 8; break; // Only in 64-bit mode
                    case 3: size = 4; break;
                }
                
                // Determine mode (global or local)
                const char* mode = "unknown";
                if ((regs->dr7 & (1 << (i*2))) && (regs->dr7 & (1 << (i*2+1)))) {
                    mode = "both";
                } else if (regs->dr7 & (1 << (i*2))) {
                    mode = "local";
                } else if (regs->dr7 & (1 << (i*2+1))) {
                    mode = "global";
                }
                
                // Check if this breakpoint was triggered
                const char* triggered = (regs->dr6 & (1 << i)) ? "YES" : "no";
                
                printf("%3d | 0x%08x | %-16s | %4d | %-6s | %s\n",
                      i, addr, type, size, mode, triggered);
            }
        }
    } else {
        printf("\nNo active hardware breakpoints\n");
    }
    
    // Show single-step status    if (regs->dr6 & DR6_BS) {
        printf("\nSingle-step mode was active at the time of the crash\n");
        
        // Check if EFLAGS.TF might have been set
        if ((header.cpu_state.eflags & 0x100) != 0) {
            printf("  Trap flag was set in EFLAGS\n");
        }
    }
    
    printf("\n===============================\n");
}

/**
 * Display formatted hexdump of a memory region
 */
static void display_memory_hexdump(const uint8_t* data, size_t size, uint32_t base_addr) {
    for (size_t offset = 0; offset < size; offset += 16) {
        // Print address
        printf("%08x: ", base_addr + offset);
        
        // Print hex bytes
        for (size_t i = 0; i < 16; i++) {
            if (offset + i < size) {
                printf("%02x ", data[offset + i]);
            } else {
                printf("   ");
            }
            
            // Extra space after 8 bytes
            if (i == 7) printf(" ");
        }
        
        // Print ASCII representation
        printf(" |");
        for (size_t i = 0; i < 16; i++) {
            if (offset + i < size) {
                char c = data[offset + i];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            } else {
                printf(" ");
            }
        }
        printf("|\n");
    }
}

/**
 * Analyze a crash dump from a previous panic
 */
bool crash_dump_analyze(const char* dump_id) {
    // If dump_id is NULL or empty, try to open the most recent dump
    char filename[64];
    
    if (!dump_id || !*dump_id) {
        // Find the most recent dump
        // This is a simplification; in reality, we'd need to scan the directory
        // and sort by timestamp, but we'll use a placeholder for now
        strcpy(filename, CRASH_DUMP_DIR "/crash_latest.dmp");
    } else {
        snprintf(filename, sizeof(filename), "%s/%s%s", 
                 CRASH_DUMP_DIR, dump_id, DUMP_FILE_EXT);
    }
    
    // Try to open the crash dump file
    vfs_file_t* dump_file = vfs_open(filename, O_RDONLY, 0);
    if (!dump_file) {
        printf("Error: Could not open crash dump file: %s\n", filename);
        return false;
    }
    
    // Read the header
    crash_dump_header_t header;
    if (vfs_read(dump_file, &header, sizeof(header)) != sizeof(header)) {
        printf("Error: Failed to read crash dump header\n");
        vfs_close(dump_file);
        return false;
    }
    
    // Verify signature and version
    if (header.signature != CRASH_DUMP_SIGNATURE) {
        printf("Error: Invalid crash dump file signature\n");
        vfs_close(dump_file);
        return false;
    }
    
    if ((header.version >> 16) > (CRASH_DUMP_VERSION >> 16)) {
        printf("Warning: Crash dump version is newer than analyzer\n");
    }
    
    // Display crash dump info
    printf("\n===== CRASH DUMP ANALYSIS =====\n\n");
    printf("Timestamp: %llu\n", header.timestamp);
    printf("Uptime: %llu ms\n", header.uptime_ms);
    printf("Panic type: %s (code %d)\n", 
           get_panic_type_string(header.panic_type), header.panic_type);
    printf("Message: %s\n", header.panic_message);
    printf("Location: %s:%d in function %s\n\n", 
           header.source_file, header.source_line, header.source_func);
    
    // Display register values
    display_registers(&header.cpu_state);
    
    // Display debug register state
    display_debug_registers(&header.debug_regs);
    
    // Display stack trace
    printf("\nStack trace:\n");
    for (uint32_t i = 0; i < header.stack_trace_count; i++) {
        printf("[%02d] 0x%08x", i, header.stack_trace[i]);
        
        // Could add symbol lookup here if we had debug info
        printf("\n");
    }
    
    // Display active tasks
    if (header.task_count > 0) {
        printf("\nActive tasks at time of crash:\n");
        printf("ID\tState\tStack Base\tStack Size\tName\n");
        printf("--------------------------------------------------\n");
        
        for (uint32_t i = 0; i < header.task_count; i++) {
            const char* state_str = "Unknown";
            
            // Convert numeric state to string (simplified)
            switch (header.tasks[i].state) {
                case 0: state_str = "READY"; break;
                case 1: state_str = "RUNNING"; break;
                case 2: state_str = "WAITING"; break;
                case 3: state_str = "BLOCKED"; break;
                case 4: state_str = "TERMINATED"; break;
            }
            
            printf("%-4d\t%-8s\t0x%08x\t0x%08x\t%s\n", 
                   header.tasks[i].id, state_str,
                   header.tasks[i].stack_base, header.tasks[i].stack_size, 
                   header.tasks[i].name);
        }
    }
    
    // Display memory regions
    if (header.memory_region_count > 0) {
        printf("\nMemory regions:\n");
        
        for (uint32_t i = 0; i < header.memory_region_count; i++) {
            uint32_t address = header.memory_regions[i].address;
            uint32_t size = header.memory_regions[i].size;
            uint32_t offset = header.memory_regions[i].offset;
            
            printf("\nMemory at 0x%08x - 0x%08x (%d bytes):\n", 
                   address, address + size - 1, size);
            
            // Read memory region data
            uint8_t buffer[MEMORY_SAMPLE_SIZE];
            vfs_seek(dump_file, offset, SEEK_SET);
            if (vfs_read(dump_file, buffer, size) == size) {
                display_memory_hexdump(buffer, size, address);
            } else {
                printf("  [Failed to read memory region data]\n");
            }
        }
    }
    
    // Display loaded modules
    if (header.module_count > 0) {
        printf("\nLoaded modules at time of crash:\n");
        printf("Name\t\t\tBase Address\tSize\n");
        printf("------------------------------------------------\n");
        
        for (uint32_t i = 0; i < header.module_count; i++) {
            printf("%-20s\t0x%08x\t0x%08x\n", 
                   header.modules[i].name,
                   header.modules[i].base_addr, 
                   header.modules[i].size);
        }
    }
    
    // Close the file
    vfs_close(dump_file);
    
    printf("\n===== END OF ANALYSIS =====\n\n");
    return true;
}

/**
 * Check if a crash dump exists from previous run
 */
bool crash_dump_exists(void) {
    return crash_dump_list() > 0;
}

/**
 * List available crash dumps
 */
int crash_dump_list(void) {
    // In a real implementation, we would:
    // 1. Open the crash dump directory
    // 2. List all files matching the DUMP_FILE_PREFIX pattern
    // 3. Display their timestamps and details
    
    // For now, just return a placeholder value
    return 0; // No crash dumps found (implementation needed)
}
