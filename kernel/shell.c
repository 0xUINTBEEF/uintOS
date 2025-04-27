#include "shell.h"
#include "keyboard.h"
#include "io.h"
#include "../memory/paging.h"
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

// Shell I/O functions
void shell_print(const char *str) {
    while (*str) {
        // Simple implementation: write the character to the screen
        // In a real implementation, this would use a terminal driver
        display_character(*str, 15); // Assuming display_character is defined elsewhere
        str++;
    }
}

void shell_println(const char *str) {
    shell_print(str);
    shell_print("\n");
}

void shell_display_prompt() {
    shell_print(COLOR_GREEN "uintOS" COLOR_RESET ":" COLOR_BLUE "~$ " COLOR_RESET);
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
    shell_println("  meminfo  - Display memory information");
    shell_println("  taskinfo - Display task information");
    shell_println("  reboot   - Reboot the system");
}

void cmd_clear(int argc, char *argv[]) {
    // Simple implementation: fill screen with spaces
    for (int i = 0; i < 25 * 80; i++) {
        display_character(' ', 0);
    }
    // Reset cursor position
    // In a real implementation, we would have proper cursor management
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
    // Display task information
    shell_println("Task Information:");
    shell_println("  Number of Tasks: 2");
    shell_println("  Current Task: Kernel");
    // You would get actual task information here
}

void cmd_reboot(int argc, char *argv[]) {
    shell_println("Rebooting system...");
    // In a real implementation, this would trigger a system reboot
    // For example: outb(0x64, 0xFE);
}