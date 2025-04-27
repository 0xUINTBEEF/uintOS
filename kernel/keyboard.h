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

// Scan code mapping for standard TR QWERTY keyboard
extern const char scancode_to_ascii[];

// Function declarations
void keyboard_init(void);
char keyboard_read_key(void);
void keyboard_handler(void);
int is_key_available(void);

#endif // KEYBOARD_H