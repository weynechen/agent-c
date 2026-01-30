/**
 * @file prompt_loader.h
 * @brief Prompt Loading and Rendering
 *
 * Provides access to embedded prompts and variable substitution.
 */

#ifndef PROMPT_LOADER_H
#define PROMPT_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief Render system prompt with variable substitution
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
 * @brief Render tool prompt with variable substitution
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
