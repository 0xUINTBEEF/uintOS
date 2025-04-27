#include "shell.h"
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "../memory/paging.h"
#include "../filesystem/fat12.h"
#include "../memory/heap.h"
#include "task.h"

// ANSI color codes
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_BLUE "\033[34m"

// Buffer for the current command
static char command_buffer[MAX_COMMAND_LENGTH];
static int buffer_position = 0;

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

// Process a key input in the shell
static void process_key(char key) {
    if (key == '\n') {
        // Enter key - execute command
        shell_println("");
        command_buffer[buffer_position] = 0; // Null terminate
        if (buffer_position > 0) {
            shell_execute_command(command_buffer);
        }
        buffer_position = 0;
        shell_display_prompt();
    } else if (key == '\b') {
        // Backspace - delete a character
        if (buffer_position > 0) {
            buffer_position--;
            shell_print("\b \b"); // Erase character on screen
        }
    } else if (buffer_position < MAX_COMMAND_LENGTH - 1) {
        // Regular character - add to buffer
        command_buffer[buffer_position++] = key;
        display_character(key, 15); // Echo character
    } else {
        // Buffer is full, alert the user
        display_character('\a', 15); // Bell character as alert
    }
}

// Main shell loop
void shell_run() {
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
        } else {
            shell_println("Unknown command. Type 'help' for a list of commands.");
        }
    }
}

// Initialize the shell
void shell_init() {
    buffer_position = 0;
}

// Command implementations
void cmd_help(int argc, char *argv[]) {
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
    shell_println("  reboot   - Reboot the system");
    shell_println("  vgademo  - Run VGA demonstration");
}

void cmd_clear(int argc, char *argv[]) {
    // Use the VGA driver to clear the screen
    vga_clear_screen();
}

void cmd_echo(int argc, char *argv[]) {
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