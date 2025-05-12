#include "shell.h"
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "../memory/paging.h"
#include "../filesystem/fat12.h"
#include "../memory/heap.h"
#include "task.h"
#include "logging/log.h"

// ANSI color codes
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_BLUE "\033[34m"

// Buffer for the current command
static char command_buffer[MAX_COMMAND_LENGTH];
static int buffer_position = 0;

// Command history buffer
static char command_history[COMMAND_HISTORY_SIZE][MAX_COMMAND_LENGTH];
static int history_count = 0;
static int history_position = -1;

// Utilities for string manipulation
static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static void strcpy(char *dest, const char *src) {
    while ((*dest++ = *src++));
}

// Additional string utility functions
static int strlen(const char *str) {
    int length = 0;
    while (*str++) {
        length++;
    }
    return length;
}

static void int_to_string(int value, char *str) {
    // Handle negative numbers
    int negative = 0;
    if (value < 0) {
        negative = 1;
        value = -value;
    }
    
    // Handle zero case
    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    
    // Find string length by repeatedly dividing by 10
    int length = 0;
    int temp = value;
    while (temp > 0) {
        temp /= 10;
        length++;
    }
    
    // Add negative sign if needed
    if (negative) {
        str[0] = '-';
        length++;
    }
    
    // Add null terminator
    str[length] = '\0';
    
    // Fill in the digits from right to left
    int i = length - 1;
    while (value > 0) {
        str[i--] = '0' + (value % 10);
        value /= 10;
    }
    
    // If it was negative, we've already put the sign in place
}

/**
 * Add a command to the history buffer
 */
static void add_to_history(const char *command) {
    // Don't add empty commands or duplicates of the last command
    if (strlen(command) == 0 || (history_count > 0 && 
        strcmp(command, command_history[history_count - 1]) == 0)) {
        return;
    }
    
    // Add to history, replacing oldest command if history is full
    if (history_count < COMMAND_HISTORY_SIZE) {
        strcpy(command_history[history_count++], command);
    } else {
        // Shift all commands one position down
        for (int i = 0; i < COMMAND_HISTORY_SIZE - 1; i++) {
            strcpy(command_history[i], command_history[i + 1]);
        }
        // Add new command at the end
        strcpy(command_history[COMMAND_HISTORY_SIZE - 1], command);
    }
    
    // Reset history position to point to the latest command
    history_position = history_count;
}

/**
 * Display the command from history at the given position
 */
static void display_history_command(int position) {
    if (position < 0 || position >= history_count) {
        return;
    }
    
    // Clear the current line
    int i;
    for (i = 0; i < buffer_position; i++) {
        shell_print("\b \b");
    }
    
    // Copy and display the command from history
    strcpy(command_buffer, command_history[position]);
    buffer_position = strlen(command_buffer);
    shell_print(command_buffer);
}

/**
 * Auto-complete the current command
 */
static void auto_complete() {
    // Save current command if any
    char partial_cmd[MAX_COMMAND_LENGTH];
    strcpy(partial_cmd, command_buffer);
    partial_cmd[buffer_position] = '\0';
    
    // List of available commands for autocompletion
    const char *commands[] = {
        "help", "clear", "echo", "ls", "cat", "meminfo", "memstat",
        "memtest", "taskinfo", "reboot", "vgademo", "log", "vfs", "wdm"
    };
    int command_count = sizeof(commands) / sizeof(commands[0]);
    
    // Find matching commands
    const char *matches[command_count];
    int match_count = 0;
    int match_length = 0;
    
    for (int i = 0; i < command_count; i++) {
        if (strncmp(partial_cmd, commands[i], strlen(partial_cmd)) == 0) {
            matches[match_count++] = commands[i];
            
            // Calculate common prefix length for all matches
            if (match_count == 1) {
                match_length = strlen(commands[i]);
            } else {
                int j;
                for (j = 0; j < match_length && j < strlen(commands[i]); j++) {
                    if (matches[0][j] != commands[i][j]) {
                        break;
                    }
                }
                match_length = j;
            }
        }
    }
    
    // No matches found
    if (match_count == 0) {
        return;
    }
    
    // Single match - complete the command
    if (match_count == 1) {
        // Clear current content
        for (int i = 0; i < buffer_position; i++) {
            shell_print("\b \b");
        }
        
        // Copy full command
        strcpy(command_buffer, matches[0]);
        buffer_position = strlen(command_buffer);
        
        // Display command
        shell_print(command_buffer);
        return;
    }
    
    // Multiple matches - complete common prefix and show options
    if (match_length > buffer_position) {
        // Clear current content
        for (int i = 0; i < buffer_position; i++) {
            shell_print("\b \b");
        }
        
        // Copy common prefix
        strncpy(command_buffer, matches[0], match_length);
        command_buffer[match_length] = '\0';
        buffer_position = match_length;
        
        // Display common prefix
        shell_print(command_buffer);
        
        // List all matches
        shell_println("");
        for (int i = 0; i < match_count; i++) {
            shell_println(matches[i]);
        }
        shell_display_prompt();
        shell_print(command_buffer);
    }
}

// Shell I/O functions
void shell_print(const char *str) {
    // Use VGA driver instead of direct display
    vga_write_string(str);
}

void shell_println(const char *str) {
    shell_print(str);
    shell_print("\n");
}

void shell_display_prompt() {
    // Save the current color
    uint8_t old_color = vga_current_color;
    
    // Display username in green
    vga_set_color(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    shell_print("uintOS");
    
    // Display divider in white
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    shell_print(":");
    
    // Display path in blue
    vga_set_color(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    shell_print("~");
    
    // Display prompt in white
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    shell_print("$ ");
    
    // Restore the original color
    vga_set_color(old_color);
}

/**
 * Special keys handling
 */
#define KEY_UP_ARROW    0x48
#define KEY_DOWN_ARROW  0x50
#define KEY_RIGHT_ARROW 0x4D
#define KEY_LEFT_ARROW 0x4B
#define KEY_HOME        0x47
#define KEY_END         0x4F
#define KEY_TAB         0x09
#define KEY_DEL         0x53
#define KEY_ESC         0x1B

// Process a key input in the shell
static void process_key(char key) {
    if (key == '\n') {
        // Enter key - execute command
        shell_println("");
        command_buffer[buffer_position] = 0; // Null terminate
        if (buffer_position > 0) {
            add_to_history(command_buffer);
            shell_execute_command(command_buffer);
        }
        buffer_position = 0;
        history_position = history_count;
        shell_display_prompt();
    } 
    else if (key == '\b') {
        // Backspace - delete a character
        if (buffer_position > 0) {
            buffer_position--;
            shell_print("\b \b"); // Erase character on screen
        }
    }
    else if (key == KEY_TAB) {
        // Tab key - auto-complete
        auto_complete();
    }
    else if (key == KEY_UP_ARROW) {
        // Up arrow - navigate history upward
        if (history_position > 0) {
            history_position--;
            display_history_command(history_position);
        }
    }
    else if (key == KEY_DOWN_ARROW) {
        // Down arrow - navigate history downward
        if (history_position < history_count - 1) {
            history_position++;
            display_history_command(history_position);
        } else if (history_position == history_count - 1) {
            // Clear the current line when reaching the end of history
            history_position = history_count;
            int i;
            for (i = 0; i < buffer_position; i++) {
                shell_print("\b \b");
            }
            buffer_position = 0;
            command_buffer[0] = '\0';
        }
    }
    else if (key == KEY_DEL) {
        // Delete key - remove character at cursor
        if (buffer_position < strlen(command_buffer)) {
            // Shift characters to the left
            for (int i = buffer_position; i < strlen(command_buffer); i++) {
                command_buffer[i] = command_buffer[i + 1];
            }
            
            // Redraw the line
            int saved_pos = buffer_position;
            shell_print("\r");
            shell_display_prompt();
            shell_print(command_buffer);
            shell_print(" \b"); // Erase extra character at the end
            
            // Restore cursor position
            for (int i = strlen(command_buffer); i > saved_pos; i--) {
                shell_print("\b");
            }
        }
    }
    else if (key == KEY_HOME) {
        // Home key - move cursor to start of line
        int steps = buffer_position;
        for (int i = 0; i < steps; i++) {
            shell_print("\b");
        }
        buffer_position = 0;
    }
    else if (key == KEY_END) {
        // End key - move cursor to end of line
        int steps = strlen(command_buffer) - buffer_position;
        for (int i = 0; i < steps; i++) {
            shell_print("\033[C"); // ANSI forward
        }
        buffer_position = strlen(command_buffer);
    }
    else if (buffer_position < MAX_COMMAND_LENGTH - 1) {
        // Regular character - add to buffer
        // Make room by shifting characters to the right
        if (buffer_position < strlen(command_buffer)) {
            for (int i = strlen(command_buffer); i >= buffer_position; i++) {
                command_buffer[i + 1] = command_buffer[i];
            }
        }
        
        command_buffer[buffer_position++] = key;
        
        // Redraw the line from the current position
        int i;
        for (i = buffer_position - 1; command_buffer[i]; i++) {
            display_character(command_buffer[i], 15);
        }
        
        // Move cursor back to the insertion point
        for (; i > buffer_position; i--) {
            shell_print("\b");
        }
    } 
    else {
        // Buffer is full, alert the user
        display_character('\a', 15); // Bell character as alert
    }
}

// Main shell loop
void shell_run() {
    log_info("SHELL", "Starting uintOS Shell");
    shell_println("Welcome to uintOS Shell!");
    shell_display_prompt();
    
    while (1) {
        if (is_key_available()) {
            char key = keyboard_read_key();
            process_key(key);
        }
        // This is where we would yield to other tasks in a preemptive multitasking OS
    }
}

// Parse and execute a command
void shell_execute_command(const char *command) {
    log_debug("SHELL", "Executing command: %s", command);
    
    char cmd_copy[MAX_COMMAND_LENGTH];
    strcpy(cmd_copy, command);
    
    // Parse arguments
    char *argv[MAX_ARGS];
    int argc = 0;
    
    // Tokenize the command string
    char *token = cmd_copy;
    char *next_token = cmd_copy;
    
    while (argc < MAX_ARGS && *next_token) {
        // Skip leading whitespace
        while (*token && *token == ' ') token++;
        
        if (!*token) break;
        
        argv[argc++] = token;
        
        // Find end of current token
        next_token = token;
        while (*next_token && *next_token != ' ') next_token++;
        
        if (!*next_token) break;
        
        *next_token = 0; // Null-terminate this token
        token = next_token + 1;
    }
    
    // Execute the command
    if (argc > 0) {
        if (strcmp(argv[0], "help") == 0) {
            cmd_help(argc, argv);
        } else if (strcmp(argv[0], "clear") == 0) {
            cmd_clear(argc, argv);
        } else if (strcmp(argv[0], "echo") == 0) {
            cmd_echo(argc, argv);
        } else if (strcmp(argv[0], "meminfo") == 0) {
            cmd_meminfo(argc, argv);
        } else if (strcmp(argv[0], "taskinfo") == 0) {
            cmd_taskinfo(argc, argv);
        } else if (strcmp(argv[0], "taskman") == 0) {
            cmd_taskman(argc, argv);
        } else if (strcmp(argv[0], "reboot") == 0) {
            cmd_reboot(argc, argv);
        } else if (strcmp(argv[0], "memstat") == 0) {
            cmd_memstat(argc, argv);
        } else if (strcmp(argv[0], "memtest") == 0) {
            cmd_memtest(argc, argv);
        } else if (strcmp(argv[0], "ls") == 0) {
            cmd_ls(argc, argv);
        } else if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(argc, argv);
        } else if (strcmp(argv[0], "vgademo") == 0) {
            cmd_vgademo(argc, argv);
        } else if (strcmp(argv[0], "log") == 0) {
            cmd_log(argc, argv);
        } else if (strcmp(argv[0], "vfs") == 0) {
            cmd_vfs(argc, argv);
        } else if (strcmp(argv[0], "wdm") == 0) {
            cmd_wdm(argc, argv);  // Windows driver command
        } else if (strcmp(argv[0], "usb") == 0) {
            cmd_usb(argc, argv);  // USB command
        } else if (strcmp(argv[0], "vm") == 0) {
            cmd_vm(argc, argv);  // VM management command
        } else if (strcmp(argv[0], "gui") == 0) {
            cmd_gui(argc, argv);  // New GUI command        } else if (strcmp(argv[0], "panic") == 0) {
            cmd_panic(argc, argv);  // Kernel panic test command
        } else if (strcmp(argv[0], "preempt") == 0) {
            cmd_preempt(argc, argv);  // Preemptive multitasking control        } else if (strcmp(argv[0], "taskdemo") == 0) {
            cmd_taskdemo(argc, argv);  // Run multitasking demo
        } else {
            log_warning("SHELL", "Unknown command: %s", argv[0]);
            shell_println("Unknown command. Type 'help' for a list of commands.");
        }
    }
}

// Initialize the shell
void shell_init() {
    buffer_position = 0;
    history_count = 0;
    history_position = -1;
    log_info("SHELL", "Shell initialized");
}

// Command implementations
void cmd_help(int argc, char *argv[]) {
    log_debug("SHELL", "Executing help command");
    shell_println("uintOS Shell Commands:");
    shell_println("  help     - Display this help message");
    shell_println("  clear    - Clear the screen");
    shell_println("  echo     - Display a message");
    shell_println("  ls       - List directory contents");
    shell_println("  cat      - Display file contents");
    shell_println("  meminfo  - Display basic memory information");
    shell_println("  memstat  - Display detailed memory statistics");
    shell_println("  memtest  - Run memory allocation tests");
    shell_println("  taskinfo - Display task information");
    shell_println("  taskman  - Launch interactive task manager");
    shell_println("  reboot   - Reboot the system");
    shell_println("  vgademo  - Run VGA demonstration");
    shell_println("  log      - View and manage system logs");
    shell_println("  vfs      - Virtual filesystem operations");    shell_println("  wdm      - Windows driver management");
    shell_println("  usb      - USB subsystem management");
    shell_println("  vm       - Manage virtual machines");
    shell_println("  gui      - Graphical user interface commands");    shell_println("  preempt  - Control preemptive multitasking");
    shell_println("  taskdemo - Run multitasking demonstration");
    shell_println("  panic    - Test kernel panic handling (WARNING: crashes system)");
}

void cmd_clear(int argc, char *argv[]) {
    // Use the VGA driver to clear the screen
    log_debug("SHELL", "Clearing screen");
    vga_clear_screen();
}

void cmd_echo(int argc, char *argv[]) {
    log_debug("SHELL", "Executing echo command");
    for (int i = 1; i < argc; i++) {
        shell_print(argv[i]);
        if (i < argc - 1) {
            shell_print(" ");
        }
    }
    shell_println("");
}

void cmd_meminfo(int argc, char *argv[]) {
    // Display memory information from the paging system
    log_debug("SHELL", "Displaying memory information");
    shell_println("Memory Information:");
    shell_println("  Page Size: 4096 bytes");
    // You would get actual memory usage statistics here
    shell_println("  Total Memory: 16 MB");
    shell_println("  Used Memory: 4 MB");
    shell_println("  Free Memory: 12 MB");
}

void cmd_taskinfo(int argc, char *argv[]) {
    // Get the total number of tasks
    int count = get_task_count();
    int current_id = get_current_task_id();
    
    // Display header
    shell_println("=== Task Information ===");
    char buffer[32];
    
    // Display task count
    int_to_string(count, buffer);
    shell_print("Total Tasks: ");
    shell_println(buffer);
    
    // Display current task
    if (current_id >= 0) {
        shell_print("Current Task ID: ");
        int_to_string(current_id, buffer);
        shell_println(buffer);
    } else {
        shell_println("No active task");
    }
    
    // Display preemption status
    shell_print("Preemptive Scheduling: ");
    if (is_preemption_enabled()) {
        shell_println("ENABLED");
    } else {
        shell_println("DISABLED");
    }
    
    // Display timer information
    shell_print("System Ticks: ");
    uint64_t ticks = get_preemption_ticks();
    // Simple conversion of uint64_t to string (only showing lower 32 bits for simplicity)
    int_to_string((int)ticks, buffer);
    shell_println(buffer);
    
    // Display table header
    shell_println("\nID  State    Stack Size  Name");
    shell_println("--  -------  ----------  ----");
    
    // Display information for each task
    for (int i = 0; i < count; i++) {
        task_info_t info;
        if (get_task_info(i, &info)) {
            // ID
            int_to_string(info.id, buffer);
            shell_print(buffer);
            // Padding
            for (int j = strlen(buffer); j < 4; j++) {
                shell_print(" ");
            }
            
            // State
            const char* state;
            switch (info.state) {
                case TASK_STATE_UNUSED:  state = "UNUSED"; break;
                case TASK_STATE_READY:   state = "READY"; break;
                case TASK_STATE_RUNNING: state = "RUNNING"; break;
                default:                 state = "UNKNOWN"; break;
            }
            shell_print(state);
            // Padding
            for (int j = strlen(state); j < 9; j++) {
                shell_print(" ");
            }
            
            // Stack Size
            int_to_string(info.stack_size, buffer);
            shell_print(buffer);
            // Padding
            for (int j = strlen(buffer); j < 12; j++) {
                shell_print(" ");
            }
            
            // Name with current task indicator
            if (info.is_current) {
                shell_print("*");
            } else {
                shell_print(" ");
            }
            shell_println(info.name);
        }
    }
}

void cmd_reboot(int argc, char *argv[]) {
    shell_println("Rebooting system...");
    
    // Wait a moment to allow the message to be displayed
    for (volatile int i = 0; i < 1000000; i++) { /* delay */ }
    
    // Try to reboot using the keyboard controller
    // Send the reboot command to the keyboard controller
    outb(0x64, 0xFE);
    
    // If that doesn't work, try a triple fault to force a reset
    asm volatile("cli");  // Disable interrupts
    asm volatile("lidt [0]");  // Load an invalid IDT
    asm volatile("int $0x3");  // Trigger an interrupt that will cause a triple fault
    
    // We should never get here, but just in case
    shell_println("Reboot failed. System halted.");
    while(1) { asm volatile("hlt"); }
}

// Command implementations for file operations
void cmd_ls(int argc, char *argv[]) {
    // Allocate a buffer for directory entries
    #define MAX_FILES 20
    fat12_file_entry_t entries[MAX_FILES];
    
    // Path defaults to root directory if not specified
    const char* path = (argc > 1) ? argv[1] : "/";
    
    // Get directory listing
    int count = fat12_list_directory(path, entries, MAX_FILES);
    
    if (count < 0) {
        // Error occurred, display appropriate message
        shell_print("Error: ");
        switch (count) {
            case FAT12_ERR_NOT_FOUND:   shell_println("Directory not found"); break;
            case FAT12_ERR_IO_ERROR:    shell_println("I/O error"); break;
            case FAT12_ERR_INVALID_ARG: shell_println("Invalid path"); break;
            default:                     shell_println("Unknown error"); break;
        }
        return;
    }
    
    if (count == 0) {
        shell_println("Directory is empty");
        return;
    }
    
    // Display column headers
    shell_println("Name                Size       Attributes");
    shell_println("----                ----       ----------");
    
    // Display each entry
    for (int i = 0; i < count; i++) {
        // Print filename with padding
        shell_print(entries[i].name);
        
        // Calculate padding for alignment
        int padding = 20 - strlen(entries[i].name);
        for (int j = 0; j < padding; j++) {
            shell_print(" ");
        }
        
        // Print file size with proper formatting
        char size_str[12];
        int_to_string(entries[i].size, size_str);
        shell_print(size_str);
        
        // More padding
        padding = 10 - strlen(size_str);
        for (int j = 0; j < padding; j++) {
            shell_print(" ");
        }
        
        // Print file attributes
        if (entries[i].attributes & FAT12_ATTR_DIRECTORY) shell_print("D");
        else shell_print("-");
        
        if (entries[i].attributes & FAT12_ATTR_READ_ONLY) shell_print("R");
        else shell_print("-");
        
        if (entries[i].attributes & FAT12_ATTR_HIDDEN) shell_print("H");
        else shell_print("-");
        
        if (entries[i].attributes & FAT12_ATTR_SYSTEM) shell_print("S");
        else shell_print("-");
        
        if (entries[i].attributes & FAT12_ATTR_ARCHIVE) shell_print("A");
        else shell_print("-");
        
        shell_println("");
    }
    
    // Print total file count
    char count_str[12];
    int_to_string(count, count_str);
    shell_print("Total: ");
    shell_print(count_str);
    shell_println(" files");
}

void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        shell_println("Usage: cat <filename>");
        return;
    }
    
    // Get file size first
    int size = fat12_get_file_size(argv[1]);
    
    if (size < 0) {
        // Error occurred
        shell_print("Error: ");
        switch (size) {
            case FAT12_ERR_NOT_FOUND: shell_println("File not found"); break;
            case FAT12_ERR_IO_ERROR:  shell_println("I/O error"); break;
            default:                   shell_println("Unknown error"); break;
        }
        return;
    }
    
    // Allocate a buffer for the file contents (we'll use a fixed buffer size for simplicity)
    #define MAX_FILE_SIZE 4096
    if (size > MAX_FILE_SIZE) {
        shell_println("File too large to display");
        return;
    }
    
    char buffer[MAX_FILE_SIZE];
    
    // Read the file
    int bytes_read = fat12_read_file(argv[1], buffer, size);
    
    if (bytes_read < 0) {
        shell_println("Error reading file");
        return;
    }
    
    // Ensure buffer is null-terminated for proper display
    buffer[bytes_read] = '\0';
    
    // Display the file contents
    shell_println(buffer);
}

// Enhanced memory statistics command
void cmd_memstat(int argc, char *argv[]) {
    heap_stats_t stats;
    heap_get_stats(&stats);
    
    shell_println("=== Memory Heap Statistics ===");
    
    char buffer[32];
    
    // Total memory in heap
    int_to_string(stats.total_memory, buffer);
    shell_print("Total Heap Memory  : ");
    shell_print(buffer);
    shell_println(" bytes");
    
    // Used memory
    int_to_string(stats.used_memory, buffer);
    shell_print("Used Memory        : ");
    shell_print(buffer);
    shell_print(" bytes (");
    int percentage = (stats.used_memory * 100) / stats.total_memory;
    int_to_string(percentage, buffer);
    shell_print(buffer);
    shell_println("%)");
    
    // Free memory
    int_to_string(stats.free_memory, buffer);
    shell_print("Free Memory        : ");
    shell_print(buffer);
    shell_print(" bytes (");
    percentage = (stats.free_memory * 100) / stats.total_memory;
    int_to_string(percentage, buffer);
    shell_print(buffer);
    shell_println("%)");
    
    // Current allocations
    int_to_string(stats.allocation_count, buffer);
    shell_print("Active Allocations : ");
    shell_print(buffer);
    shell_println("");
    
    // Average allocation size
    if (stats.allocation_count > 0) {
        int avg_size = stats.used_memory / stats.allocation_count;
        int_to_string(avg_size, buffer);
        shell_print("Avg Allocation Size: ");
        shell_print(buffer);
        shell_println(" bytes");
    }
}

// Memory testing command
void cmd_memtest(int argc, char *argv[]) {
    shell_println("=== Memory Allocation Test ===");
    
    // Store pointers to allocated memory
    #define MAX_TEST_ALLOCS 10
    void* allocations[MAX_TEST_ALLOCS] = {NULL};
    char buffer[32];
    
    // Test 1: Allocate different sizes
    shell_println("Test 1: Allocating blocks of different sizes...");
    for (int i = 0; i < MAX_TEST_ALLOCS; i++) {
        int size = (i + 1) * 128;  // 128, 256, 384, etc.
        allocations[i] = malloc(size);
        
        if (allocations[i]) {
            shell_print("  Block ");
            int_to_string(i, buffer);
            shell_print(buffer);
            shell_print(": ");
            int_to_string(size, buffer);
            shell_print(buffer);
            shell_println(" bytes allocated successfully");
            
            // Write pattern to the allocated memory
            for (int j = 0; j < size; j++) {
                ((char*)allocations[i])[j] = (char)(j % 256);
            }
        } else {
            shell_print("  Block ");
            int_to_string(i, buffer);
            shell_print(buffer);
            shell_println(": Allocation failed");
        }
    }
    
    // Test 2: Verify memory integrity
    shell_println("\nTest 2: Verifying memory integrity...");
    int passed = 1;
    for (int i = 0; i < MAX_TEST_ALLOCS; i++) {
        if (allocations[i]) {
            int size = (i + 1) * 128;
            int valid = 1;
            
            for (int j = 0; j < size; j++) {
                if (((char*)allocations[i])[j] != (char)(j % 256)) {
                    valid = 0;
                    break;
                }
            }
            
            shell_print("  Block ");
            int_to_string(i, buffer);
            shell_print(buffer);
            if (valid) {
                shell_println(": Passed integrity check");
            } else {
                shell_println(": Failed integrity check!");
                passed = 0;
            }
        }
    }
    
    // Test 3: Free half the blocks and reallocate
    shell_println("\nTest 3: Freeing and reallocating blocks...");
    for (int i = 0; i < MAX_TEST_ALLOCS; i += 2) {
        if (allocations[i]) {
            shell_print("  Freeing block ");
            int_to_string(i, buffer);
            shell_println(buffer);
            free(allocations[i]);
            allocations[i] = NULL;
        }
    }
    
    // Reallocate freed blocks with different sizes
    for (int i = 0; i < MAX_TEST_ALLOCS; i += 2) {
        int size = (i + 1) * 64;  // Different size than before
        allocations[i] = malloc(size);
        
        if (allocations[i]) {
            shell_print("  Reallocated block ");
            int_to_string(i, buffer);
            shell_print(buffer);
            shell_print(": ");
            int_to_string(size, buffer);
            shell_print(buffer);
            shell_println(" bytes");
            
            // Write a different pattern
            for (int j = 0; j < size; j++) {
                ((char*)allocations[i])[j] = (char)(255 - (j % 256));
            }
        } else {
            shell_print("  Block ");
            int_to_string(i, buffer);
            shell_print(buffer);
            shell_println(": Reallocation failed");
        }
    }
    
    // Final cleanup
    shell_println("\nCleaning up all allocations...");
    for (int i = 0; i < MAX_TEST_ALLOCS; i++) {
        if (allocations[i]) {
            free(allocations[i]);
            allocations[i] = NULL;
        }
    }
    
    // Show final statistics
    shell_println("\nFinal heap statistics:");
    cmd_memstat(0, NULL);
    
    if (passed) {
        shell_println("\nMemory tests completed successfully!");
    } else {
        shell_println("\nMemory tests completed with errors!");
    }
}

void cmd_vgademo(int argc, char *argv[]) {
    // Run the VGA demonstration
    extern void vga_demo();
    vga_demo();
}

// VFS command implementation
void cmd_vfs(int argc, char *argv[]) {
    /* Include necessary headers */
    #include "../filesystem/vfs/vfs.h"
    
    if (argc == 1) {
        /* Display usage information if no arguments */
        shell_println("Usage: vfs <command> [options]");
        shell_println("Commands:");
        shell_println("  init      - Initialize the VFS system");
        shell_println("  mount     - Mount a filesystem");
        shell_println("  unmount   - Unmount a filesystem");
        shell_println("  list      - List mounted filesystems");
        shell_println("  info      - Display filesystem information");
        shell_println("");
        shell_println("Examples:");
        shell_println("  vfs init                      - Initialize VFS");
        shell_println("  vfs mount fat12 fd0 /mnt      - Mount FAT12 filesystem on /mnt");
        shell_println("  vfs unmount /mnt              - Unmount filesystem at /mnt");
        shell_println("  vfs list                      - List all mounted filesystems");
        shell_println("  vfs info /mnt                 - Display info about filesystem at /mnt");
        return;
    }
    
    /* Handle various VFS subcommands */
    if (strcmp(argv[1], "init") == 0) {
        /* Initialize the VFS */
        int result = vfs_init();
        if (result == VFS_SUCCESS) {
            shell_println("VFS initialized successfully");
            
            /* Register available filesystem types with VFS */
            extern void register_fat12_with_vfs();
            register_fat12_with_vfs();
            shell_println("Registered filesystem types with VFS");
        } else {
            shell_print("Error initializing VFS: ");
            char buffer[16];
            int_to_string(result, buffer);
            shell_println(buffer);
        }
    }
    else if (strcmp(argv[1], "mount") == 0) {
        /* Mount a filesystem */
        if (argc < 5) {
            shell_println("Usage: vfs mount <type> <device> <mountpoint>");
            return;
        }
        
        const char* fs_type = argv[2];
        const char* device = argv[3];
        const char* mount_point = argv[4];
        
        int result = vfs_mount(fs_type, device, mount_point, 0);
        if (result == VFS_SUCCESS) {
            shell_print("Mounted ");
            shell_print(fs_type);
            shell_print(" from ");
            shell_print(device);
            shell_print(" on ");
            shell_println(mount_point);
        } else {
            shell_print("Error mounting filesystem: ");
            char buffer[16];
            int_to_string(result, buffer);
            shell_println(buffer);
        }
    }
    else if (strcmp(argv[1], "unmount") == 0) {
        /* Unmount a filesystem */
        if (argc < 3) {
            shell_println("Usage: vfs unmount <mountpoint>");
            return;
        }
        
        const char* mount_point = argv[2];
        
        int result = vfs_unmount(mount_point);
        if (result == VFS_SUCCESS) {
            shell_print("Unmounted ");
            shell_println(mount_point);
        } else {
            shell_print("Error unmounting filesystem: ");
            char buffer[16];
            int_to_string(result, buffer);
            shell_println(buffer);
        }
    }
    else if (strcmp(argv[1], "list") == 0) {
        /* List mounted filesystems - This needs more implementation */
        shell_println("Mounted filesystems:");
        shell_println("  Type     Device       Mount Point");
        shell_println("  ----     ------       -----------");
        /* 
         * We need to add a function to VFS to enumerate mount points
         * For now, just show a placeholder
         */
        shell_println("  <Feature not yet implemented>");
    }
    else if (strcmp(argv[1], "info") == 0) {
        /* Display filesystem information */
        if (argc < 3) {
            shell_println("Usage: vfs info <path>");
            return;
        }
        
        const char* path = argv[2];
        uint64_t total_size, free_size;
        
        int result = vfs_statfs(path, &total_size, &free_size);
        if (result == VFS_SUCCESS) {
            shell_print("Filesystem information for ");
            shell_println(path);
            
            char buffer[32];
            
            shell_print("  Total Size: ");
            int_to_string(total_size, buffer);
            shell_println(buffer);
            
            shell_print("  Free Space: ");
            int_to_string(free_size, buffer);
            shell_println(buffer);
            
            uint64_t used_size = total_size - free_size;
            shell_print("  Used Space: ");
            int_to_string(used_size, buffer);
            shell_println(buffer);
            
            uint64_t percent_used = (used_size * 100) / total_size;
            shell_print("  Usage: ");
            int_to_string(percent_used, buffer);
            shell_print(buffer);
            shell_println("%");
        } else if (result == VFS_ERR_UNSUPPORTED) {
            shell_println("This filesystem does not support the statfs operation");
        } else {
            shell_print("Error getting filesystem information: ");
            char buffer[16];
            int_to_string(result, buffer);
            shell_println(buffer);
        }
    }
    else {
        shell_println("Unknown vfs subcommand. Try 'vfs' for help.");
    }
}

// Log command implementation
void cmd_log(int argc, char *argv[]) {
    // Include logging header
    #include "logging/log.h"
    
    if (argc == 1) {
        // Display usage information if no arguments
        shell_println("Usage: log <command> [options]");
        shell_println("Commands:");
        shell_println("  show      - Display current log buffer");
        shell_println("  clear     - Clear the log buffer");
        shell_println("  level     - Set or display log level");
        shell_println("  dest      - Set or display log destinations");
        shell_println("  format    - Set or display log format options");
        shell_println("  test      - Generate test log messages");
        shell_println("");
        shell_println("Examples:");
        shell_println("  log show                   - Show current logs");
        shell_println("  log level                  - Display current log level");
        shell_println("  log level debug            - Set log level to debug");
        shell_println("  log dest                   - Display current log destinations");
        shell_println("  log dest screen            - Set logs to appear only on screen");
        shell_println("  log dest memory+screen     - Enable multiple destinations");
        return;
    }
    
    // Handle various log subcommands
    if (strcmp(argv[1], "show") == 0) {
        // Show current log buffer
        log_dump_buffer();
    }
    else if (strcmp(argv[1], "clear") == 0) {
        // Clear the log buffer
        log_clear_buffer();
        shell_println("Log buffer cleared");
    }
    else if (strcmp(argv[1], "level") == 0) {
        if (argc == 2) {
            // Display current log level
            shell_println("Current log levels (from least to most severe):");
            shell_println("  0: TRACE     - Detailed tracing information");
            shell_println("  1: DEBUG     - Debugging information");
            shell_println("  2: INFO      - General information");
            shell_println("  3: NOTICE    - Normal but significant events");
            shell_println("  4: WARNING   - Potential issues");
            shell_println("  5: ERROR     - Error conditions");
            shell_println("  6: CRITICAL  - Critical conditions");
            shell_println("  7: ALERT     - Action must be taken immediately");
            shell_println("  8: EMERGENCY - System is unusable");
        } else {
            // Set log level based on argument
            log_level_t level = LOG_LEVEL_INFO; // Default to info
            
            if (strcmp(argv[2], "trace") == 0) {
                level = LOG_LEVEL_TRACE;
            } else if (strcmp(argv[2], "debug") == 0) {
                level = LOG_LEVEL_DEBUG;
            } else if (strcmp(argv[2], "info") == 0) {
                level = LOG_LEVEL_INFO;
            } else if (strcmp(argv[2], "notice") == 0) {
                level = LOG_LEVEL_NOTICE;
            } else if (strcmp(argv[2], "warning") == 0) {
                level = LOG_LEVEL_WARNING;
            } else if (strcmp(argv[2], "error") == 0) {
                level = LOG_LEVEL_ERROR;
            } else if (strcmp(argv[2], "critical") == 0) {
                level = LOG_LEVEL_CRITICAL;
            } else if (strcmp(argv[2], "alert") == 0) {
                level = LOG_LEVEL_ALERT;
            } else if (strcmp(argv[2], "emergency") == 0) {
                level = LOG_LEVEL_EMERGENCY;
            } else {
                // Try to parse as numeric level
                int num_level = 0;
                for (int i = 0; argv[2][i] != '\0'; i++) {
                    if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                        num_level = num_level * 10 + (argv[2][i] - '0');
                    } else {
                        shell_println("Invalid log level");
                        return;
                    }
                }
                if (num_level >= 0 && num_level <= 8) {
                    level = (log_level_t)num_level;
                } else {
                    shell_println("Invalid log level. Must be 0-8 or named level");
                    return;
                }
            }
            
            // Set the new log level
            log_set_level(level);
            shell_print("Log level set to: ");
            shell_println(log_level_to_string(level));
        }
    }
    else if (strcmp(argv[1], "dest") == 0) {
        if (argc == 2) {
            // Display current destinations
            shell_println("Log destination options:");
            shell_println("  memory   - Store logs in memory buffer");
            shell_println("  screen   - Output logs to screen");
            shell_println("  serial   - Output logs to serial port");
            shell_println("  all      - Output logs to all destinations");
            shell_println("");
            shell_println("Use + to combine destinations (e.g., memory+screen)");
        } else {
            // Parse destination options
            uint8_t dest = 0;
            char *token = argv[2];
            char *next;
            
            while (token && *token) {
                // Find the end of the current token (before the +)
                next = token;
                while (*next && *next != '+') next++;
                
                // Temporarily null-terminate this token
                char old_char = *next;
                *next = '\0';
                
                // Process the token
                if (strcmp(token, "memory") == 0) {
                    dest |= LOG_DEST_MEMORY;
                } else if (strcmp(token, "screen") == 0) {
                    dest |= LOG_DEST_SCREEN;
                } else if (strcmp(token, "serial") == 0) {
                    dest |= LOG_DEST_SERIAL;
                } else if (strcmp(token, "all") == 0) {
                    dest = LOG_DEST_ALL;
                    break;
                } else {
                    shell_println("Invalid destination option");
                    return;
                }
                
                // Restore the character and move to next token
                *next = old_char;
                if (*next == '+') next++;
                token = *next ? next : NULL;
            }
            
            // Set the new destinations
            log_set_destinations(dest);
            shell_println("Log destinations updated");
        }
    }
    else if (strcmp(argv[1], "format") == 0) {
        if (argc == 2) {
            // Display format options
            shell_println("Log format options:");
            shell_println("  timestamp - Include timestamp");
            shell_println("  level     - Include log level");
            shell_println("  source    - Include source info");
            shell_println("  full      - Include all format options");
            shell_println("");
            shell_println("Use + to combine options (e.g., level+source)");
        } else {
            // Parse format options
            uint8_t format = 0;
            char *token = argv[2];
            char *next;
            
            while (token && *token) {
                // Find the end of the current token (before the +)
                next = token;
                while (*next && *next != '+') next++;
                
                // Temporarily null-terminate this token
                char old_char = *next;
                *next = '\0';
                
                // Process the token
                if (strcmp(token, "timestamp") == 0) {
                    format |= LOG_FORMAT_TIMESTAMP;
                } else if (strcmp(token, "level") == 0) {
                    format |= LOG_FORMAT_LEVEL;
                } else if (strcmp(token, "source") == 0) {
                    format |= LOG_FORMAT_SOURCE;
                } else if (strcmp(token, "full") == 0) {
                    format = LOG_FORMAT_FULL;
                    break;
                } else {
                    shell_println("Invalid format option");
                    return;
                }
                
                // Restore the character and move to next token
                *next = old_char;
                if (*next == '+') next++;
                token = *next ? next : NULL;
            }
            
            // Set the new format options
            log_set_format_options(format);
            shell_println("Log format options updated");
        }
    }
    else if (strcmp(argv[1], "test") == 0) {
        // Generate test log messages at various levels
        shell_println("Generating test log messages...");
        
        log_trace("TEST", "This is a TRACE message");
        log_debug("TEST", "This is a DEBUG message");
        log_info("TEST", "This is an INFO message");
        log_notice("TEST", "This is a NOTICE message");
        log_warning("TEST", "This is a WARNING message");
        log_error("TEST", "This is an ERROR message");
        log_critical("TEST", "This is a CRITICAL message");
        log_alert("TEST", "This is an ALERT message");
        log_emergency("TEST", "This is an EMERGENCY message");
        
        shell_println("Test messages generated. Use 'log show' to see messages in the buffer.");
    }
    else {
        shell_println("Unknown log subcommand. Try 'log' for help.");
    }
}

// Add the implementation of the wdm command
void cmd_wdm(int argc, char *argv[]) {
    log_debug("SHELL", "Executing wdm command");
    
    #include "../drivers/windows/driver_manager.h"
    
    if (argc < 2) {
        shell_println("Windows Driver Manager Commands:");
        shell_println("  wdm init         - Initialize the Windows driver subsystem");
        shell_println("  wdm shutdown     - Shutdown the Windows driver subsystem");
        shell_println("  wdm load <path> <name> <type> - Load a Windows driver");
        shell_println("  wdm unload <id>  - Unload a Windows driver");
        shell_println("  wdm start <id>   - Start a loaded driver");
        shell_println("  wdm stop <id>    - Stop a running driver");
        shell_println("  wdm list         - List all loaded drivers");
        shell_println("  wdm info <id>    - Display information about a driver");
        shell_println("  wdm devices      - List all registered devices");
        return;
    }
    
    if (strcmp(argv[1], "init") == 0) {
        // Initialize the Windows driver manager
        int status = driver_manager_init();
        if (status == 0) {
            shell_println("Windows driver subsystem initialized successfully");
        } else {
            shell_print("Failed to initialize Windows driver subsystem: error ");
            char buf[16];
            itoa(status, buf, 10);
            shell_println(buf);
        }
    }
    else if (strcmp(argv[1], "shutdown") == 0) {
        // Shutdown the Windows driver manager
        driver_manager_shutdown();
        shell_println("Windows driver subsystem shutdown complete");
    }
    else if (strcmp(argv[1], "load") == 0) {
        // Load a Windows driver
        if (argc < 5) {
            shell_println("Usage: wdm load <path> <name> <type>");
            shell_println("Types: 0=unknown, 1=storage, 2=network, 3=display, 4=input, 5=audio");
            return;
        }
        
        const char* path = argv[2];
        const char* name = argv[3];
        int type = atoi(argv[4]);
        uint32_t flags = 0;
        
        int driver_id = driver_manager_load(path, name, (driver_type_t)type, flags);
        
        if (driver_id >= 0) {
            shell_print("Driver loaded successfully, ID: ");
            char buf[16];
            itoa(driver_id, buf, 10);
            shell_println(buf);
        } else {
            shell_print("Failed to load driver: error ");
            char buf[16];
            itoa(driver_id, buf, 10);
            shell_println(buf);
        }
    }
    else if (strcmp(argv[1], "unload") == 0) {
        // Unload a Windows driver
        if (argc < 3) {
            shell_println("Usage: wdm unload <id>");
            return;
        }
        
        int driver_id = atoi(argv[2]);
        int status = driver_manager_unload(driver_id);
        
        if (status == 0) {
            shell_println("Driver unloaded successfully");
        } else {
            shell_print("Failed to unload driver: error ");
            char buf[16];
            itoa(status, buf, 10);
            shell_println(buf);
        }
    }
    else if (strcmp(argv[1], "start") == 0) {
        // Start a loaded driver
        if (argc < 3) {
            shell_println("Usage: wdm start <id>");
            return;
        }
        
        int driver_id = atoi(argv[2]);
        int status = driver_manager_start(driver_id);
        
        if (status == 0) {
            shell_println("Driver started successfully");
        } else {
            shell_print("Failed to start driver: error ");
            char buf[16];
            itoa(status, buf, 10);
            shell_println(buf);
        }
    }
    else if (strcmp(argv[1], "stop") == 0) {
        // Stop a running driver
        if (argc < 3) {
            shell_println("Usage: wdm stop <id>");
            return;
        }
        
        int driver_id = atoi(argv[2]);
        int status = driver_manager_stop(driver_id);
        
        if (status == 0) {
            shell_println("Driver stopped successfully");
        } else {
            shell_print("Failed to stop driver: error ");
            char buf[16];
            itoa(status, buf, 10);
            shell_println(buf);
        }
    }
    else if (strcmp(argv[1], "list") == 0) {
        // List all loaded drivers
        int count = driver_manager_get_count();
        
        if (count <= 0) {
            shell_println("No drivers loaded");
            return;
        }
        
        shell_println("ID | Name                 | Type   | State   | Devices");
        shell_println("---+----------------------+--------+---------+--------");
        
        for (int i = 0; i < count; i++) {
            driver_info_t info;
            if (driver_manager_get_info(i, &info) == 0) {
                char id[8];
                itoa(i, id, 10);
                
                char devices[8];
                itoa(info.device_count, devices, 10);
                
                // Print driver ID (right-aligned)
                int spaces = 2 - strlen(id);
                while (spaces-- > 0) shell_print(" ");
                shell_print(id);
                shell_print(" | ");
                
                // Print driver name (truncated if needed)
                shell_print(info.name);
                spaces = 20 - strlen(info.name);
                while (spaces-- > 0) shell_print(" ");
                shell_print(" | ");
                
                // Print driver type
                const char* type_str = "Unknown";
                switch (info.type) {
                    case DRV_TYPE_STORAGE: type_str = "Storage"; break;
                    case DRV_TYPE_NETWORK: type_str = "Network"; break;
                    case DRV_TYPE_DISPLAY: type_str = "Display"; break;
                    case DRV_TYPE_INPUT: type_str = "Input"; break;
                    case DRV_TYPE_AUDIO: type_str = "Audio"; break;
                    case DRV_TYPE_USB: type_str = "USB"; break;
                    case DRV_TYPE_SERIAL: type_str = "Serial"; break;
                    case DRV_TYPE_PARALLEL: type_str = "Parallel"; break;
                    case DRV_TYPE_SYSTEM: type_str = "System"; break;
                }
                shell_print(type_str);
                spaces = 6 - strlen(type_str);
                while (spaces-- > 0) shell_print(" ");
                shell_print(" | ");
                
                // Print driver state
                const char* state_str = "Unknown";
                switch (info.state) {
                    case DRV_STATE_UNLOADED: state_str = "Unloaded"; break;
                    case DRV_STATE_LOADED: state_str = "Loaded"; break;
                    case DRV_STATE_STARTED: state_str = "Started"; break;
                    case DRV_STATE_PAUSED: state_str = "Paused"; break;
                    case DRV_STATE_STOPPED: state_str = "Stopped"; break;
                    case DRV_STATE_ERROR: state_str = "Error"; break;
                }
                shell_print(state_str);
                spaces = 7 - strlen(state_str);
                while (spaces-- > 0) shell_print(" ");
                shell_print(" | ");
                
                // Print device count
                shell_println(devices);
            }
        }
    }
    else if (strcmp(argv[1], "info") == 0) {
        // Display information about a driver
        if (argc < 3) {
            shell_println("Usage: wdm info <id>");
            return;
        }
        
        int driver_id = atoi(argv[2]);
        driver_info_t info;
        
        if (driver_manager_get_info(driver_id, &info) == 0) {
            shell_println("Driver Information:");
            
            shell_print("  Name: ");
            shell_println(info.name);
            
            shell_print("  Description: ");
            shell_println(info.description);
            
            shell_print("  Version: ");
            shell_println(info.version);
            
            shell_print("  Type: ");
            switch (info.type) {
                case DRV_TYPE_UNKNOWN: shell_println("Unknown"); break;
                case DRV_TYPE_STORAGE: shell_println("Storage"); break;
                case DRV_TYPE_NETWORK: shell_println("Network"); break;
                case DRV_TYPE_DISPLAY: shell_println("Display"); break;
                case DRV_TYPE_INPUT: shell_println("Input"); break;
                case DRV_TYPE_AUDIO: shell_println("Audio"); break;
                case DRV_TYPE_USB: shell_println("USB"); break;
                case DRV_TYPE_SERIAL: shell_println("Serial"); break;
                case DRV_TYPE_PARALLEL: shell_println("Parallel"); break;
                case DRV_TYPE_SYSTEM: shell_println("System"); break;
                default: shell_println("Invalid"); break;
            }
            
            shell_print("  State: ");
            switch (info.state) {
                case DRV_STATE_UNLOADED: shell_println("Unloaded"); break;
                case DRV_STATE_LOADED: shell_println("Loaded"); break;
                case DRV_STATE_STARTED: shell_println("Started"); break;
                case DRV_STATE_PAUSED: shell_println("Paused"); break;
                case DRV_STATE_STOPPED: shell_println("Stopped"); break;
                case DRV_STATE_ERROR: shell_println("Error"); break;
                default: shell_println("Invalid"); break;
            }
            
            shell_print("  Device Count: ");
            char buf[16];
            itoa(info.device_count, buf, 10);
            shell_println(buf);
            
            shell_print("  Error Count: ");
            itoa(info.error_count, buf, 10);
            shell_println(buf);
        } else {
            shell_print("Failed to get driver information for ID: ");
            char buf[16];
            itoa(driver_id, buf, 10);
            shell_println(buf);
        }
    }
    else {
        shell_println("Unknown wdm command. Type 'wdm' for a list of commands.");
    }
}

// USB command implementation
void cmd_usb(int argc, char *argv[]) {
    // Include necessary headers
    #include "../drivers/usb/usb_mass_storage.h"
    #include "../hal/include/hal_usb.h"
    
    if (argc == 1) {
        // Display usage information if no arguments
        shell_println("USB Subsystem Commands:");
        shell_println("  usb init       - Initialize USB subsystem");
        shell_println("  usb scan       - Scan for USB devices");
        shell_println("  usb list       - List connected USB devices");
        shell_println("  usb info <id>  - Show detailed information about a USB device");
        shell_println("  usb mount <id> <path> - Mount a USB storage device");
        shell_println("  usb umount <id>       - Unmount a USB storage device");
        shell_println("  usb reset <id>        - Reset a USB device");
        shell_println("  usb shutdown          - Shut down USB subsystem");
        shell_println("");
        shell_println("Examples:");
        shell_println("  usb init              - Initialize the USB subsystem");
        shell_println("  usb mount 1 /mnt/usb  - Mount USB device 1 at /mnt/usb");
        return;
    }
    
    // Handle various USB subcommands
    if (strcmp(argv[1], "init") == 0) {
        shell_println("Initializing USB subsystem...");
        
        // First initialize the HAL USB
        int result = hal_usb_init();
        if (result < 0) {
            shell_println("Failed to initialize USB HAL!");
            return;
        }
        
        // Then initialize the Mass Storage driver
        result = usb_mass_storage_init();
        if (result < 0) {
            shell_println("Failed to initialize USB Mass Storage driver!");
            return;
        }
        
        shell_println("USB subsystem initialized successfully.");
    }
    else if (strcmp(argv[1], "scan") == 0) {
        shell_println("Scanning for USB devices...");
        
        // Detect USB Mass Storage devices
        int count = usb_mass_storage_detect_devices();
        
        if (count < 0) {
            shell_println("Error scanning for USB devices!");
            return;
        }
        
        char buffer[16];
        int_to_string(count, buffer);
        shell_print("Found ");
        shell_print(buffer);
        shell_println(" USB Mass Storage devices.");
    }
    else if (strcmp(argv[1], "list") == 0) {
        // Get list of USB Mass Storage devices
        usb_mass_storage_device_t devices[8];
        int count = usb_mass_storage_get_devices(devices, 8);
        
        if (count < 0) {
            shell_println("Error getting USB device list!");
            return;
        }
        
        if (count == 0) {
            shell_println("No USB storage devices connected.");
            shell_println("Try 'usb scan' to scan for devices.");
            return;
        }
        
        // Display header for device list
        shell_println("ID  Vendor      Product                  Size        Mounted");
        shell_println("--  ----------  -----------------------  ----------  -------");
        
        // Display each device
        char buffer[32];
        for (int i = 0; i < count; i++) {
            // Device ID
            int_to_string(devices[i].device_addr, buffer);
            shell_print(buffer);
            shell_print("  ");
            
            // Add padding for ID
            int padding = 2 - strlen(buffer);
            while (padding-- > 0) {
                shell_print(" ");
            }
            
            // Vendor
            shell_print(devices[i].vendor);
            padding = 12 - strlen(devices[i].vendor);
            while (padding-- > 0) {
                shell_print(" ");
            }
            
            // Product
            shell_print(devices[i].product);
            padding = 25 - strlen(devices[i].product);
            while (padding-- > 0) {
                shell_print(" ");
            }
            
            // Size (blocks * block size)
            uint64_t size_kb = ((uint64_t)devices[i].num_blocks * devices[i].block_size) / 1024;
            if (size_kb >= 1024 * 1024) {
                // Display in GB
                uint64_t size_gb = size_kb / (1024 * 1024);
                int_to_string(size_gb, buffer);
                shell_print(buffer);
                shell_print(" GB    ");
            } else if (size_kb >= 1024) {
                // Display in MB
                uint64_t size_mb = size_kb / 1024;
                int_to_string(size_mb, buffer);
                shell_print(buffer);
                shell_print(" MB    ");
            } else {
                // Display in KB
                int_to_string(size_kb, buffer);
                shell_print(buffer);
                shell_print(" KB    ");
            }
            
            // Mounted status
            if (devices[i].mounted) {
                shell_println("Yes");
            } else {
                shell_println("No");
            }
        }
    }
    else if (strcmp(argv[1], "info") == 0) {
        // Display detailed information about a device
        if (argc < 3) {
            shell_println("Usage: usb info <device_id>");
            return;
        }
        
        // Parse device ID
        int device_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                device_id = device_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Invalid device ID!");
                return;
            }
        }
        
        // Get device info
        usb_mass_storage_device_t devices[8];
        int count = usb_mass_storage_get_devices(devices, 8);
        int found = 0;
        
        for (int i = 0; i < count; i++) {
            if (devices[i].device_addr == device_id) {
                // Display detailed information
                shell_println("USB Device Information:");
                
                shell_print("  Address:     ");
                char buffer[16];
                int_to_string(devices[i].device_addr, buffer);
                shell_println(buffer);
                
                shell_print("  Vendor:      ");
                shell_println(devices[i].vendor);
                
                shell_print("  Product:     ");
                shell_println(devices[i].product);
                
                shell_print("  Revision:    ");
                shell_println(devices[i].revision);
                
                shell_print("  Block Size:  ");
                int_to_string(devices[i].block_size, buffer);
                shell_print(buffer);
                shell_println(" bytes");
                
                shell_print("  Num Blocks:  ");
                int_to_string(devices[i].num_blocks, buffer);
                shell_println(buffer);
                
                shell_print("  Total Size:  ");
                uint64_t size_kb = ((uint64_t)devices[i].num_blocks * devices[i].block_size) / 1024;
                if (size_kb >= 1024 * 1024) {
                    // Display in GB
                    uint64_t size_gb = size_kb / (1024 * 1024);
                    int_to_string(size_gb, buffer);
                    shell_print(buffer);
                    shell_println(" GB");
                } else if (size_kb >= 1024) {
                    // Display in MB
                    uint64_t size_mb = size_kb / 1024;
                    int_to_string(size_mb, buffer);
                    shell_print(buffer);
                    shell_println(" MB");
                } else {
                    // Display in KB
                    int_to_string(size_kb, buffer);
                    shell_print(buffer);
                    shell_println(" KB");
                }
                
                shell_print("  Interface:   ");
                int_to_string(devices[i].interface_num, buffer);
                shell_println(buffer);
                
                shell_print("  Max LUN:     ");
                int_to_string(devices[i].max_lun, buffer);
                shell_println(buffer);
                
                shell_print("  Status:      ");
                shell_println(devices[i].mounted ? "Mounted" : "Not mounted");
                
                // Test if the device is ready
                int ready = usb_mass_storage_test_unit_ready(device_id, 0);
                shell_print("  Ready:       ");
                if (ready > 0) {
                    shell_println("Yes");
                } else if (ready == 0) {
                    shell_println("No");
                } else {
                    shell_println("Error");
                }
                
                found = 1;
                break;
            }
        }
        
        if (!found) {
            shell_println("Device not found! Use 'usb list' to see available devices.");
        }
    }
    else if (strcmp(argv[1], "mount") == 0) {
        // Mount a USB storage device
        if (argc < 4) {
            shell_println("Usage: usb mount <device_id> <mount_point>");
            return;
        }
        
        // Parse device ID
        int device_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                device_id = device_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Invalid device ID!");
                return;
            }
        }
        
        const char* mount_point = argv[3];
        
        // Mount the device
        shell_print("Mounting USB device ");
        char buffer[16];
        int_to_string(device_id, buffer);
        shell_print(buffer);
        shell_print(" on ");
        shell_print(mount_point);
        shell_println("...");
        
        int result = usb_mass_storage_mount(device_id, mount_point);
        if (result == 0) {
            shell_println("Device mounted successfully.");
        } else {
            shell_println("Failed to mount device!");
        }
    }
    else if (strcmp(argv[1], "umount") == 0 || strcmp(argv[1], "unmount") == 0) {
        // Unmount a USB storage device
        if (argc < 3) {
            shell_println("Usage: usb umount <device_id>");
            return;
        }
        
        // Parse device ID
        int device_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                device_id = device_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Invalid device ID!");
                return;
            }
        }
        
        // Unmount the device
        shell_print("Unmounting USB device ");
        char buffer[16];
        int_to_string(device_id, buffer);
        shell_print(buffer);
        shell_println("...");
        
        int result = usb_mass_storage_unmount(device_id);
        if (result == 0) {
            shell_println("Device unmounted successfully.");
        } else {
            shell_println("Failed to unmount device!");
        }
    }
    else if (strcmp(argv[1], "reset") == 0) {
        // Reset a USB device port
        if (argc < 3) {
            shell_println("Usage: usb reset <device_id>");
            return;
        }
        
        // Parse device ID
        int device_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                device_id = device_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Invalid device ID!");
                return;
            }
        }
        
        // Reset the device port
        // For simplicity we'll assume controller 0, port = device_id
        shell_print("Resetting USB device ");
        char buffer[16];
        int_to_string(device_id, buffer);
        shell_print(buffer);
        shell_println("...");
        
        int result = hal_usb_reset_port(0, device_id);
        if (result == 0) {
            shell_println("Device reset successfully.");
            
            // After reset, we need to re-scan for devices
            shell_println("Rescanning for USB devices...");
            usb_mass_storage_detect_devices();
        } else {
            shell_println("Failed to reset device!");
        }
    }
    else if (strcmp(argv[1], "shutdown") == 0) {
        // Shut down the USB subsystem
        shell_println("Shutting down USB subsystem...");
        
        // First shut down the Mass Storage driver
        usb_mass_storage_shutdown();
        
        // Then shut down the HAL USB
        hal_usb_shutdown();
        
        shell_println("USB subsystem shut down.");
    }
    else {
        shell_println("Unknown USB command. Try 'usb' for help.");
    }
}

// Process Manager UI command implementation
void cmd_taskman(int argc, char *argv[]) {
    #include "task.h"
    #include "io.h"
    #include "keyboard.h"
    
    // Process Manager UI constants
    #define TASKMAN_REFRESH_DELAY 500000 // Microseconds between screen refreshes
    #define TASKMAN_HEADER_COLOR vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY)
    #define TASKMAN_SELECTED_COLOR vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE)
    #define TASKMAN_NORMAL_COLOR vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK)
    #define TASKMAN_RUNNING_COLOR vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK)
    #define TASKMAN_SYSTEM_COLOR vga_entry_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK)
    #define TASKMAN_WARNING_COLOR vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK)
    #define TASKMAN_ERROR_COLOR vga_entry_color(VGA_COLOR_RED, VGA_COLOR_BLACK)
    
    // Static data for task manager state
    static int selected_task = 0;
    static int task_view_scroll = 0;
    static const int tasks_per_page = 15;
    static const char* help_text[] = {
        "UP/DOWN - Select task",
        "S - Suspend selected task",
        "R - Resume selected task",
        "T - Terminate selected task",
        "P - Priority (higher/lower)",
        "F5 - Refresh view",
        "ESC - Exit to shell",
        NULL
    };
    
    // Keep track of the last seen task count to detect changes
    static int last_task_count = 0;
    
    // Save original VGA state
    uint8_t original_color = vga_current_color;
    
    log_debug("SHELL", "Starting Task Manager UI");
    vga_clear_screen();
    
    // Run the Task Manager UI until user exits
    int running = 1;
    int refresh_needed = 1;
    
    while (running) {
        // Only redraw the screen if something changed
        if (refresh_needed) {
            // Get current task information
            int task_count = get_task_count();
            int current_task_id = get_current_task_id();
            
            // Auto-adjust selection if tasks have been added/removed
            if (task_count != last_task_count) {
                if (selected_task >= task_count) {
                    selected_task = task_count > 0 ? task_count - 1 : 0;
                }
                last_task_count = task_count;
            }
            
            // Adjust scroll position if needed
            if (selected_task < task_view_scroll) {
                task_view_scroll = selected_task;
            } else if (selected_task >= task_view_scroll + tasks_per_page) {
                task_view_scroll = selected_task - tasks_per_page + 1;
            }
            
            // Clear the screen
            vga_clear_screen();
            
            // Draw title bar
            vga_set_color(TASKMAN_HEADER_COLOR);
            for (int i = 0; i < VGA_WIDTH; i++) {
                vga_write_char_at(' ', i, 0);
            }
            vga_write_string_at("uintOS Task Manager", 30, 0);
            
            // Draw column headers
            vga_set_color(TASKMAN_HEADER_COLOR);
            vga_write_string_at("ID", 1, 1);
            vga_write_string_at("Name", 6, 1);
            vga_write_string_at("State", 30, 1);
            vga_write_string_at("Priv", 40, 1);
            vga_write_string_at("Flags", 50, 1);
            vga_write_string_at("Stack", 60, 1);
            vga_write_string_at("Parent", 70, 1);
            
            // Draw separator line
            vga_set_color(TASKMAN_NORMAL_COLOR);
            for (int i = 0; i < VGA_WIDTH; i++) {
                vga_write_char_at('-', i, 2);
            }
            
            // Display task information
            int displayed_tasks = 0;
            for (int i = task_view_scroll; i < task_count && displayed_tasks < tasks_per_page; i++) {
                task_info_t info;
                if (get_task_info(i, &info)) {
                    // Determine row color
                    if (i == selected_task) {
                        vga_set_color(TASKMAN_SELECTED_COLOR);
                    } else if (info.is_current) {
                        vga_set_color(TASKMAN_RUNNING_COLOR);
                    } else if (info.flags & TASK_FLAG_SYSTEM) {
                        vga_set_color(TASKMAN_SYSTEM_COLOR);
                    } else {
                        vga_set_color(TASKMAN_NORMAL_COLOR);
                    }
                    
                    int row = 3 + displayed_tasks;
                    
                    // ID column
                    char id_str[8];
                    int_to_string(info.id, id_str);
                    vga_write_string_at(id_str, 1, row);
                    
                    // Name column
                    vga_write_string_at(info.name, 6, row);
                    
                    // State column
                    const char* state_str = "Unknown";
                    switch (info.state) {
                        case TASK_STATE_UNUSED:    state_str = "Unused"; break;
                        case TASK_STATE_READY:     state_str = "Ready"; break;
                        case TASK_STATE_RUNNING:   state_str = "Running"; break;
                        case TASK_STATE_BLOCKED:   state_str = "Blocked"; break;
                        case TASK_STATE_SUSPENDED: state_str = "Suspended"; break;
                        case TASK_STATE_ZOMBIE:    state_str = "Zombie"; break;
                    }
                    vga_write_string_at(state_str, 30, row);
                    
                    // Privilege level column
                    const char* priv_str = "?";
                    switch (info.privilege_level) {
                        case TASK_PRIV_KERNEL:  priv_str = "Kernel"; break;
                        case TASK_PRIV_DRIVER:  priv_str = "Driver"; break;
                        case TASK_PRIV_SYSTEM:  priv_str = "System"; break;
                        case TASK_PRIV_USER:    priv_str = "User"; break;
                    }
                    vga_write_string_at(priv_str, 40, row);
                    
                    // Flags column
                    char flags[8] = "-----";
                    if (info.flags & TASK_FLAG_SYSTEM)   flags[0] = 'S';
                    if (info.flags & TASK_FLAG_USER)     flags[1] = 'U';
                    if (info.flags & TASK_FLAG_KERNEL)   flags[2] = 'K';
                    if (info.flags & TASK_FLAG_DRIVER)   flags[3] = 'D';
                    if (info.flags & TASK_FLAG_SERVICE)  flags[4] = 'V';
                    vga_write_string_at(flags, 50, row);
                    
                    // Stack size column
                    char stack_str[12];
                    int_to_string(info.stack_size, stack_str);
                    vga_write_string_at(stack_str, 60, row);
                    
                    // Parent ID column
                    if (info.parent_id >= 0) {
                        char parent_str[8];
                        int_to_string(info.parent_id, parent_str);
                        vga_write_string_at(parent_str, 70, row);
                    } else {
                        vga_write_string_at("-", 70, row);
                    }
                    
                    displayed_tasks++;
                }
            }
            
            // Draw status bar
            vga_set_color(TASKMAN_HEADER_COLOR);
            for (int i = 0; i < VGA_WIDTH; i++) {
                vga_write_char_at(' ', i, 20);
            }
            
            char status[64];
            int_to_string(task_count, status);
            strcat(status, " Tasks | Active: ");
            char active[8];
            int_to_string(current_task_id, active);
            strcat(status, active);
            vga_write_string_at(status, 1, 20);
            
            // Draw footer with help
            vga_set_color(TASKMAN_HEADER_COLOR);
            for (int i = 0; i < VGA_WIDTH; i++) {
                vga_write_char_at(' ', i, 22);
                vga_write_char_at(' ', i, 23);
            }
            
            for (int i = 0; help_text[i] != NULL; i++) {
                int x = i * 20;
                if (x < VGA_WIDTH) {
                    vga_write_string_at(help_text[i], x, 22);
                } else {
                    vga_write_string_at(help_text[i], x - VGA_WIDTH, 23);
                }
            }
            
            // Draw selected task details
            if (task_count > 0 && selected_task >= 0 && selected_task < task_count) {
                task_info_t info;
                if (get_task_info(selected_task, &info)) {
                    vga_set_color(TASKMAN_NORMAL_COLOR);
                    vga_write_string_at("Selected Task Details:", 1, 19);
                    
                    // Draw a box around details
                    vga_draw_box(0, 18, 79, 20, TASKMAN_NORMAL_COLOR);
                    
                    // Security SID information
                    char sid_str[128] = "SID: ";
                    if (info.user_sid.authority_value > 0) {
                        char auth[16];
                        int_to_string(info.user_sid.authority_value, auth);
                        strcat(sid_str, "S-1-");
                        strcat(sid_str, auth);
                        
                        for (int i = 0; i < info.user_sid.sub_authority_count; i++) {
                            strcat(sid_str, "-");
                            char sub_auth[16];
                            int_to_string(info.user_sid.sub_authorities[i], sub_auth);
                            strcat(sid_str, sub_auth);
                        }
                    } else {
                        strcat(sid_str, "None");
                    }
                    
                    vga_write_string_at(sid_str, 30, 19);
                }
            }
            
            // Reset the refresh flag
            refresh_needed = 0;
        }
        
        // Check for keyboard input
        if (is_key_available()) {
            char key = keyboard_read_key();
            
            switch (key) {
                case KEY_UP_ARROW:
                    if (selected_task > 0) {
                        selected_task--;
                        refresh_needed = 1;
                    }
                    break;
                    
                case KEY_DOWN_ARROW:
                    if (selected_task < get_task_count() - 1) {
                        selected_task++;
                        refresh_needed = 1;
                    }
                    break;
                    
                case 's':   // Suspend task
                case 'S':
                    if (selected_task >= 0 && selected_task < get_task_count()) {
                        int result = suspend_task(selected_task);
                        refresh_needed = 1;
                    }
                    break;
                    
                case 'r':   // Resume task
                case 'R':
                    if (selected_task >= 0 && selected_task < get_task_count()) {
                        int result = resume_task(selected_task);
                        refresh_needed = 1;
                    }
                    break;
                    
                case 't':   // Terminate task
                case 'T':
                    if (selected_task >= 0 && selected_task < get_task_count()) {
                        task_info_t info;
                        if (get_task_info(selected_task, &info)) {
                            // Don't allow terminating the idle task or the current task (ourselves)
                            if (!info.is_current && strcmp(info.name, "System Idle") != 0) {
                                int result = terminate_task(selected_task, 0);
                                refresh_needed = 1;
                            }
                        }
                    }
                    break;
                    
                case 'p':   // Increase priority
                case 'P':
                    // Priority adjustment would go here if supported
                    refresh_needed = 1;
                    break;
                    
                case 0x3F:  // F5 key - refresh
                    refresh_needed = 1;
                    break;
                    
                case KEY_ESC:  // Exit
                    running = 0;
                    break;
            }
        }
        
        // Short delay to limit refresh rate and CPU usage
        for (volatile int i = 0; i < TASKMAN_REFRESH_DELAY; i++) {
            // Simple delay
        }
        
        // Poll for changes in task count every few iterations
        static int poll_counter = 0;
        if (++poll_counter >= 5) {
            poll_counter = 0;
            int current_count = get_task_count();
            if (current_count != last_task_count) {
                refresh_needed = 1;
            }
        }
    }
    
    // Restore original console state
    vga_set_color(original_color);
    vga_clear_screen();
    
    log_debug("SHELL", "Task Manager UI exited");
}

// VM management command implementation
void cmd_vm(int argc, char *argv[]) {
    // Include necessary headers
    #include "virtualization/vmx.h"
    #include "virtualization/vm_memory.h"
    
    if (argc == 1) {
        // Display usage information if no arguments
        shell_println("Virtual Machine Management Commands:");
        shell_println("  vm init        - Initialize hardware virtualization subsystem");
        shell_println("  vm create <name> <memory_kb> <vcpus> - Create a new virtual machine");
        shell_println("  vm delete <id>  - Delete a virtual machine");
        shell_println("  vm start <id>   - Start a virtual machine");
        shell_println("  vm stop <id>    - Stop a virtual machine");
        shell_println("  vm pause <id>   - Pause a running virtual machine");
        shell_println("  vm resume <id>  - Resume a paused virtual machine");
        shell_println("  vm list         - List all virtual machines");
        shell_println("  vm info <id>    - Display detailed information about a VM");
        shell_println("  vm load <id> <image> - Load a kernel image into a VM");
        shell_println("  vm snapshot <id> <file> [flags] - Create a VM snapshot");
        shell_println("  vm restore <file> - Restore a VM from a snapshot");
        shell_println("");
        shell_println("Examples:");
        shell_println("  vm create myvm 65536 1    - Create a VM with 64MB RAM and 1 vCPU");
        shell_println("  vm start 1               - Start VM with ID 1");
        shell_println("  vm snapshot 1 snapshot.bin - Create snapshot of VM 1");
        return;
    }
    else if (strcmp(argv[1], "init") == 0) {
        shell_println("Initializing hardware virtualization subsystem...");
        
        if (!vmx_is_supported()) {
            shell_println("Error: CPU does not support hardware virtualization (Intel VT-x)");
            shell_println("Make sure virtualization is enabled in BIOS/UEFI settings");
            return;
        }
        
        int result = vmx_init();
        if (result != 0) {
            shell_println("Error: Failed to initialize virtualization subsystem");
            return;
        }
        
        shell_println("Hardware virtualization subsystem initialized successfully");
    }
    else if (strcmp(argv[1], "create") == 0) {
        // Create a new VM
        if (argc < 5) {
            shell_println("Usage: vm create <name> <memory_kb> <vcpus>");
            return;
        }
        
        const char* name = argv[2];
        
        // Parse memory size
        uint32_t memory_kb = 0;
        for (int i = 0; argv[3][i]; i++) {
            if (argv[3][i] >= '0' && argv[3][i] <= '9') {
                memory_kb = memory_kb * 10 + (argv[3][i] - '0');
            } else {
                shell_println("Error: Invalid memory size.");
                return;
            }
        }
        
        // Parse vCPU count
        uint32_t vcpus = 0;
        for (int i = 0; argv[4][i]; i++) {
            if (argv[4][i] >= '0' && argv[4][i] <= '9') {
                vcpus = vcpus * 10 + (argv[4][i] - '0');
            } else {
                shell_println("Error: Invalid vCPU count.");
                return;
            }
        }
        
        // Validate parameters
        if (memory_kb < 4096) {
            shell_println("Error: Memory size must be at least 4096 KB (4 MB).");
            return;
        }
        
        if (vcpus < 1 || vcpus > 16) {
            shell_println("Error: vCPU count must be between 1 and 16.");
            return;
        }
        
        // Create the VM
        shell_print("Creating VM '");
        shell_print(name);
        shell_print("' with ");
        
        // Display memory in appropriate units
        if (memory_kb >= 1024*1024) {
            char gb_str[16];
            int_to_string(memory_kb / (1024*1024), gb_str);
            shell_print(gb_str);
            shell_print(" GB");
        } else if (memory_kb >= 1024) {
            char mb_str[16];
            int_to_string(memory_kb / 1024, mb_str);
            shell_print(mb_str);
            shell_print(" MB");
        } else {
            char kb_str[16];
            int_to_string(memory_kb, kb_str);
            shell_print(kb_str);
            shell_print(" KB");
        }
        
        shell_print(" memory and ");
        char vcpu_str[16];
        int_to_string(vcpus, vcpu_str);
        shell_print(vcpu_str);
        shell_println(" vCPU(s)...");
        
        int vm_id = vmx_create_vm(name, memory_kb, vcpus);
        if (vm_id < 0) {
            shell_println("Error: Failed to create virtual machine.");
            return;
        }
        
        shell_print("VM created successfully with ID: ");
        char id_str[16];
        int_to_string(vm_id, id_str);
        shell_println(id_str);
    }
    else if (strcmp(argv[1], "delete") == 0) {
        // Delete a VM
        if (argc < 3) {
            shell_println("Usage: vm delete <id>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        // Delete the VM
        shell_print("Deleting VM with ID ");
        shell_print(argv[2]);
        shell_println("...");
        
        int result = vmx_delete_vm(vm_id);
        if (result != 0) {
            shell_println("Error: Failed to delete VM. It may be running or not exist.");
            return;
        }
        
        shell_println("VM deleted successfully.");
    }
    else if (strcmp(argv[1], "start") == 0) {
        // Start a VM
        if (argc < 3) {
            shell_println("Usage: vm start <id>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        // Start the VM
        shell_print("Starting VM with ID ");
        shell_print(argv[2]);
        shell_println("...");
        
        int result = vmx_start_vm(vm_id);
        if (result != 0) {
            shell_println("Error: Failed to start VM. It may already be running or not exist.");
            return;
        }
        
        shell_println("VM started successfully.");
    }
    else if (strcmp(argv[1], "stop") == 0) {
        // Stop a VM
        if (argc < 3) {
            shell_println("Usage: vm stop <id>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        // Stop the VM
        shell_print("Stopping VM with ID ");
        shell_print(argv[2]);
        shell_println("...");
        
        int result = vmx_stop_vm(vm_id);
        if (result != 0) {
            shell_println("Error: Failed to stop VM. It may not be running or not exist.");
            return;
        }
        
        shell_println("VM stopped successfully.");
    }
    else if (strcmp(argv[1], "pause") == 0) {
        // Pause a VM
        if (argc < 3) {
            shell_println("Usage: vm pause <id>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        // Pause the VM
        shell_print("Pausing VM with ID ");
        shell_print(argv[2]);
        shell_println("...");
        
        int result = vmx_pause_vm(vm_id);
        if (result != 0) {
            shell_println("Error: Failed to pause VM. It may not be running or not exist.");
            return;
        }
        
        shell_println("VM paused successfully.");
    }
    else if (strcmp(argv[1], "resume") == 0) {
        // Resume a VM
        if (argc < 3) {
            shell_println("Usage: vm resume <id>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        // Resume the VM
        shell_print("Resuming VM with ID ");
        shell_print(argv[2]);
        shell_println("...");
        
        int result = vmx_resume_vm(vm_id);
        if (result != 0) {
            shell_println("Error: Failed to resume VM. It may not be paused or not exist.");
            return;
        }
        
        shell_println("VM resumed successfully.");
    }
    else if (strcmp(argv[1], "list") == 0) {
        // List all VMs
        vm_instance_t vms[MAX_VMS];
        int count = vmx_list_vms(vms, MAX_VMS);
        
        if (count < 0) {
            shell_println("Error: Failed to list VMs.");
            return;
        }
        
        if (count == 0) {
            shell_println("No virtual machines found.");
            return;
        }
        
        // Display VM table header
        shell_println("ID | Name                 | Memory       | vCPUs | State");
        shell_println("---+----------------------+--------------+-------+------------");
        
        // Display each VM
        for (int i = 0; i < count; i++) {
            // ID column
            char id_str[8];
            int_to_string(vms[i].id, id_str);
            
            // Add padding for ID
            int padding = 2 - strlen(id_str);
            while (padding-- > 0) {
                shell_print(" ");
            }
            
            shell_print(id_str);
            shell_print(" | ");
            
            // Name column
            shell_print(vms[i].name);
            padding = 20 - strlen(vms[i].name);
            while (padding-- > 0) {
                shell_print(" ");
            }
            shell_print(" | ");
            
            // Memory column
            char mem_str[16];
            if (vms[i].allocated_memory >= 1024*1024) {
                int_to_string(vms[i].allocated_memory / (1024*1024), mem_str);
                shell_print(mem_str);
                shell_print(" GB");
                padding = 10 - strlen(mem_str) - 3;
            } else if (vms[i].allocated_memory >= 1024) {
                int_to_string(vms[i].allocated_memory / 1024, mem_str);
                shell_print(mem_str);
                shell_print(" MB");
                padding = 10 - strlen(mem_str) - 3;
            } else {
                int_to_string(vms[i].allocated_memory, mem_str);
                shell_print(mem_str);
                shell_print(" KB");
                padding = 10 - strlen(mem_str) - 3;
            }
            while (padding-- > 0) {
                shell_print(" ");
            }
            shell_print(" | ");
            
            // vCPUs column
            char vcpu_str[8];
            int_to_string(vms[i].vcpu_count, vcpu_str);
            shell_print(vcpu_str);
            padding = 5 - strlen(vcpu_str);
            while (padding-- > 0) {
                shell_print(" ");
            }
            shell_print(" | ");
            
            // State column
            const char* state;
            switch (vms[i].state) {
                case VM_STATE_READY:      state = "Ready"; break;
                case VM_STATE_RUNNING:    state = "Running"; break;
                case VM_STATE_PAUSED:     state = "Paused"; break;
                case VM_STATE_ERROR:      state = "Error"; break;
                case VM_STATE_TERMINATED: state = "Terminated"; break;
                default:                  state = "Unknown"; break;
            }
            shell_println(state);
        }
        
        // Display summary
        char count_str[16];
        int_to_string(count, count_str);
        shell_print("Total: ");
        shell_print(count_str);
        shell_println(" virtual machines");
    }
    else if (strcmp(argv[1], "info") == 0) {
        // Display detailed VM info
        if (argc < 3) {
            shell_println("Usage: vm info <id>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        // Get VM info
        vm_instance_t vm;
        int result = vmx_get_vm_info(vm_id, &vm);
        if (result != 0) {
            shell_println("Error: VM not found.");
            return;
        }
        
        // Display VM information
        shell_println("Virtual Machine Information:");
        
        shell_print("  ID:      ");
        char id_str[16];
        int_to_string(vm.id, id_str);
        shell_println(id_str);
        
        shell_print("  Name:    ");
        shell_println(vm.name);
        
        shell_print("  Memory:  ");
        char mem_str[16];
        if (vm.allocated_memory >= 1024*1024) {
            int_to_string(vm.allocated_memory / (1024*1024), mem_str);
            shell_print(mem_str);
            shell_println(" GB");
        } else if (vm.allocated_memory >= 1024) {
            int_to_string(vm.allocated_memory / 1024, mem_str);
            shell_print(mem_str);
            shell_println(" MB");
        } else {
            int_to_string(vm.allocated_memory, mem_str);
            shell_print(mem_str);
            shell_println(" KB");
        }
        
        shell_print("  vCPUs:   ");
        char vcpu_str[8];
        int_to_string(vm.vcpu_count, vcpu_str);
        shell_println(vcpu_str);
        
        shell_print("  State:   ");
        switch (vm.state) {
            case VM_STATE_READY:      shell_println("Ready"); break;
            case VM_STATE_RUNNING:    shell_println("Running"); break;
            case VM_STATE_PAUSED:     shell_println("Paused"); break;
            case VM_STATE_ERROR:      shell_println("Error"); break;
            case VM_STATE_TERMINATED: shell_println("Terminated"); break;
            default:                  shell_println("Unknown"); break;
        }
        
        shell_print("  Type:    ");
        switch (vm.type) {
            case VM_TYPE_NORMAL:   shell_println("Normal"); break;
            case VM_TYPE_PARAVIRT: shell_println("Paravirtualized"); break;
            case VM_TYPE_FULLVIRT: shell_println("Fully Virtualized"); break;
            default:               shell_println("Unknown"); break;
        }
        
        // Display CR3 (page directory) in hex
        shell_print("  CR3:     0x");
        char cr3_str[16] = "00000000";
        for (int i = 7, val = vm.cr3; i >= 0; i--, val >>= 4) {
            cr3_str[i] = "0123456789ABCDEF"[val & 0xF];
        }
        shell_println(cr3_str);
        
        // Additional information could be shown here for a more detailed view
    }
    else if (strcmp(argv[1], "load") == 0) {
        // Load a kernel image into VM
        if (argc < 4) {
            shell_println("Usage: vm load <id> <image_path>");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        const char* image_path = argv[3];
        
        // Load the kernel image
        shell_print("Loading kernel image '");
        shell_print(image_path);
        shell_print("' into VM ");
        shell_print(argv[2]);
        shell_println("...");
        
        int result = vmx_load_kernel(vm_id, image_path);
        if (result != 0) {
            shell_println("Error: Failed to load kernel image.");
            return;
        }
        
        shell_println("Kernel image loaded successfully.");
    }
    else if (strcmp(argv[1], "snapshot") == 0) {
        // Create a snapshot of a VM
        if (argc < 4) {
            shell_println("Usage: vm snapshot <id> <file> [flags]");
            shell_println("Flags: 1 = Include memory, 2 = Include devices, 4 = Compress");
            return;
        }
        
        // Parse VM ID
        int vm_id = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                vm_id = vm_id * 10 + (argv[2][i] - '0');
            } else {
                shell_println("Error: Invalid VM ID.");
                return;
            }
        }
        
        const char* snapshot_path = argv[3];
        
        // Default flags: include memory and devices
        uint32_t flags = VM_SNAPSHOT_INCLUDE_MEMORY | VM_SNAPSHOT_INCLUDE_DEVICES;
        
        // Parse flags if provided
        if (argc > 4) {
            flags = 0;
            for (int i = 0; argv[4][i]; i++) {
                if (argv[4][i] >= '0' && argv[4][i] <= '9') {
                    flags = flags * 10 + (argv[4][i] - '0');
                } else {
                    shell_println("Error: Invalid flags.");
                    return;
                }
            }
        }
        
        shell_print("Creating snapshot of VM ");
        shell_print(argv[2]);
        shell_print(" to '");
        shell_print(snapshot_path);
        shell_println("'...");
        
        int result = vmx_create_snapshot(vm_id, snapshot_path, flags);
        if (result != 0) {
            shell_println("Error: Failed to create VM snapshot.");
            return;
        }
        
        shell_println("VM snapshot created successfully.");
    }
    else if (strcmp(argv[1], "restore") == 0) {
        // Restore a VM from a snapshot
        if (argc < 3) {
            shell_println("Usage: vm restore <snapshot_file>");
            return;
        }
        
        const char* snapshot_path = argv[2];
        uint32_t new_vm_id = 0;
        
        shell_print("Restoring VM from snapshot '");
        shell_print(snapshot_path);
        shell_println("'...");
        
        int result = vmx_restore_snapshot(snapshot_path, &new_vm_id);
        if (result != 0) {
            shell_println("Error: Failed to restore VM from snapshot.");
            return;
        }
        
        shell_print("VM restored successfully with ID: ");
        char id_str[16];
        int_to_string(new_vm_id, id_str);
        shell_println(id_str);
    }
    else {
        shell_println("Unknown VM command. Try 'vm' for help.");
    }
}

// GUI command implementation
void cmd_gui(int argc, char *argv[]) {
    #include "gui/window.h"
    #include "gui/controls.h"
    #include "gui/layout.h"
    #include "graphics/graphics.h"
    #include "keyboard.h"
    
    if (argc == 1) {
        // Display usage information if no arguments
        shell_println("GUI Subsystem Commands:");
        shell_println("  gui start    - Start the GUI subsystem and enter graphical mode");
        shell_println("  gui demo     - Run the GUI demonstration");
        shell_println("  gui config   - Configure GUI settings");
        shell_println("  gui info     - Display GUI subsystem information");
        shell_println("  gui shutdown - Shut down GUI subsystem and return to text mode");
        shell_println("");
        shell_println("Examples:");
        shell_println("  gui start    - Start the GUI interface");
        return;
    }
    
    if (strcmp(argv[1], "start") == 0) {
        shell_println("Starting GUI subsystem...");
        
        // First initialize the graphics subsystem
        int result = graphics_init();
        if (result != 0) {
            shell_println("Failed to initialize graphics subsystem!");
            return;
        }
        
        // Then initialize the window manager
        result = window_manager_init();
        if (result != 0) {
            shell_println("Failed to initialize window manager!");
            graphics_shutdown();
            return;
        }
        
        shell_println("GUI subsystem initialized. Starting GUI session...");
        shell_println("Press ESC to return to command line.");
        
        // Short delay to allow message to be read
        for (volatile int i = 0; i < 1000000; i++) { /* delay */ }
        
        // Start GUI main loop
        extern void gui_main_loop(void);
        gui_main_loop();
        
        // When GUI exits, restore text mode
        shell_println("GUI session ended. Back to shell.");
    }
    else if (strcmp(argv[1], "demo") == 0) {
        // Run GUI demo without full mode switch
        shell_println("Starting GUI demonstration...");
        
        // Initialize graphics for the demo
        int result = graphics_init();
        if (result != 0) {
            shell_println("Failed to initialize graphics subsystem!");
            return;
        }
        
        // Initialize the window manager
        result = window_manager_init();
        if (result != 0) {
            shell_println("Failed to initialize window manager!");
            graphics_shutdown();
            return;
        }
        
        shell_println("Running GUI demo. Press any key to exit.");
        
        // Short delay to allow message to be read
        for (volatile int i = 0; i < 1000000; i++) { /* delay */ }
        
        // Run the GUI demo
        extern void gui_demo(void);
        gui_demo();
        
        // Clean up and return to text mode
        window_manager_shutdown();
        graphics_shutdown();
        
        shell_println("GUI demonstration completed.");
    }
    else if (strcmp(argv[1], "config") == 0) {
        // Configure GUI settings
        if (argc >= 3 && strcmp(argv[2], "resolution") == 0) {
            if (argc < 4) {
                shell_println("Available resolutions:");
                shell_println("  640x480");
                shell_println("  800x600");
                shell_println("  1024x768");
                shell_println("  1280x1024");
                shell_println("Current resolution: 640x480");
                return;
            }
            
            // Set resolution based on argument
            if (strcmp(argv[3], "640x480") == 0) {
                shell_println("Setting resolution to 640x480");
                graphics_set_resolution(GRAPHICS_RES_640X480);
            }
            else if (strcmp(argv[3], "800x600") == 0) {
                shell_println("Setting resolution to 800x600");
                graphics_set_resolution(GRAPHICS_RES_800X600);
            }
            else if (strcmp(argv[3], "1024x768") == 0) {
                shell_println("Setting resolution to 1024x768");
                graphics_set_resolution(GRAPHICS_RES_1024X768);
            }
            else if (strcmp(argv[3], "1280x1024") == 0) {
                shell_println("Setting resolution to 1280x1024");
                graphics_set_resolution(GRAPHICS_RES_1280X1024);
            }
            else {
                shell_println("Unsupported resolution. Valid options: 640x480, 800x600, 1024x768, 1280x1024");
            }
        }
        else if (argc >= 3 && strcmp(argv[2], "theme") == 0) {
            if (argc < 4) {
                shell_println("Available themes:");
                shell_println("  classic  - Classic blue theme");
                shell_println("  modern   - Modern flat theme");
                shell_println("  dark     - Dark mode theme");
                shell_println("  light    - Light mode theme");
                shell_println("Current theme: classic");
                return;
            }
            
            // Set theme based on argument
            if (strcmp(argv[3], "classic") == 0) {
                shell_println("Setting theme to classic");
                extern void gui_set_theme(int theme);
                gui_set_theme(0); // Classic theme
            }
            else if (strcmp(argv[3], "modern") == 0) {
                shell_println("Setting theme to modern");
                extern void gui_set_theme(int theme);
                gui_set_theme(1); // Modern theme
            }
            else if (strcmp(argv[3], "dark") == 0) {
                shell_println("Setting theme to dark");
                extern void gui_set_theme(int theme);
                gui_set_theme(2); // Dark theme
            }
            else if (strcmp(argv[3], "light") == 0) {
                shell_println("Setting theme to light");
                extern void gui_set_theme(int theme);
                gui_set_theme(3); // Light theme
            }
            else {
                shell_println("Unsupported theme. Valid options: classic, modern, dark, light");
            }
        }
        else {
            shell_println("GUI Configuration Commands:");
            shell_println("  gui config resolution [setting] - Configure display resolution");
            shell_println("  gui config theme [setting]      - Configure GUI theme");
        }
    }
    else if (strcmp(argv[1], "info") == 0) {
        // Display GUI subsystem information
        shell_println("GUI Subsystem Information:");
        
        // Check if graphics is initialized
        int graphics_status = graphics_is_initialized();
        shell_print("Graphics Subsystem: ");
        shell_println(graphics_status ? "Initialized" : "Not initialized");
        
        // Current resolution
        if (graphics_status) {
            int width, height, bpp;
            graphics_get_resolution(&width, &height, &bpp);
            
            char buffer[64] = "Current Resolution: ";
            char num_str[16];
            
            int_to_string(width, num_str);
            strcat(buffer, num_str);
            strcat(buffer, "x");
            
            int_to_string(height, num_str);
            strcat(buffer, num_str);
            strcat(buffer, ", ");
            
            int_to_string(bpp, num_str);
            strcat(buffer, num_str);
            strcat(buffer, " bpp");
            
            shell_println(buffer);
        }
        
        // Check if window manager is initialized
        int wm_status = window_manager_is_initialized();
        shell_print("Window Manager: ");
        shell_println(wm_status ? "Initialized" : "Not initialized");
        
        // Window count
        if (wm_status) {
            int window_count = window_get_count();
            char buffer[32] = "Active Windows: ";
            char num_str[16];
            
            int_to_string(window_count, num_str);
            strcat(buffer, num_str);
            
            shell_println(buffer);
        }
        
        // Video memory information
        shell_print("Video Memory: ");
        uint32_t video_mem_kb = graphics_get_video_memory() / 1024;
        
        if (video_mem_kb >= 1024) {
            char mb_str[16];
            int_to_string(video_mem_kb / 1024, mb_str);
            shell_print(mb_str);
            shell_println(" MB");
        } else {
            char kb_str[16];
            int_to_string(video_mem_kb, kb_str);
            shell_print(kb_str);
            shell_println(" KB");
        }
        
        // Show current theme
        shell_print("Current Theme: ");
        extern int gui_get_current_theme(void);
        int current_theme = gui_get_current_theme();
        
        switch (current_theme) {
            case 0: shell_println("Classic"); break;
            case 1: shell_println("Modern"); break;
            case 2: shell_println("Dark"); break;
            case 3: shell_println("Light"); break;
            default: shell_println("Unknown"); break;
        }
    }
    else if (strcmp(argv[1], "shutdown") == 0) {
        // Shut down GUI subsystem
        shell_println("Shutting down GUI subsystem...");
        
        // First shut down window manager
        window_manager_shutdown();
        
        // Then shut down graphics
        graphics_shutdown();
        
        shell_println("GUI subsystem shut down successfully.");
    }
    else {
        shell_println("Unknown GUI command. Try 'gui' for help.");
    }
}

// Kernel panic test command implementation
void cmd_panic(int argc, char *argv[]) {
    #include "panic.h"
    
    if (argc < 2) {
        shell_println("Usage: panic <type>");
        shell_println("Types:");
        shell_println("  general       - General unspecified error");
        shell_println("  memory        - Memory corruption");
        shell_println("  pagefault     - Page fault");
        shell_println("  doublefault   - Double fault");
        shell_println("  stackoverflow - Stack overflow");
        shell_println("  divzero       - Division by zero");
        shell_println("  assert        - Assertion failure");
        shell_println("  hardware      - Hardware failure");
        shell_println("  driver        - Driver error");
        shell_println("  irq           - Unexpected interrupt");
        shell_println("  fs            - Filesystem error");
        shell_println("");
        shell_println("WARNING: This command will intentionally crash the system.");
        shell_println("It is intended for testing the kernel panic handler.");
        return;
    }
    
    // Confirmation prompt
    shell_println("WARNING: This will cause a kernel panic and halt the system.");
    shell_print("Are you sure you want to continue? (y/N) ");
    
    // Wait for a response
    while (!is_key_available()) {}
    
    char key = keyboard_read_key();
    shell_print(&key);
    shell_println("");
    
    if (key != 'y' && key != 'Y') {
        shell_println("Panic test aborted.");
        return;
    }
    
    // Small delay to allow messages to be seen
    for (volatile int i = 0; i < 500000; i++) {}
    
    // Determine which type of panic to trigger
    const char* type = argv[1];
    
    if (strcmp(type, "general") == 0) {
        PANIC(PANIC_GENERAL, "Manual panic triggered for testing");
    }
    else if (strcmp(type, "memory") == 0) {
        PANIC(PANIC_MEMORY_CORRUPTION, "Simulating memory corruption for testing");
    }
    else if (strcmp(type, "pagefault") == 0) {
        PANIC(PANIC_PAGE_FAULT, "Simulating a page fault for testing");
    }
    else if (strcmp(type, "doublefault") == 0) {
        PANIC(PANIC_DOUBLE_FAULT, "Simulating a double fault for testing");
    }
    else if (strcmp(type, "stackoverflow") == 0) {
        PANIC(PANIC_STACK_OVERFLOW, "Simulating a stack overflow for testing");
    }
    else if (strcmp(type, "divzero") == 0) {
        // Actually cause a division by zero
        shell_println("Causing a real division by zero...");
        for (volatile int i = 0; i < 100000; i++) {} // Delay
        int x = 0;
        int y = 100 / x; // This will cause a real division by zero
        shell_println("This should not be printed");
    }
    else if (strcmp(type, "assert") == 0) {
        // Trigger an assertion failure
        shell_println("Triggering assertion failure...");
        ASSERT(0 == 1);
    }
    else if (strcmp(type, "hardware") == 0) {
        PANIC(PANIC_HARDWARE_FAILURE, "Simulating a hardware failure for testing");
    }
    else if (strcmp(type, "driver") == 0) {
        PANIC(PANIC_DRIVER_ERROR, "Simulating a driver error for testing");
    }
    else if (strcmp(type, "irq") == 0) {
        PANIC(PANIC_UNEXPECTED_IRQ, "Simulating an unexpected interrupt for testing");
    }
    else if (strcmp(type, "fs") == 0) {
        PANIC(PANIC_FS_ERROR, "Simulating a filesystem error for testing");
    }
    else {
        shell_println("Unknown panic type. Run 'panic' without arguments for usage.");
    }
}

/**
 * Control preemptive multitasking system
 * Usage: preempt [enable|disable|status]
 */
void cmd_preempt(int argc, char *argv[]) {
    if (argc < 2) {
        // Just show status if no arguments
        shell_println("=== Preemptive Multitasking Control ===");
        shell_print("Current status: ");
        if (is_preemption_enabled()) {
            shell_println("ENABLED");
        } else {
            shell_println("DISABLED");
        }
        shell_println("\nUsage: preempt [enable|disable|status|stats|reset]");
        shell_println("  enable  - Enable preemptive task switching");
        shell_println("  disable - Disable preemptive task switching");
        shell_println("  status  - Show current preemptive multitasking status");
        shell_println("  stats   - Show detailed preemption statistics");
        shell_println("  reset   - Reset preemption statistics");
        return;
    }

    if (strcmp(argv[1], "enable") == 0) {
        shell_println("Enabling preemptive multitasking...");
        enable_preemption();
        shell_println("Preemptive multitasking is now enabled.");
    } else if (strcmp(argv[1], "disable") == 0) {
        shell_println("Disabling preemptive multitasking...");
        disable_preemption();
        shell_println("Preemptive multitasking is now disabled. Tasks must yield manually.");
    } else if (strcmp(argv[1], "status") == 0) {
        shell_print("Preemptive multitasking is currently: ");
        if (is_preemption_enabled()) {
            shell_println("ENABLED");
        } else {
            shell_println("DISABLED");
        }
        
        // Display timer information
        char buffer[32];
        shell_print("System ticks: ");
        int_to_string((int)get_preemption_ticks(), buffer);
        shell_println(buffer);
    } else if (strcmp(argv[1], "stats") == 0) {
        // Display detailed statistics
        shell_println("=== Preemptive Multitasking Statistics ===");
        
        // Get statistics
        uint64_t involuntary = 0, voluntary = 0, timer_ints = 0, disabled_time = 0;
        get_preemption_stats(&involuntary, &voluntary, &timer_ints, &disabled_time);
        
        char buffer[32];
        
        // Display statistics
        shell_print("Timer interrupts: ");
        int_to_string((int)timer_ints, buffer);
        shell_println(buffer);
        
        shell_print("Involuntary task switches: ");
        int_to_string((int)involuntary, buffer);
        shell_println(buffer);
        
        shell_print("Voluntary task switches: ");
        int_to_string((int)voluntary, buffer);
        shell_println(buffer);
        
        shell_print("Total task switches: ");
        int_to_string((int)(involuntary + voluntary), buffer);
        shell_println(buffer);
        
        shell_print("Time spent with preemption disabled: ");
        int_to_string((int)disabled_time, buffer);
        shell_print(buffer);
        shell_println(" ticks");
        
        // Calculate percentages
        if (timer_ints > 0) {
            int preemption_disabled_percent = (disabled_time * 100) / timer_ints;
            shell_print("Percentage of time with preemption disabled: ");
            int_to_string(preemption_disabled_percent, buffer);
            shell_print(buffer);
            shell_println("%");
        }
        
        // Show system info
        shell_print("Current task count: ");
        int_to_string(get_task_count(), buffer);
        shell_println(buffer);
        
        shell_print("Current task ID: ");
        int_to_string(get_current_task_id(), buffer);
        shell_println(buffer);
    } else if (strcmp(argv[1], "reset") == 0) {
        reset_preemption_stats();
        shell_println("Preemption statistics have been reset.");
    } else {
        shell_println("Invalid argument. Use 'enable', 'disable', 'status', 'stats', or 'reset'.");
    }
}

/**
 * Run the multitasking demo
 */
void cmd_taskdemo(int argc, char *argv[]) {
    log_debug("SHELL", "Starting multitasking demo");
    
    shell_println("Starting multitasking demonstration...");
    shell_println("This will show two tasks running concurrently.");
    shell_println("Use the 'preempt' command beforehand to enable/disable preemption.");
    shell_println("Press any key to exit the demo when it's running.");
    shell_println("Starting in 3 seconds...");
    
    // Small delay before starting demo
    for (volatile int i = 0; i < 3000000; i++);
    
    // Include the task demo header
    #include "task_demo.h"
    
    // Run the demo
    start_multitasking_demo();
    
    shell_println("Multitasking demonstration completed.");
}