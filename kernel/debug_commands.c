/**
 * Crash dump analyzer command handler
 */
void cmd_crashdump(int argc, char *argv[]) {
    log_debug("SHELL", "Executing crashdump command");
    
    // Imported from crash_dump.h
    extern bool crash_dump_analyze(const char* dump_id);
    extern int crash_dump_list(void);
    extern bool crash_dump_exists(void);
    
    if (argc < 2) {
        // No arguments provided - see if any dumps exist
        if (!crash_dump_exists()) {
            shell_println("No crash dumps found.");
            return;
        }
        
        shell_println("Available crash dumps:");
        int count = crash_dump_list();
        
        if (count == 0) {
            shell_println("No crash dumps found.");
        } else {
            shell_println("");
            shell_println("Use 'crashdump analyze <dump_id>' to analyze a specific dump");
            shell_println("or 'crashdump analyze' to analyze the most recent dump.");
        }
        return;
    }
    
    // Parse subcommand
    if (strcmp(argv[1], "list") == 0) {
        shell_println("Available crash dumps:");
        crash_dump_list();
    }
    else if (strcmp(argv[1], "analyze") == 0) {
        const char* dump_id = NULL;
        
        // If a specific dump ID is provided
        if (argc >= 3) {
            dump_id = argv[2];
        }
        
        if (!crash_dump_analyze(dump_id)) {
            shell_println("Error analyzing crash dump.");
        }
    }
    else {
        shell_println("Unknown subcommand. Available commands:");
        shell_println("  crashdump list    - List available crash dumps");
        shell_println("  crashdump analyze [dump_id] - Analyze crash dump");
    }
}

/**
 * Hardware debug breakpoint command handler
 */
void cmd_debug_bp(int argc, char *argv[]) {
    log_debug("SHELL", "Executing debug breakpoint command");
    
    // Import debug register functions
    extern int debug_set_breakpoint(int index, void* address, 
                                   int type, int size, bool global);
    extern int debug_clear_breakpoint(int index);
    extern void debug_registers_get_state(void* regs);
    
    if (argc < 2) {
        shell_println("Usage: debug_bp <subcommand> [options]");
        shell_println("Subcommands:");
        shell_println("  set <index> <address> <type> <size> - Set a hardware breakpoint");
        shell_println("    index: 0-3");
        shell_println("    address: Memory address (hex)");
        shell_println("    type: 0=execute, 1=write, 3=access");
        shell_println("    size: 0=1 byte, 1=2 bytes, 3=4 bytes");
        shell_println("  clear <index> - Clear a hardware breakpoint");
        shell_println("  status - Show current debug register status");
        return;
    }
    
    // Handle subcommands
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 6) {
            shell_println("Not enough arguments for 'set' command.");
            return;
        }
        
        // Parse arguments
        int index = atoi(argv[2]);
        uint32_t address;
        sscanf(argv[3], "%x", &address);
        int type = atoi(argv[4]);
        int size = atoi(argv[5]);
        
        // Validate parameters
        if (index < 0 || index > 3) {
            shell_println("Invalid breakpoint index (must be 0-3).");
            return;
        }
        
        if (type < 0 || type > 3) {
            shell_println("Invalid breakpoint type.");
            return;
        }
        
        if (size != 0 && size != 1 && size != 3) {
            shell_println("Invalid breakpoint size.");
            return;
        }
        
        // Set the hardware breakpoint
        if (debug_set_breakpoint(index, (void*)address, type, size, true) == 0) {
            shell_println("Hardware breakpoint set successfully.");
        } else {
            shell_println("Failed to set hardware breakpoint.");
        }
    }
    else if (strcmp(argv[1], "clear") == 0) {
        if (argc < 3) {
            shell_println("Please specify breakpoint index to clear.");
            return;
        }
        
        int index = atoi(argv[2]);
        if (debug_clear_breakpoint(index) == 0) {
            shell_println("Hardware breakpoint cleared successfully.");
        } else {
            shell_println("Failed to clear hardware breakpoint.");
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        // Show current debug register status
        extern void debug_registers_get_state(void* regs);
        
        // Structure to hold debug register values
        typedef struct {
            uint32_t dr0, dr1, dr2, dr3, dr6, dr7;
        } debug_regs_t;
        
        debug_regs_t regs;
        debug_registers_get_state(&regs);
        
        shell_println("Debug Register Status:");
        char buffer[64];
        
        sprintf(buffer, "DR0: 0x%08x", regs.dr0);
        shell_println(buffer);
        
        sprintf(buffer, "DR1: 0x%08x", regs.dr1);
        shell_println(buffer);
        
        sprintf(buffer, "DR2: 0x%08x", regs.dr2);
        shell_println(buffer);
        
        sprintf(buffer, "DR3: 0x%08x", regs.dr3);
        shell_println(buffer);
        
        sprintf(buffer, "DR6: 0x%08x", regs.dr6);
        shell_println(buffer);
        
        sprintf(buffer, "DR7: 0x%08x", regs.dr7);
        shell_println(buffer);
        
        // Display active breakpoints
        shell_println("\nActive Breakpoints:");
        
        for (int i = 0; i < 4; i++) {
            if ((regs.dr7 & (1 << (i*2))) || (regs.dr7 & (1 << (i*2+1)))) {
                uint32_t addr = 0;
                switch (i) {
                    case 0: addr = regs.dr0; break;
                    case 1: addr = regs.dr1; break;
                    case 2: addr = regs.dr2; break;
                    case 3: addr = regs.dr3; break;
                }
                
                uint32_t rw_bits = (regs.dr7 >> (16 + i*4)) & 3;
                uint32_t len_bits = (regs.dr7 >> (18 + i*4)) & 3;
                
                const char* type_str = "unknown";
                switch (rw_bits) {
                    case 0: type_str = "execution"; break;
                    case 1: type_str = "write"; break; 
                    case 2: type_str = "I/O"; break;
                    case 3: type_str = "access"; break;
                }
                
                uint32_t size = 1;
                switch (len_bits) {
                    case 0: size = 1; break;
                    case 1: size = 2; break;
                    case 2: size = 8; break;
                    case 3: size = 4; break;
                }
                
                sprintf(buffer, "BP%d: addr=0x%08x, type=%s, size=%d bytes", 
                       i, addr, type_str, size);
                shell_println(buffer);
            }
        }
    }
    else {
        shell_println("Unknown subcommand.");
    }
}
