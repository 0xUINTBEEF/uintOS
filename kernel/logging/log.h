#ifndef UINTOS_LOG_H
#define UINTOS_LOG_H

#include <inttypes.h>

/* Log Levels - from least to most severe */
typedef enum {
    LOG_LEVEL_TRACE = 0,    // Detailed tracing information
    LOG_LEVEL_DEBUG = 1,    // Debugging information
    LOG_LEVEL_INFO = 2,     // General information
    LOG_LEVEL_NOTICE = 3,   // Normal but significant events
    LOG_LEVEL_WARNING = 4,  // Potential issues
    LOG_LEVEL_ERROR = 5,    // Error conditions
    LOG_LEVEL_CRITICAL = 6, // Critical conditions
    LOG_LEVEL_ALERT = 7,    // Action must be taken immediately
    LOG_LEVEL_EMERGENCY = 8 // System is unusable
} log_level_t;

/* Log Output Destinations */
#define LOG_DEST_MEMORY  0x01  // Store logs in memory buffer
#define LOG_DEST_SCREEN  0x02  // Output logs to screen
#define LOG_DEST_SERIAL  0x04  // Output logs to serial port
#define LOG_DEST_ALL     0xFF  // Output logs everywhere

/* Log Formatting Options */
#define LOG_FORMAT_TIMESTAMP  0x01  // Include timestamp
#define LOG_FORMAT_LEVEL      0x02  // Include log level
#define LOG_FORMAT_SOURCE     0x04  // Include source info
#define LOG_FORMAT_FULL       0xFF  // Include all formatting options

/* Maximum log buffer size */
#define LOG_BUFFER_SIZE       16384  // 16KB log buffer

/* Maximum log message length */
#define LOG_MAX_MESSAGE_SIZE  256

/* Log colors for different levels */
#define LOG_COLOR_TRACE       0x07  // White on black
#define LOG_COLOR_DEBUG       0x0B  // Light cyan on black
#define LOG_COLOR_INFO        0x0A  // Light green on black
#define LOG_COLOR_NOTICE      0x0E  // Yellow on black
#define LOG_COLOR_WARNING     0x0E  // Yellow on black
#define LOG_COLOR_ERROR       0x0C  // Light red on black
#define LOG_COLOR_CRITICAL    0x4F  // White on red
#define LOG_COLOR_ALERT       0x5F  // White on magenta
#define LOG_COLOR_EMERGENCY   0xCF  // White on light red

/* Function definitions */

/**
 * Initialize the logging subsystem
 *
 * @param log_level Minimum level of messages to log
 * @param destination Bit mask of LOG_DEST_* flags
 * @param format_options Bit mask of LOG_FORMAT_* flags
 * @return 0 on success, -1 on failure
 */
int log_init(log_level_t log_level, uint8_t destinations, uint8_t format_options);

/**
 * Log a message with specified log level
 *
 * @param level Log level for this message
 * @param source Source of the message (module or file)
 * @param format printf-style format string
 * @param ... Additional arguments for format string
 */
void log_message(log_level_t level, const char* source, const char* format, ...);

/**
 * Convenience macro for trace-level messages
 */
#define log_trace(source, format, ...) log_message(LOG_LEVEL_TRACE, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for debug-level messages
 */
#define log_debug(source, format, ...) log_message(LOG_LEVEL_DEBUG, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for info-level messages
 */
#define log_info(source, format, ...) log_message(LOG_LEVEL_INFO, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for notice-level messages
 */
#define log_notice(source, format, ...) log_message(LOG_LEVEL_NOTICE, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for warning-level messages
 */
#define log_warning(source, format, ...) log_message(LOG_LEVEL_WARNING, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for error-level messages
 */
#define log_error(source, format, ...) log_message(LOG_LEVEL_ERROR, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for critical-level messages
 */
#define log_critical(source, format, ...) log_message(LOG_LEVEL_CRITICAL, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for alert-level messages
 */
#define log_alert(source, format, ...) log_message(LOG_LEVEL_ALERT, source, format, ##__VA_ARGS__)

/**
 * Convenience macro for emergency-level messages
 */
#define log_emergency(source, format, ...) log_message(LOG_LEVEL_EMERGENCY, source, format, ##__VA_ARGS__)

/**
 * Set the minimum log level
 *
 * @param level New minimum log level
 */
void log_set_level(log_level_t level);

/**
 * Set the log destinations
 *
 * @param destinations Bit mask of LOG_DEST_* flags
 */
void log_set_destinations(uint8_t destinations);

/**
 * Set the log format options
 *
 * @param format_options Bit mask of LOG_FORMAT_* flags
 */
void log_set_format_options(uint8_t format_options);

/**
 * Retrieve logs from memory buffer
 *
 * @param buffer Destination buffer to copy logs to
 * @param max_size Maximum size of the buffer
 * @return Number of bytes copied
 */
uint32_t log_get_buffer(char* buffer, uint32_t max_size);

/**
 * Clear the log buffer
 */
void log_clear_buffer(void);

/**
 * Dump log buffer to screen
 */
void log_dump_buffer(void);

/**
 * Get a string representation of a log level
 *
 * @param level Log level
 * @return String representation of the log level
 */
const char* log_level_to_string(log_level_t level);

/**
 * Get VGA color attribute for a log level
 *
 * @param level Log level
 * @return VGA color attribute
 */
uint8_t log_level_to_color(log_level_t level);

#endif /* UINTOS_LOG_H */