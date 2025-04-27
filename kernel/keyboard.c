#include "keyboard.h"
#include "irq.h"
#include "io.h"

// Circular buffer for keyboard input
#define KEYBOARD_BUFFER_SIZE 128
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static volatile int buffer_full = 0;  // Flag to track buffer full state

// Map from scan code to ASCII character (US layout)
const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

// Uppercase version for shift key
const char scancode_to_ascii_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

// Keyboard state
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int caps_lock = 0;

// Handler for keyboard IRQ
void keyboard_handler() {
    unsigned char status = inb(KEYBOARD_STATUS_PORT);
    
    if (status & KEYBOARD_OUTPUT_BUFFER_FULL) {
        unsigned char scancode = inb(KEYBOARD_DATA_PORT);
        
        // Track modifier keys
        if (scancode == 0x2A || scancode == 0x36) {           // Shift press
            shift_pressed = 1;
        } else if (scancode == 0xAA || scancode == 0xB6) {    // Shift release
            shift_pressed = 0;
        } else if (scancode == 0x1D) {                        // Ctrl press
            ctrl_pressed = 1; 
        } else if (scancode == 0x9D) {                        // Ctrl release
            ctrl_pressed = 0;
        } else if (scancode == 0x38) {                        // Alt press
            alt_pressed = 1;
        } else if (scancode == 0xB8) {                        // Alt release
            alt_pressed = 0;
        } else if (scancode == 0x3A) {                        // Caps Lock toggle
            caps_lock = !caps_lock;
        } else if (scancode < 0x80) {  // Key press (not release)
            // Determine character based on modifiers
            char ascii = 0;
            
            if (shift_pressed || caps_lock) {
                // Consider caps lock only for letters
                if (caps_lock && !shift_pressed && 
                    scancode >= 0x10 && scancode <= 0x32 && 
                    (scancode <= 0x19 || scancode >= 0x1E)) {
                    // Apply caps lock for letters only
                    ascii = scancode_to_ascii_shift[scancode];
                } else if (shift_pressed) {
                    // Apply shift modifier
                    ascii = scancode_to_ascii_shift[scancode];
                } else {
                    // Regular character
                    ascii = scancode_to_ascii[scancode];
                }
            } else {
                // Regular unmodified key
                ascii = scancode_to_ascii[scancode];
            }
            
            // Handle Ctrl+C (SIGINT equivalent)
            if (ctrl_pressed && (ascii == 'c' || ascii == 'C')) {
                // Add special handling if needed
                // For now, just add it to the buffer as 0x03 (ETX)
                ascii = 0x03; 
            }
            
            if (ascii) {
                // Store character in circular buffer with full detection
                int next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
                
                if (next_head != buffer_tail) {  // Buffer not full
                    keyboard_buffer[buffer_head] = ascii;
                    buffer_head = next_head;
                    buffer_full = (buffer_head == buffer_tail);
                }
                // If buffer is full, we simply drop the keypress
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
    
    // Clear the keyboard buffer and state
    buffer_head = buffer_tail = 0;
    buffer_full = 0;
    shift_pressed = ctrl_pressed = alt_pressed = caps_lock = 0;
}

// Check if there's a key available in the buffer
int is_key_available() {
    return (buffer_head != buffer_tail) || buffer_full;
}

// Read a key from the keyboard buffer
char keyboard_read_key() {
    if (is_key_available()) {
        char key = keyboard_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
        buffer_full = 0;  // Buffer can't be full after reading
        return key;
    }
    return 0;
}

// Wait for a keypress and return it (blocking)
char keyboard_wait_key() {
    while (!is_key_available()) {
        // Wait for a key to be available
        // In a real OS, we would use a proper wait mechanism
        // or yield to other tasks here
    }
    return keyboard_read_key();
}

// Flush the keyboard buffer (clear all pending keypresses)
void keyboard_flush() {
    buffer_head = buffer_tail = 0;
    buffer_full = 0;
}