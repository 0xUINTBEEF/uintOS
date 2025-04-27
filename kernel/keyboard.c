#include "keyboard.h"
#include "irq.h"
#include "io.h"

// Circular buffer for keyboard input
#define KEYBOARD_BUFFER_SIZE 128
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int buffer_head = 0;
static int buffer_tail = 0;

// Map from scan code to ASCII character (US layout)
const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

// Handler for keyboard IRQ
void keyboard_handler() {
    unsigned char status = inb(KEYBOARD_STATUS_PORT);
    
    if (status & KEYBOARD_OUTPUT_BUFFER_FULL) {
        unsigned char scancode = inb(KEYBOARD_DATA_PORT);
        
        // Process only key press events (ignore key release)
        if (scancode < 0x80) {
            char ascii = scancode_to_ascii[scancode];
            
            if (ascii) {
                // Store in circular buffer
                int next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
                if (next_head != buffer_tail) {  // Check if buffer is full
                    keyboard_buffer[buffer_head] = ascii;
                    buffer_head = next_head;
                }
            }
        }
    }
    
    // Send EOI (End of Interrupt) signal
    outb(0x20, 0x20);
}

// Initialize keyboard
void keyboard_init() {
    // Register keyboard handler for IRQ 1
    register_interrupt_handler(33, keyboard_handler);
    
    // Clear the keyboard buffer
    buffer_head = buffer_tail = 0;
}

// Check if there's a key available in the buffer
int is_key_available() {
    return buffer_head != buffer_tail;
}

// Read a key from the keyboard buffer
char keyboard_read_key() {
    if (is_key_available()) {
        char key = keyboard_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
        return key;
    }
    return 0;
}