#include "log.h"
#include "../vga.h"
#include "../io.h"
#include "../sync.h" // Include synchronization primitives
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* Log system state */
static log_level_t current_log_level = LOG_LEVEL_INFO;
static uint8_t log_destinations = LOG_DEST_SCREEN | LOG_DEST_MEMORY;
static uint8_t log_format_options = LOG_FORMAT_LEVEL | LOG_FORMAT_SOURCE;
static uint32_t log_timestamp = 0;
static mutex_t log_mutex; // Mutex for thread-safe logging

/* Log buffer */
static char log_buffer[LOG_BUFFER_SIZE];
static uint32_t log_buffer_position = 0;

/* Log level strings */
static const char* log_level_strings[] = {
    "TRACE", "DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "CRITICAL", "ALERT", "EMERGENCY"
};

/* Utility functions to convert numbers to strings */
static void uitoa(unsigned int value, char* str, int base) {
    const char* digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char buf[33];
    char* ptr = &buf[sizeof(buf) - 1];
    *ptr = '\0';
    
    do {
        *--ptr = digits[value % base];
        value /= base;
    } while (value != 0);
    
    strcpy(str, ptr);
}

static char* format_timestamp(uint32_t timestamp, char* buffer) {
    uint32_t seconds = timestamp / 100;
    uint32_t centiseconds = timestamp % 100;
    
    uitoa(seconds, buffer, 10);
    strcat(buffer, ".");
    
    char cs_str[3];
    uitoa(centiseconds, cs_str, 10);
    
    // Pad with leading zero if needed
    if (centiseconds < 10) {
        strcat(buffer, "0");
    }
    
    strcat(buffer, cs_str);
    
    return buffer;
}

// Get current system time in timer ticks (100Hz)
static uint32_t get_system_time() {
    // Use the system's hardware timer
    // This implementation uses a proper system timer instead of a simple counter
    if (timer_get_ticks_available()) {
        // Return the actual system timer value if available
        return timer_get_ticks();
    } else {
        // Fall back to the simpler counter if the timer isn't initialized yet
        return log_timestamp;
    }
}

int log_init(log_level_t log_level, uint8_t destinations, uint8_t format_options) {
    current_log_level = log_level;
    log_destinations = destinations;
    log_format_options = format_options;
    
    // Initialize the mutex for thread safety
    mutex_init(&log_mutex);
    
    // Clear log buffer
    memset(log_buffer, 0, LOG_BUFFER_SIZE);
    log_buffer_position = 0;
    
    // Log the initialization as the first message
    log_info("LOG", "Logging system initialized (level=%s)", log_level_to_string(log_level));
    
    return 0;
}

void log_message(log_level_t level, const char* source, const char* format, ...) {
    if (level < current_log_level) {
        return; // Skip this message if below the current log level
    }
    
    // Acquire the mutex for thread safety
    mutex_lock(&log_mutex);
    
    char message[LOG_MAX_MESSAGE_SIZE];
    va_list args;
    
    // Format message with variable arguments
    va_start(args, format);
    vsnprintf(message, LOG_MAX_MESSAGE_SIZE, format, args);
    va_end(args);
    
    char full_message[LOG_MAX_MESSAGE_SIZE];
    char timestamp_str[12];
    
    // Start with empty string
    full_message[0] = '\0';
    
    // Add timestamp if enabled
    if (log_format_options & LOG_FORMAT_TIMESTAMP) {
        format_timestamp(get_system_time(), timestamp_str);
        strcat(full_message, "[");
        strcat(full_message, timestamp_str);
        strcat(full_message, "] ");
    }
    
    // Add log level if enabled
    if (log_format_options & LOG_FORMAT_LEVEL) {
        strcat(full_message, "[");
        strcat(full_message, log_level_to_string(level));
        strcat(full_message, "] ");
    }
    
    // Add source if enabled
    if (log_format_options & LOG_FORMAT_SOURCE && source != NULL) {
        strcat(full_message, "[");
        strcat(full_message, source);
        strcat(full_message, "] ");
    }
    
    // Add the actual message
    strcat(full_message, message);
    
    // Output to destinations
    
    // Output to screen if enabled
    if (log_destinations & LOG_DEST_SCREEN) {
        uint8_t old_color = vga_current_color;
        vga_set_color(log_level_to_color(level));
        vga_write_string(full_message);
        vga_write_string("\n");
        vga_set_color(old_color);
    }
    
    // Store in memory buffer if enabled
    if (log_destinations & LOG_DEST_MEMORY && log_buffer_position < LOG_BUFFER_SIZE) {
        uint32_t message_length = strlen(full_message);
        
        // Check if we have enough space
        if (log_buffer_position + message_length + 2 < LOG_BUFFER_SIZE) {
            // Copy the message to the buffer
            strcpy(&log_buffer[log_buffer_position], full_message);
            log_buffer_position += message_length;
            
            // Add a newline character
            log_buffer[log_buffer_position++] = '\n';
            log_buffer[log_buffer_position] = '\0';
        } else {
            // Buffer is full, implement circular buffer behavior
            // Move old messages to make room for new message
            uint32_t move_size = log_buffer_position - (LOG_BUFFER_SIZE / 2);
            memmove(log_buffer, log_buffer + move_size, LOG_BUFFER_SIZE - move_size);
            log_buffer_position -= move_size;
            
            // Add the warning about lost messages
            const char* overflow_msg = "[LOGGING] Log buffer overflow, oldest messages lost\n";
            strcpy(&log_buffer[log_buffer_position], overflow_msg);
            log_buffer_position += strlen(overflow_msg);
            
            // Now add the new message
            strcpy(&log_buffer[log_buffer_position], full_message);
            log_buffer_position += message_length;
            log_buffer[log_buffer_position++] = '\n';
            log_buffer[log_buffer_position] = '\0';
        }
    }
    
    // Output to serial port if enabled
    if (log_destinations & LOG_DEST_SERIAL) {
        // Write to COM1 port (0x3F8)
        const char* ptr = full_message;
        while(*ptr) {
            outb(0x3F8, *ptr++);
        }
        outb(0x3F8, '\r');
        outb(0x3F8, '\n');
    }
    
    // Update the timestamp counter
    log_timestamp++;
    
    // Release the mutex
    mutex_unlock(&log_mutex);
}

void log_set_level(log_level_t level) {
    if (level <= LOG_LEVEL_EMERGENCY) {
        mutex_lock(&log_mutex);
        log_debug("LOG", "Changing log level from %s to %s", 
                 log_level_to_string(current_log_level),
                 log_level_to_string(level));
        current_log_level = level;
        mutex_unlock(&log_mutex);
    }
}

void log_set_destinations(uint8_t destinations) {
    mutex_lock(&log_mutex);
    log_debug("LOG", "Changing log destinations from 0x%x to 0x%x", 
             log_destinations, destinations);
    log_destinations = destinations;
    mutex_unlock(&log_mutex);
}

void log_set_format_options(uint8_t format_options) {
    mutex_lock(&log_mutex);
    log_debug("LOG", "Changing log format options from 0x%x to 0x%x", 
             log_format_options, format_options);
    log_format_options = format_options;
    mutex_unlock(&log_mutex);
}

uint32_t log_get_buffer(char* buffer, uint32_t max_size) {
    if (buffer == NULL || max_size == 0) {
        return 0;
    }
    
    mutex_lock(&log_mutex);
    uint32_t bytes_to_copy = (log_buffer_position < max_size) ? 
                             log_buffer_position : max_size - 1;
    
    memcpy(buffer, log_buffer, bytes_to_copy);
    buffer[bytes_to_copy] = '\0'; // Ensure null termination
    mutex_unlock(&log_mutex);
    
    return bytes_to_copy;
}

void log_clear_buffer(void) {
    mutex_lock(&log_mutex);
    log_debug("LOG", "Clearing log buffer");
    memset(log_buffer, 0, LOG_BUFFER_SIZE);
    log_buffer_position = 0;
    mutex_unlock(&log_mutex);
}

void log_dump_buffer(void) {
    mutex_lock(&log_mutex);
    uint8_t old_color = vga_current_color;
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    
    vga_write_string("\n--- LOG BUFFER DUMP ---\n");
    vga_write_string(log_buffer);
    vga_write_string("--- END OF LOG BUFFER ---\n");
    
    vga_set_color(old_color);
    mutex_unlock(&log_mutex);
}

const char* log_level_to_string(log_level_t level) {
    if (level <= LOG_LEVEL_EMERGENCY) {
        return log_level_strings[level];
    }
    return "UNKNOWN";
}

uint8_t log_level_to_color(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_TRACE:     return LOG_COLOR_TRACE;
        case LOG_LEVEL_DEBUG:     return LOG_COLOR_DEBUG;
        case LOG_LEVEL_INFO:      return LOG_COLOR_INFO;
        case LOG_LEVEL_NOTICE:    return LOG_COLOR_NOTICE;
        case LOG_LEVEL_WARNING:   return LOG_COLOR_WARNING;
        case LOG_LEVEL_ERROR:     return LOG_COLOR_ERROR;
        case LOG_LEVEL_CRITICAL:  return LOG_COLOR_CRITICAL;
        case LOG_LEVEL_ALERT:     return LOG_COLOR_ALERT;
        case LOG_LEVEL_EMERGENCY: return LOG_COLOR_EMERGENCY;
        default:                  return LOG_COLOR_INFO;
    }
}