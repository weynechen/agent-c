/**
 * @file platform_wrap.h
 * @brief Platform-specific wrapper layer
 *
 * Provides cross-platform abstractions for hosted environments.
 * Handles terminal initialization, UTF-8 encoding, command line arguments,
 * color support, and other platform-specific functionality.
 */

#ifndef PLATFORM_WRAP_H
#define PLATFORM_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for terminal initialization
 */
typedef struct {
    int enable_colors;  /**< Enable ANSI color codes (1=yes, 0=no, -1=auto) */
    int enable_utf8;    /**< Enable UTF-8 encoding (1=yes, 0=no, -1=auto) */
} platform_init_config_t;

/**
 * @brief Initialize terminal for the current platform
 *
 * This function performs platform-specific terminal setup:
 * - Windows: Set console code pages to UTF-8
 * - Linux/macOS: Check terminal capabilities
 * - Others: No-op
 *
 * @param config Configuration options (NULL for defaults)
 * @return 0 on success, -1 on error
 */
int platform_init_terminal(const platform_init_config_t *config);

/**
 * @brief Cleanup terminal state
 *
 * Restores terminal to original state if needed.
 */
void platform_cleanup_terminal(void);

/**
 * @brief Get default configuration
 *
 * Returns a default configuration with auto-detection enabled.
 *
 * @return Default configuration
 */
platform_init_config_t platform_init_get_defaults(void);

/**
 * @brief Get UTF-8 encoded command line arguments
 *
 * On Windows, converts system-encoded argv to UTF-8.
 * On other platforms, returns the original argv.
 *
 * Usage example:
 * @code
 * int main(int argc, char *argv[]) {
 *     platform_init_terminal(NULL);
 *     
 *     char **utf8_argv = platform_get_argv_utf8(argc, argv);
 *     // Use utf8_argv[1], utf8_argv[2], etc.
 *     
 *     platform_free_argv_utf8(utf8_argv, argc);
 *     platform_cleanup_terminal();
 *     return 0;
 * }
 * @endcode
 *
 * @param argc Number of arguments
 * @param argv Original command line arguments
 * @return UTF-8 encoded arguments (must be freed with platform_free_argv_utf8)
 */
char **platform_get_argv_utf8(int argc, char *argv[]);

/**
 * @brief Free UTF-8 command line arguments
 *
 * Frees memory allocated by platform_get_argv_utf8.
 *
 * @param utf8_argv UTF-8 encoded arguments
 * @param argc Number of arguments
 */
void platform_free_argv_utf8(char **utf8_argv, int argc);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_WRAP_H */
