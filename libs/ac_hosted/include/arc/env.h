/**
 * @file env.h
 * @brief Environment Configuration Loader (Hosted Feature)
 *
 * Multi-level environment configuration loading with fallback chain:
 * 1. Environment variables (already set in shell)
 * 2. User config directory (~/.config/arc/.env)
 * 3. Current working directory (./.env)
 *
 * Usage:
 * @code
 * #include <arc/env.h>
 *
 * int main(void) {
 *     // Load from all levels (user config + current dir)
 *     ac_env_load(NULL);
 *
 *     // Get required API key (exits with error if not found)
 *     const char *api_key = ac_env_require("OPENAI_API_KEY");
 *
 *     // Get optional value with default
 *     const char *model = ac_env_get("OPENAI_MODEL", "gpt-4o-mini");
 *
 *     // ...
 * }
 * @endcode
 */

#ifndef ARC_HOSTED_ENV_H
#define ARC_HOSTED_ENV_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Environment Loading
 *============================================================================*/

/**
 * @brief Load environment from multi-level config files
 *
 * Loads .env files from multiple locations in order (later overrides earlier):
 * 1. User config: ~/.config/arc/.env (or $XDG_CONFIG_HOME/arc/.env)
 * 2. App-specific: app_name directory if provided
 * 3. Current directory: ./.env
 *
 * Environment variables already set in shell take precedence over file values.
 *
 * @param app_name  Optional application name for app-specific config
 *                  (e.g., "chat_git_commit" loads ~/.config/arc/chat_git_commit/.env)
 *                  Pass NULL to skip app-specific loading.
 * @return Number of .env files successfully loaded
 */
int ac_env_load(const char *app_name);

/**
 * @brief Load environment with verbose output
 *
 * Same as ac_env_load but prints which files are loaded.
 *
 * @param app_name  Optional application name
 * @return Number of .env files successfully loaded
 */
int ac_env_load_verbose(const char *app_name);

/*============================================================================
 * Environment Access
 *============================================================================*/

/**
 * @brief Get environment variable with default value
 *
 * @param name          Environment variable name
 * @param default_value Default value if not set or empty
 * @return Environment value or default_value
 */
const char *ac_env_get(const char *name, const char *default_value);

/**
 * @brief Get required environment variable
 *
 * Logs error and returns NULL if variable is not set.
 *
 * @param name  Environment variable name
 * @return Environment value or NULL if not set
 */
const char *ac_env_require(const char *name);

/**
 * @brief Check if environment variable is set and non-empty
 *
 * @param name  Environment variable name
 * @return true if set and non-empty
 */
bool ac_env_isset(const char *name);

/*============================================================================
 * Configuration Paths
 *============================================================================*/

/**
 * @brief Get user config directory for arc
 *
 * Returns path to ~/.config/arc or $XDG_CONFIG_HOME/arc.
 * Creates directory if it doesn't exist.
 *
 * @param buffer      Buffer to store path
 * @param buffer_size Size of buffer
 * @return Pointer to buffer on success, NULL on error
 */
char *ac_env_get_config_dir(char *buffer, size_t buffer_size);

/**
 * @brief Print help message for environment setup
 *
 * Prints instructions for setting up .env files.
 *
 * @param app_name  Application name for context
 */
void ac_env_print_help(const char *app_name);

#ifdef __cplusplus
}
#endif

#endif /* ARC_HOSTED_ENV_H */
