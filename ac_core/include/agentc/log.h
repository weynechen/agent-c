/**
 * @file log.h
 * @brief AgentC logging interface
 * 
 * Provides a unified logging interface across all platforms.
 * Core layer defines the API, port layer implements platform-specific output.
 */

#ifndef AGENTC_LOG_H
#define AGENTC_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log levels
 */
typedef enum {
    AC_LOG_LEVEL_OFF = 0,    /**< Logging disabled */
    AC_LOG_LEVEL_ERROR = 1,  /**< Error messages only */
    AC_LOG_LEVEL_WARN = 2,   /**< Warnings and errors */
    AC_LOG_LEVEL_INFO = 3,   /**< Informational messages */
    AC_LOG_LEVEL_DEBUG = 4   /**< Debug messages (verbose) */
} ac_log_level_t;

/**
 * @brief Log handler function type
 * 
 * User can implement custom log handlers to redirect logs to files,
 * remote servers, or perform custom formatting.
 * 
 * @param level Log level
 * @param file Source file name
 * @param line Line number
 * @param func Function name
 * @param fmt Format string (printf-style)
 * @param args Variable arguments list
 */
typedef void (*ac_log_handler_t)(
    ac_log_level_t level,
    const char* file,
    int line,
    const char* func,
    const char* fmt,
    va_list args
);

/**
 * @brief Set the global log level
 * 
 * Messages below this level will be filtered out.
 * 
 * @param level Minimum log level to output
 */
void ac_log_set_level(ac_log_level_t level);

/**
 * @brief Get the current log level
 * 
 * @return Current log level
 */
ac_log_level_t ac_log_get_level(void);

/**
 * @brief Set a custom log handler
 * 
 * By default, logs are output using the platform-specific implementation
 * in the port layer. Users can override this by setting a custom handler.
 * 
 * @param handler Custom log handler function, or NULL to restore default
 */
void ac_log_set_handler(ac_log_handler_t handler);

/**
 * @brief Internal logging functions (do not call directly, use macros)
 */
void ac_log_error(const char* file, int line, const char* func, const char* fmt, ...);
void ac_log_warn(const char* file, int line, const char* func, const char* fmt, ...);
void ac_log_info(const char* file, int line, const char* func, const char* fmt, ...);
void ac_log_debug(const char* file, int line, const char* func, const char* fmt, ...);

/**
 * @brief Logging macros (preferred interface)
 * 
 * These macros automatically capture file, line, and function information.
 * 
 * Example usage:
 * @code
 * AC_LOG_INFO("Agent initialized: %s", agent_name);
 * AC_LOG_ERROR("HTTP request failed: status=%d", status_code);
 * @endcode
 */
#define AC_LOG_ERROR(fmt, ...) ac_log_error(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AC_LOG_WARN(fmt, ...)  ac_log_warn(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AC_LOG_INFO(fmt, ...)  ac_log_info(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AC_LOG_DEBUG(fmt, ...) ac_log_debug(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LOG_H */
