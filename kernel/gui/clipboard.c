/**
 * @file clipboard.c
 * @brief Simple clipboard system implementation
 */
#include <string.h>
#include "clipboard.h"
#include "../logging/log.h"
#include "../../hal/include/hal_memory.h"

// Clipboard internal state
static struct {
    clipboard_content_type_t type;     // Type of content
    void* data;                         // Clipboard data
    uint32_t size;                      // Size of data
    uint32_t capacity;                  // Allocated buffer capacity
} clipboard = {0};

/**
 * Initialize the clipboard system
 */
void clipboard_init(void) {
    // Start with an empty clipboard
    clipboard.type = CLIPBOARD_TYPE_NONE;
    clipboard.data = NULL;
    clipboard.size = 0;
    clipboard.capacity = 0;
    
    log_debug("CLIPBOARD", "Clipboard system initialized");
}

/**
 * Internal function to ensure capacity
 */
static int clipboard_ensure_capacity(uint32_t size) {
    // If we already have enough capacity, no action needed
    if (clipboard.capacity >= size) {
        return 1;
    }
    
    // Cap size to the maximum allowed
    if (size > CLIPBOARD_MAX_SIZE) {
        size = CLIPBOARD_MAX_SIZE;
    }
    
    // Allocate or reallocate the buffer
    void* new_buffer;
    if (clipboard.data) {
        // Need to reallocate
        new_buffer = hal_memory_alloc(size);
        if (!new_buffer) {
            log_error("CLIPBOARD", "Failed to allocate clipboard buffer");
            return 0;
        }
        
        // Copy existing data to new buffer
        memcpy(new_buffer, clipboard.data, clipboard.size);
        
        // Free old buffer
        hal_memory_free(clipboard.data);
    } else {
        // First allocation
        new_buffer = hal_memory_alloc(size);
        if (!new_buffer) {
            log_error("CLIPBOARD", "Failed to allocate clipboard buffer");
            return 0;
        }
    }
    
    // Update clipboard state
    clipboard.data = new_buffer;
    clipboard.capacity = size;
    
    return 1;
}

/**
 * Copy text to the clipboard
 */
int clipboard_set_text(const char* text, uint32_t length) {
    if (!text || length == 0) {
        // Clear clipboard if empty text
        clipboard_clear();
        return 1;
    }
    
    // Ensure we have capacity for text and null terminator
    if (!clipboard_ensure_capacity(length + 1)) {
        return 0;
    }
    
    // Copy the text
    memcpy(clipboard.data, text, length);
    
    // Ensure null termination
    ((char*)clipboard.data)[length] = '\0';
    
    // Update clipboard state
    clipboard.type = CLIPBOARD_TYPE_TEXT;
    clipboard.size = length + 1; // Including null terminator
    
    log_debug("CLIPBOARD", "Text copied to clipboard (%u bytes)", length);
    return 1;
}

/**
 * Get text from the clipboard
 */
const char* clipboard_get_text(void) {
    if (clipboard.type != CLIPBOARD_TYPE_TEXT || !clipboard.data) {
        return NULL;
    }
    
    return (const char*)clipboard.data;
}

/**
 * Get the length of text in the clipboard
 */
uint32_t clipboard_get_text_length(void) {
    if (clipboard.type != CLIPBOARD_TYPE_TEXT || !clipboard.data) {
        return 0;
    }
    
    // Return size minus null terminator
    return clipboard.size > 0 ? clipboard.size - 1 : 0;
}

/**
 * Copy binary data to the clipboard
 */
int clipboard_set_binary(const void* data, uint32_t length) {
    if (!data || length == 0) {
        // Clear clipboard if empty data
        clipboard_clear();
        return 1;
    }
    
    // Cap size to the maximum allowed
    if (length > CLIPBOARD_MAX_SIZE) {
        length = CLIPBOARD_MAX_SIZE;
    }
    
    // Ensure we have enough capacity
    if (!clipboard_ensure_capacity(length)) {
        return 0;
    }
    
    // Copy the data
    memcpy(clipboard.data, data, length);
    
    // Update clipboard state
    clipboard.type = CLIPBOARD_TYPE_BINARY;
    clipboard.size = length;
    
    log_debug("CLIPBOARD", "Binary data copied to clipboard (%u bytes)", length);
    return 1;
}

/**
 * Get binary data from the clipboard
 */
const void* clipboard_get_binary(void) {
    if (clipboard.type != CLIPBOARD_TYPE_BINARY || !clipboard.data) {
        return NULL;
    }
    
    return clipboard.data;
}

/**
 * Get the length of binary data in the clipboard
 */
uint32_t clipboard_get_binary_length(void) {
    if (clipboard.type != CLIPBOARD_TYPE_BINARY || !clipboard.data) {
        return 0;
    }
    
    return clipboard.size;
}

/**
 * Get the type of content currently in the clipboard
 */
clipboard_content_type_t clipboard_get_type(void) {
    return clipboard.type;
}

/**
 * Clear the clipboard
 */
void clipboard_clear(void) {
    // Free any allocated memory
    if (clipboard.data) {
        hal_memory_free(clipboard.data);
        clipboard.data = NULL;
    }
    
    // Reset clipboard state
    clipboard.type = CLIPBOARD_TYPE_NONE;
    clipboard.size = 0;
    clipboard.capacity = 0;
    
    log_debug("CLIPBOARD", "Clipboard cleared");
}