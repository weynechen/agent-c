/**
 * @file agent_hooks.h
 * @brief Agent Lifecycle Hooks
 *
 * Provides a non-intrusive way to observe agent execution.
 * Hooks are optional callbacks that get invoked at key points
 * during agent execution.
 *
 * Compile-time control:
 * - Define AC_DISABLE_HOOKS to completely disable hooks (zero overhead)
 *
 * Use cases:
 * - Tracing/observability (ac_trace module)
 * - Metrics collection
 * - Debugging/profiling
 * - Custom logging
 *
 * Example:
 * @code
 * // Define custom hooks
 * static void my_on_run_start(void *ctx, const ac_hook_run_start_t *info) {
 *     printf("Agent %s started with: %s\n", info->agent_name, info->message);
 * }
 *
 * // Register hooks
 * ac_agent_hooks_t hooks = {
 *     .ctx = my_context,
 *     .on_run_start = my_on_run_start,
 * };
 * ac_agent_set_hooks(&hooks);
 *
 * // Run agent - hooks will be called
 * ac_agent_run(agent, "Hello");
 *
 * // Unregister hooks
 * ac_agent_set_hooks(NULL);
 * @endcode
 */

#ifndef ARC_AGENT_HOOKS_H
#define ARC_AGENT_HOOKS_H

#include <stddef.h>
#include <stdint.h>
#include "arc/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Hook Info Structures
 *
 * These structures contain the data passed to each hook callback.
 * All pointers are valid only for the duration of the callback.
 *============================================================================*/

/**
 * @brief Info for on_run_start hook
 */
typedef struct {
    const char *agent_name;      /**< Agent name (may be NULL) */
    const char *message;         /**< User input message */
    const char *instructions;    /**< System instructions (may be NULL) */
    int max_iterations;          /**< Max ReACT iterations */
    size_t tool_count;           /**< Number of registered tools */
} ac_hook_run_start_t;

/**
 * @brief Info for on_run_end hook
 */
typedef struct {
    const char *agent_name;      /**< Agent name */
    const char *content;         /**< Final response content (may be NULL) */
    int iterations;              /**< Number of iterations executed */
    int total_prompt_tokens;     /**< Total prompt tokens used */
    int total_completion_tokens; /**< Total completion tokens used */
    uint64_t duration_ms;        /**< Total execution time in ms */
} ac_hook_run_end_t;

/**
 * @brief Info for on_iter_start/end hooks
 */
typedef struct {
    const char *agent_name;      /**< Agent name */
    int iteration;               /**< Current iteration (1-based) */
    int max_iterations;          /**< Maximum iterations */
} ac_hook_iter_t;

/**
 * @brief Info for on_llm_request hook
 *
 * Note: messages and tools are raw pointers. Use ac_messages_to_json_string()
 * and similar helpers if you need JSON serialization.
 */
typedef struct {
    const char *agent_name;           /**< Agent name */
    const char *model;                /**< Model name */
    const ac_message_t *messages;     /**< Message list (raw pointer) */
    const char *tools_schema;         /**< Tools schema as JSON (may be NULL) */
    size_t message_count;             /**< Number of messages */
} ac_hook_llm_request_t;

/**
 * @brief Info for on_llm_response hook
 *
 * Note: tool_calls is a raw pointer. Use ac_tool_calls_to_json_string()
 * if you need JSON serialization.
 */
typedef struct {
    const char *agent_name;           /**< Agent name */
    const char *content;              /**< Response content (may be NULL) */
    const ac_tool_call_t *tool_calls; /**< Tool call list (raw pointer) */
    int tool_call_count;              /**< Number of tool calls */
    int prompt_tokens;                /**< Prompt tokens used */
    int completion_tokens;            /**< Completion tokens used */
    int total_tokens;                 /**< Total tokens used */
    const char *finish_reason;        /**< Finish reason */
    uint64_t duration_ms;             /**< LLM request duration in ms */
} ac_hook_llm_response_t;

/**
 * @brief Info for on_tool_start hook
 */
typedef struct {
    const char *agent_name;      /**< Agent name */
    const char *id;              /**< Tool call ID */
    const char *name;            /**< Tool function name */
    const char *arguments;       /**< Tool arguments as JSON */
} ac_hook_tool_start_t;

/**
 * @brief Info for on_tool_end hook
 */
typedef struct {
    const char *agent_name;      /**< Agent name */
    const char *id;              /**< Tool call ID */
    const char *name;            /**< Tool function name */
    const char *result;          /**< Tool result as JSON */
    uint64_t duration_ms;        /**< Tool execution time in ms */
    int success;                 /**< 1 if successful, 0 if error */
} ac_hook_tool_end_t;

/*============================================================================
 * Agent Hooks Structure
 *============================================================================*/

/**
 * @brief Agent lifecycle hooks
 *
 * All callbacks are optional - set to NULL if not needed.
 * Callbacks are invoked synchronously in the agent's thread.
 * Keep callbacks fast to avoid blocking agent execution.
 */
typedef struct {
    void *ctx;  /**< User context passed to all callbacks */

    /* Agent run lifecycle */
    void (*on_run_start)(void *ctx, const ac_hook_run_start_t *info);
    void (*on_run_end)(void *ctx, const ac_hook_run_end_t *info);

    /* ReACT iteration */
    void (*on_iter_start)(void *ctx, const ac_hook_iter_t *info);
    void (*on_iter_end)(void *ctx, const ac_hook_iter_t *info);

    /* LLM interaction */
    void (*on_llm_request)(void *ctx, const ac_hook_llm_request_t *info);
    void (*on_llm_response)(void *ctx, const ac_hook_llm_response_t *info);

    /* Tool execution */
    void (*on_tool_start)(void *ctx, const ac_hook_tool_start_t *info);
    void (*on_tool_end)(void *ctx, const ac_hook_tool_end_t *info);

} ac_agent_hooks_t;

/*============================================================================
 * Hooks API
 *============================================================================*/

/**
 * @brief Set global agent hooks
 *
 * Registers hooks that will be called for all agent executions.
 * Only one set of hooks can be active at a time.
 *
 * @param hooks Hooks structure (copied), or NULL to disable hooks
 *
 * Thread safety: This function is not thread-safe.
 * Set hooks before starting any agent runs.
 */
void ac_agent_set_hooks(const ac_agent_hooks_t *hooks);

/**
 * @brief Get current hooks
 *
 * @return Current hooks, or NULL if not set
 */
const ac_agent_hooks_t *ac_agent_get_hooks(void);

#ifdef __cplusplus
}
#endif

#endif /* ARC_AGENT_HOOKS_H */
