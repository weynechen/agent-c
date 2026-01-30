/**
 * @file prompt_loader.h
 * @brief Prompt Loading and Rendering
 *
 * Provides access to embedded prompts and variable substitution.
 * Supports dynamic placeholder replacement for runtime context.
 */

#ifndef PROMPT_LOADER_H
#define PROMPT_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Prompt Context - Runtime Environment for Placeholder Substitution
 *============================================================================*/

/**
 * @brief Runtime context for prompt placeholder substitution
 *
 * Contains environment information that will replace placeholders
 * like ${workspace}, ${cwd}, ${os}, etc. in prompt templates.
 */
typedef struct {
    const char *workspace;           /**< Workspace/project root directory */
    const char *cwd;                 /**< Current working directory */
    const char *directory;           /**< Alias for workspace (compatibility) */
    const char *os;                  /**< Operating system name */
    const char *shell;               /**< Shell name (bash, zsh, etc.) */
    const char *user;                /**< Current username */
    int safe_mode;                   /**< Whether safe mode is enabled */
    int sandbox_enabled;             /**< Whether sandbox is enabled */
} prompt_context_t;

/**
 * @brief Initialize a prompt context with default values
 *
 * Populates the context with current environment information:
 * - cwd: current working directory
 * - os: detected operating system
 * - shell: detected shell
 * - user: current username
 *
 * @param ctx       Context to initialize
 * @param workspace Workspace path (required)
 */
void prompt_context_init(prompt_context_t *ctx, const char *workspace);

/*============================================================================
 * Prompt Access
 *============================================================================*/

/**
 * @brief Get system prompt by name
 *
 * @param name  System prompt name (e.g., "anthropic", "openai")
 * @return Prompt content, or NULL if not found
 */
const char *prompt_get_system(const char *name);

/**
 * @brief Get tool prompt by name
 *
 * @param name  Tool name (e.g., "bash", "read", "edit")
 * @return Prompt content, or NULL if not found
 */
const char *prompt_get_tool(const char *name);

/**
 * @brief Render system prompt with variable substitution (simple)
 *
 * Replaces ${workspace} and similar variables.
 * Caller must free the returned string.
 *
 * @param name       System prompt name
 * @param workspace  Workspace path to substitute
 * @return Rendered prompt (caller must free), or NULL on error
 */
char *prompt_render_system(const char *name, const char *workspace);

/**
 * @brief Render tool prompt with variable substitution (simple)
 *
 * Replaces ${workspace} and similar variables.
 * Caller must free the returned string.
 *
 * @param name       Tool name
 * @param workspace  Workspace path to substitute
 * @return Rendered prompt (caller must free), or NULL on error
 */
char *prompt_render_tool(const char *name, const char *workspace);

/*============================================================================
 * Context-based Rendering (Enhanced)
 *============================================================================*/

/**
 * @brief Render a template string with full context substitution
 *
 * Replaces all supported placeholders:
 * - ${workspace} - workspace path
 * - ${cwd} - current working directory
 * - ${directory} - alias for workspace
 * - ${os} - operating system name
 * - ${shell} - shell name
 * - ${user} - current username
 * - ${safe_mode} - "enabled" or "disabled"
 * - ${sandbox} - "enabled" or "disabled"
 *
 * Caller must free the returned string.
 *
 * @param template   Template string with placeholders
 * @param ctx        Prompt context with substitution values
 * @return Rendered string (caller must free), or NULL on error
 */
char *prompt_render(const char *template, const prompt_context_t *ctx);

/**
 * @brief Render system prompt with full context
 *
 * @param name  System prompt name
 * @param ctx   Prompt context
 * @return Rendered prompt (caller must free), or NULL on error
 */
char *prompt_render_system_ctx(const char *name, const prompt_context_t *ctx);

/**
 * @brief Render tool prompt with full context
 *
 * @param name  Tool name
 * @param ctx   Prompt context
 * @return Rendered prompt (caller must free), or NULL on error
 */
char *prompt_render_tool_ctx(const char *name, const prompt_context_t *ctx);

/*============================================================================
 * Prompt Enumeration
 *============================================================================*/

/**
 * @brief Get count of system prompts
 * @return Number of system prompts
 */
int prompt_system_count(void);

/**
 * @brief Get count of tool prompts
 * @return Number of tool prompts
 */
int prompt_tool_count(void);

/**
 * @brief Get system prompt name by index
 * @param index  Index (0 to count-1)
 * @return Prompt name, or NULL if out of range
 */
const char *prompt_system_name(int index);

/**
 * @brief Get tool prompt name by index
 * @param index  Index (0 to count-1)
 * @return Prompt name, or NULL if out of range
 */
const char *prompt_tool_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* PROMPT_LOADER_H */
