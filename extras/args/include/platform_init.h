/**
 * @file platform_init.h
 * @brief Platform-specific terminal initialization
 *
 * Provides cross-platform terminal initialization for hosted environments.
 * Handles UTF-8 encoding, color support, and other platform-specific setup.
 */

#ifndef PLATFORM_INIT_H
#define PLATFORM_INIT_H

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

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_INIT_H */
