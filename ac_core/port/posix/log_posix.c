/**
 * @file log_posix.c
 * @brief POSIX platform logging implementation
 * 
 * Outputs colored logs to stderr with timestamp and level prefix.
 */

#include "agentc/log.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

/**
 * @brief Get log level color
 */
static const char* get_level_color(ac_log_level_t level) {
    switch (level) {
        case AC_LOG_LEVEL_ERROR: return COLOR_RED;
        case AC_LOG_LEVEL_WARN:  return COLOR_YELLOW;
        case AC_LOG_LEVEL_INFO:  return COLOR_GREEN;
        case AC_LOG_LEVEL_DEBUG: return COLOR_CYAN;
        default: return COLOR_RESET;
    }
}

/**
 * @brief Get log level string
 */
static const char* get_level_string(ac_log_level_t level) {
    switch (level) {
        case AC_LOG_LEVEL_ERROR: return "ERROR";
        case AC_LOG_LEVEL_WARN:  return "WARN ";
        case AC_LOG_LEVEL_INFO:  return "INFO ";
        case AC_LOG_LEVEL_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
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
 * @brief Platform default log handler for POSIX systems
 */
void ac_log_platform_default_handler(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
) {
    // Get timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Print log prefix with color
    const char* color = get_level_color(level);
    const char* level_str = get_level_string(level);
    
    fprintf(stderr, "%s[%s] [%s]%s ", 
            color, time_buf, level_str, COLOR_RESET);
    
    // Print message
    vfprintf(stderr, fmt, args);
    
    // Print source location in gray
    fprintf(stderr, " %s(%s:%d %s)%s\n",
            COLOR_GRAY, get_basename(file), line, func, COLOR_RESET);
    
    fflush(stderr);
}
