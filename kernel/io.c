#include "io.h"
#include "gdt.h"
#include "asm.h"
#include "task.h"

void uintos_display_character(char character, char attribute) {
    UINTOS_SET_ES(0x20);
    // Write the character and attribute to memory for display.
    UINTOS_WRITE_MEM_ES(0x0, (attribute << 8) | character);
}

void shell() {
    char command[256];

    while (1) {
        print("uintOS> ");
        read_line(command, sizeof(command));

        if (strcmp(command, "help") == 0) {
            print("Available commands:\n");
            print("  help - Show this help message\n");
            print("  tasks - List running tasks\n");
        } else if (strcmp(command, "tasks") == 0) {
            print("Listing tasks:\n");
            for (int i = 0; i < num_tasks; i++) {
                print("  Task ");
                print_int(i);
                print("\n");
            }
        } else {
            print("Unknown command. Type 'help' for a list of commands.\n");
        }
    }
}
