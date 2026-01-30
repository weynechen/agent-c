/**
 * @file log_freertos.c
 * @brief FreeRTOS platform logging implementation
 *
 * Outputs logs to serial port or custom output buffer.
 * Minimal formatting to conserve memory on embedded systems.
 */

#include "arc/log.h"
#include <stdio.h>
#include <string.h>

/*
 * Note: FreeRTOS systems typically need to implement their own
 * serial output functions. This is a template that can be customized.
 */

/**
 * @brief Weak symbol for platform-specific serial output
 *
 * Users should implement this function to output to their specific
 * serial port or logging mechanism.
 */
__attribute__((weak)) void platform_serial_write(const char* str, size_t len) {
    // Default implementation: output to stdout (if available)
    // Replace with your serial port implementation
    fwrite(str, 1, len, stdout);
    fflush(stdout);
}

/**
 * @brief Get log level character
 */
static char get_level_char(ac_log_level_t level) {
    switch (level) {
        case AC_LOG_LEVEL_ERROR: return 'E';
        case AC_LOG_LEVEL_WARN:  return 'W';
        case AC_LOG_LEVEL_INFO:  return 'I';
        case AC_LOG_LEVEL_DEBUG: return 'D';
        default: return '?';
    }
}

/**
 * @brief Extract basename from file path
 */
static const char* get_basename(const char* path) {
    const char* base = strrchr(path, '/');
    return base ? base + 1 : path;
}

/**
 * @brief Platform default log handler for FreeRTOS/embedded systems
 */
void ac_log_platform_default_handler(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
) {
    char buffer[256];  // Adjust size based on available memory
    int len = 0;

    // Compact format: [L] message (file:line)
    // Example: [E] Connection failed (http.c:123)

    len = snprintf(buffer, sizeof(buffer), "[%c] ", get_level_char(level));

    if (len > 0 && len < (int)sizeof(buffer)) {
        int msg_len = vsnprintf(buffer + len, sizeof(buffer) - len, fmt, args);
        if (msg_len > 0) {
            len += msg_len;
        }
    }

    if (len > 0 && len < (int)sizeof(buffer)) {
        int loc_len = snprintf(buffer + len, sizeof(buffer) - len,
                               " (%s:%d)\n", get_basename(file), line);
        if (loc_len > 0) {
            len += loc_len;
        }
    }

    // Ensure null termination
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
        buffer[len] = '\0';
    }

    // Output to serial port
    platform_serial_write(buffer, len);
}
