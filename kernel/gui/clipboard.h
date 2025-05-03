/**
 * @file clipboard.h
 * @brief Simple clipboard system for GUI framework
 */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdint.h>

// Maximum size of clipboard content
#define CLIPBOARD_MAX_SIZE 4096

// Clipboard content types
typedef enum {
    CLIPBOARD_TYPE_NONE,
    CLIPBOARD_TYPE_TEXT,
    CLIPBOARD_TYPE_BINARY
} clipboard_content_type_t;

/**
 * Initialize the clipboard system
 */
void clipboard_init(void);

/**
 * Copy text to the clipboard
 * 
 * @param text Text to copy
 * @param length Length of text
 * @return 1 if successful, 0 if failed
 */
int clipboard_set_text(const char* text, uint32_t length);

/**
 * Get text from the clipboard
 * 
 * @return Clipboard text or NULL if clipboard is empty or doesn't contain text
 */
const char* clipboard_get_text(void);

/**
 * Get the length of text in the clipboard
 * 
 * @return Length of clipboard text, or 0 if clipboard is empty
 */
uint32_t clipboard_get_text_length(void);

/**
 * Copy binary data to the clipboard
 * 
 * @param data Binary data to copy
 * @param length Length of data
 * @return 1 if successful, 0 if failed
 */
int clipboard_set_binary(const void* data, uint32_t length);

/**
 * Get binary data from the clipboard
 * 
 * @return Pointer to clipboard data or NULL if clipboard is empty
 */
const void* clipboard_get_binary(void);

/**
 * Get the length of binary data in the clipboard
 * 
 * @return Length of clipboard data, or 0 if clipboard is empty
 */
uint32_t clipboard_get_binary_length(void);

/**
 * Get the type of content currently in the clipboard
 * 
 * @return Clipboard content type
 */
clipboard_content_type_t clipboard_get_type(void);

/**
 * Clear the clipboard
 */
void clipboard_clear(void);

#endif /* CLIPBOARD_H */