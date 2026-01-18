/**
 * @file log_windows.c
 * @brief Windows platform logging implementation
 * 
 * Outputs colored logs to console using Windows Console API.
 */

#include "agentc/log.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

/* Console color attributes */
#define COLOR_RED     (FOREGROUND_RED | FOREGROUND_INTENSITY)
#define COLOR_YELLOW  (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COLOR_GREEN   (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COLOR_CYAN    (FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COLOR_GRAY    (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COLOR_DEFAULT (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)

/**
 * @brief Get log level color attribute
 */
static WORD get_level_color(ac_log_level_t level) {
    switch (level) {
        case AC_LOG_LEVEL_ERROR: return COLOR_RED;
        case AC_LOG_LEVEL_WARN:  return COLOR_YELLOW;
        case AC_LOG_LEVEL_INFO:  return COLOR_GREEN;
        case AC_LOG_LEVEL_DEBUG: return COLOR_CYAN;
        default: return COLOR_DEFAULT;
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
    const char* base1 = strrchr(path, '/');
    const char* base2 = strrchr(path, '\\');
    const char* base = (base1 > base2) ? base1 : base2;
    return base ? base + 1 : path;
}

/**
 * @brief Platform default log handler for Windows
 */
void ac_log_platform_default_handler(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
) {
    HANDLE console = GetStdHandle(STD_ERROR_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    GetConsoleScreenBufferInfo(console, &console_info);
    WORD original_color = console_info.wAttributes;

    // Get timestamp
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_s(&tm_info, &now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    // Print log prefix with color
    WORD color = get_level_color(level);
    const char* level_str = get_level_string(level);
    
    SetConsoleTextAttribute(console, color);
    fprintf(stderr, "[%s] [%s] ", time_buf, level_str);
    SetConsoleTextAttribute(console, original_color);
    
    // Print message
    vfprintf(stderr, fmt, args);
    
    // Print source location in gray
    SetConsoleTextAttribute(console, COLOR_GRAY);
    fprintf(stderr, " (%s:%d %s)\n", get_basename(file), line, func);
    SetConsoleTextAttribute(console, original_color);
    
    fflush(stderr);
}

#else
/* Non-Windows fallback */
void ac_log_platform_default_handler(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
) {
    fprintf(stderr, "[%s] ", level == AC_LOG_LEVEL_ERROR ? "ERROR" :
                             level == AC_LOG_LEVEL_WARN ? "WARN" :
                             level == AC_LOG_LEVEL_INFO ? "INFO" : "DEBUG");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, " (%s:%d %s)\n", file, line, func);
    fflush(stderr);
}
#endif
