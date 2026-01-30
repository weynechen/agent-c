/**
 * @file code_tools_enhanced.h
 * @brief Enhanced Tool Registration with Prompt Integration
 *
 * This layer merges MOC-generated tool schemas with rich prompt
 * descriptions from txt files, providing:
 * - Full LLM-optimized tool descriptions
 * - Runtime placeholder substitution
 * - Context-aware tool configuration
 */

#ifndef CODE_TOOLS_ENHANCED_H
#define CODE_TOOLS_ENHANCED_H

#include <agentc/tool.h>
#include "prompt_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Enhanced Tool Registry
 *============================================================================*/

/**
 * @brief Create enhanced tools with prompt-based descriptions
 *
 * This function:
 * 1. Copies MOC-generated tool definitions
 * 2. Replaces descriptions with rendered prompts from txt files
 * 3. Applies placeholder substitution using the provided context
 *
 * The returned tools array is dynamically allocated and must be
 * freed with code_tools_enhanced_free().
 *
 * @param ctx           Prompt context for placeholder substitution
 * @param out_count     Output: number of tools created
 * @return Array of enhanced tool pointers, NULL on error
 *
 * Example:
 * @code
 * prompt_context_t ctx;
 * prompt_context_init(&ctx, "/path/to/workspace");
 * 
 * size_t count;
 * ac_tool_t **tools = code_tools_enhanced_create(&ctx, &count);
 * 
 * for (size_t i = 0; i < count; i++) {
 *     ac_tool_registry_add(registry, tools[i]);
 * }
 * 
 * code_tools_enhanced_free(tools, count);
 * @endcode
 */
ac_tool_t **code_tools_enhanced_create(
    const prompt_context_t *ctx,
    size_t *out_count
);

/**
 * @brief Free enhanced tools created by code_tools_enhanced_create
 *
 * @param tools     Array of tool pointers
 * @param count     Number of tools
 */
void code_tools_enhanced_free(ac_tool_t **tools, size_t count);

/**
 * @brief Register all enhanced tools to a registry
 *
 * Convenience function that creates enhanced tools and adds them
 * to a registry in one step.
 *
 * @param registry  Tool registry to add tools to
 * @param ctx       Prompt context for placeholder substitution
 * @return Number of tools registered, or -1 on error
 */
int code_tools_register_enhanced(
    ac_tool_registry_t *registry,
    const prompt_context_t *ctx
);

/*============================================================================
 * Tool Name Mapping
 *============================================================================*/

/**
 * @brief Map between MOC tool names and prompt file names
 *
 * MOC uses function names (e.g., "read_file") while prompts
 * may use different names (e.g., "read"). This structure
 * defines the mapping.
 */
typedef struct {
    const char *moc_name;       /**< Name in MOC-generated tool */
    const char *prompt_name;    /**< Name in prompts/tools/ directory */
} code_tool_name_map_t;

/**
 * @brief Get prompt name for a MOC tool name
 *
 * @param moc_name  MOC-generated tool name
 * @return Prompt file name (without .txt), or moc_name if no mapping
 */
const char *code_tool_get_prompt_name(const char *moc_name);

#ifdef __cplusplus
}
#endif

#endif /* CODE_TOOLS_ENHANCED_H */
