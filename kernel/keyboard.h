#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "io.h"
#include "irq.h"

// Keyboard controller ports
#define KEYBOARD_DATA_PORT       0x60
#define KEYBOARD_STATUS_PORT     0x64
#define KEYBOARD_COMMAND_PORT    0x64

// Keyboard status
#define KEYBOARD_OUTPUT_BUFFER_FULL  0x01

// Special key scan codes
#define KEY_SHIFT_LEFT   0x2A
#define KEY_SHIFT_RIGHT  0x36
#define KEY_CTRL         0x1D
#define KEY_ALT          0x38
#define KEY_CAPS_LOCK    0x3A

// Scan code mapping for standard US QWERTY keyboard
extern const char scancode_to_ascii[];
extern const char scancode_to_ascii_shift[];

// Function declarations
void keyboard_init(void);
char keyboard_read_key(void);
char keyboard_wait_key(void);
void keyboard_handler(void);
int is_key_available(void);
void keyboard_flush(void);

#endif // KEYBOARD_H