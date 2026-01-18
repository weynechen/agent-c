/**
 * @file log.c
 * @brief Core logging implementation
 * 
 * This file implements the logging API defined in log.h.
 * Platform-specific output is delegated to the port layer.
 */

#include "agentc/log.h"
#include <stdio.h>
#include <stdarg.h>

/* Global log level */
static ac_log_level_t g_log_level = AC_LOG_LEVEL_INFO;

/* Custom log handler (NULL = use default platform handler) */
static ac_log_handler_t g_log_handler = NULL;

/* Forward declaration of platform-specific default handler */
void ac_log_platform_default_handler(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
);

void ac_log_set_level(ac_log_level_t level) {
    g_log_level = level;
}

ac_log_level_t ac_log_get_level(void) {
    return g_log_level;
}

void ac_log_set_handler(ac_log_handler_t handler) {
    g_log_handler = handler;
}

/**
 * @brief Internal log function
 */
static void ac_log_internal(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
) {
    // Filter by log level
    if (level > g_log_level) {
        return;
    }

    // Use custom handler if set, otherwise use platform default
    if (g_log_handler) {
        g_log_handler(level, file, line, func, fmt, args);
    } else {
        ac_log_platform_default_handler(level, file, line, func, fmt, args);
    }
}

void ac_log_error(const char* file, int line, const char* func, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ac_log_internal(AC_LOG_LEVEL_ERROR, file, line, func, fmt, args);
    va_end(args);
}

void ac_log_warn(const char* file, int line, const char* func, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ac_log_internal(AC_LOG_LEVEL_WARN, file, line, func, fmt, args);
    va_end(args);
}

void ac_log_info(const char* file, int line, const char* func, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ac_log_internal(AC_LOG_LEVEL_INFO, file, line, func, fmt, args);
    va_end(args);
}

void ac_log_debug(const char* file, int line, const char* func, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ac_log_internal(AC_LOG_LEVEL_DEBUG, file, line, func, fmt, args);
    va_end(args);
}
