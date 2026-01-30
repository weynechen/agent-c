/**
 * @file trace.h
 * @brief AgentC Trace API - Observability for Agent Execution
 *
 * Provides event-based tracing for agent execution, including:
 * - Agent start/end events
 * - ReACT iteration events
 * - LLM request/response events (with full message history, tools, tokens)
 * - Tool call/result events
 *
 * Usage:
 * @code
 * // Set up trace handler
 * void my_handler(const ac_trace_event_t *event, void *user_data) {
 *     printf("Event: %s\n", ac_trace_event_name(event->type));
 * }
 * ac_trace_set_handler(my_handler, NULL);
 * ac_trace_set_level(AC_TRACE_LEVEL_DETAILED);
 *
 * // Run agent - events will be emitted
 * ac_agent_run(agent, "Hello");
 * @endcode
 *
 * For hosted environments, use the built-in exporters from trace_exporters.h
 */

#ifndef AGENTC_TRACE_H
#define AGENTC_TRACE_H

#include "platform.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Trace Levels
 *============================================================================*/

/**
 * @brief Trace detail levels
 */
typedef enum {
    AC_TRACE_LEVEL_OFF = 0,      /**< Tracing disabled */
    AC_TRACE_LEVEL_BASIC = 1,    /**< Basic events (agent start/end) */
    AC_TRACE_LEVEL_DETAILED = 2  /**< All events including LLM/tool details */
} ac_trace_level_t;

/*============================================================================
 * Trace Event Types
 *============================================================================*/

/**
 * @brief Trace event types
 */
typedef enum {
    AC_TRACE_AGENT_START,        /**< Agent execution started */
    AC_TRACE_AGENT_END,          /**< Agent execution completed */
    AC_TRACE_REACT_ITER_START,   /**< ReACT iteration started */
    AC_TRACE_REACT_ITER_END,     /**< ReACT iteration completed */
    AC_TRACE_LLM_REQUEST,        /**< LLM request sent */
    AC_TRACE_LLM_RESPONSE,       /**< LLM response received */
    AC_TRACE_TOOL_CALL,          /**< Tool execution started */
    AC_TRACE_TOOL_RESULT         /**< Tool execution completed */
} ac_trace_event_type_t;

/*============================================================================
 * Trace Event Data Structures
 *============================================================================*/

/**
 * @brief Agent start event data
 */
typedef struct {
    const char *message;         /**< User input message */
    const char *instructions;    /**< System instructions */
    int max_iterations;          /**< Max ReACT iterations */
    size_t tool_count;           /**< Number of registered tools */
} ac_trace_agent_start_t;

/**
 * @brief Agent end event data
 */
typedef struct {
    const char *content;         /**< Final response content */
    int iterations;              /**< Number of iterations executed */
    int total_prompt_tokens;     /**< Total prompt tokens used */
    int total_completion_tokens; /**< Total completion tokens used */
    uint64_t duration_ms;        /**< Total execution time in ms */
} ac_trace_agent_end_t;

/**
 * @brief ReACT iteration event data
 */
typedef struct {
    int iteration;               /**< Current iteration number (1-based) */
    int max_iterations;          /**< Maximum iterations allowed */
} ac_trace_react_iter_t;

/**
 * @brief LLM request event data
 */
typedef struct {
    const char *model;           /**< Model name */
    const char *messages_json;   /**< Full message history as JSON array */
    const char *tools_json;      /**< Tools schema as JSON array (may be NULL) */
    size_t message_count;        /**< Number of messages */
} ac_trace_llm_request_t;

/**
 * @brief LLM response event data
 */
typedef struct {
    const char *content;         /**< Response content (may be NULL if tool calls) */
    const char *tool_calls_json; /**< Tool calls as JSON array (may be NULL) */
    int tool_call_count;         /**< Number of tool calls */
    int prompt_tokens;           /**< Tokens used for prompt */
    int completion_tokens;       /**< Tokens used for completion */
    int total_tokens;            /**< Total tokens used */
    const char *finish_reason;   /**< Finish reason: "stop", "tool_calls", etc. */
    uint64_t duration_ms;        /**< LLM request duration in ms */
} ac_trace_llm_response_t;

/**
 * @brief Tool call event data
 */
typedef struct {
    const char *id;              /**< Tool call ID */
    const char *name;            /**< Tool function name */
    const char *arguments;       /**< Tool arguments as JSON */
} ac_trace_tool_call_t;

/**
 * @brief Tool result event data
 */
typedef struct {
    const char *id;              /**< Tool call ID */
    const char *name;            /**< Tool function name */
    const char *result;          /**< Tool result as JSON */
    uint64_t duration_ms;        /**< Tool execution time in ms */
    int success;                 /**< 1 if successful, 0 if error */
} ac_trace_tool_result_t;

/*============================================================================
 * Trace Event Structure
 *============================================================================*/

/**
 * @brief Unified trace event structure
 *
 * All trace events share common fields and have type-specific data in union.
 */
typedef struct {
    ac_trace_event_type_t type;  /**< Event type */
    uint64_t timestamp_ms;       /**< Event timestamp (ms since epoch) */
    const char *trace_id;        /**< Unique trace ID for this run */
    const char *agent_name;      /**< Agent name */
    int sequence;                /**< Event sequence number within trace */
    
    /**< Type-specific event data */
    union {
        ac_trace_agent_start_t agent_start;
        ac_trace_agent_end_t agent_end;
        ac_trace_react_iter_t react_iter;
        ac_trace_llm_request_t llm_request;
        ac_trace_llm_response_t llm_response;
        ac_trace_tool_call_t tool_call;
        ac_trace_tool_result_t tool_result;
    } data;
} ac_trace_event_t;

/*============================================================================
 * Trace Handler
 *============================================================================*/

/**
 * @brief Trace event handler function type
 *
 * User implements this to receive trace events.
 *
 * @param event     Trace event (valid only during callback)
 * @param user_data User-provided context
 */
typedef void (*ac_trace_handler_t)(
    const ac_trace_event_t *event,
    void *user_data
);

/*============================================================================
 * Trace API
 *============================================================================*/

/**
 * @brief Set the global trace handler
 *
 * Only one handler can be active at a time.
 * Set to NULL to disable tracing.
 *
 * @param handler   Trace handler function
 * @param user_data User data passed to handler
 */
void ac_trace_set_handler(ac_trace_handler_t handler, void *user_data);

/**
 * @brief Set the trace detail level
 *
 * @param level Trace level (OFF, BASIC, DETAILED)
 */
void ac_trace_set_level(ac_trace_level_t level);

/**
 * @brief Get the current trace level
 *
 * @return Current trace level
 */
ac_trace_level_t ac_trace_get_level(void);

/**
 * @brief Check if tracing is enabled
 *
 * @return 1 if tracing is enabled, 0 otherwise
 */
int ac_trace_is_enabled(void);

/**
 * @brief Get event type name as string
 *
 * @param type Event type
 * @return Static string name (e.g., "agent_start", "llm_request")
 */
const char *ac_trace_event_name(ac_trace_event_type_t type);

/*============================================================================
 * Internal API (used by agent.c, llm.c)
 *============================================================================*/

/**
 * @brief Emit a trace event
 *
 * This is called internally by agent/llm code. Do not call directly.
 *
 * @param event Event to emit (will be passed to handler)
 */
void ac_trace_emit(const ac_trace_event_t *event);

/**
 * @brief Generate a new trace ID
 *
 * Creates a unique ID for a trace session.
 *
 * @param buffer Output buffer (at least 32 bytes)
 * @param size   Buffer size
 * @return Pointer to buffer, or NULL on error
 */
char *ac_trace_generate_id(char *buffer, size_t size);

/**
 * @brief Get current timestamp in milliseconds
 *
 * @return Milliseconds since Unix epoch
 */
uint64_t ac_trace_timestamp_ms(void);

/*============================================================================
 * Trace Context (for internal use)
 *============================================================================*/

/**
 * @brief Trace context for an agent run
 *
 * Maintained by agent.c during execution.
 */
typedef struct {
    char trace_id[32];           /**< Unique trace ID */
    const char *agent_name;      /**< Agent name */
    int sequence;                /**< Event sequence counter */
    uint64_t start_time_ms;      /**< Run start time */
    int total_prompt_tokens;     /**< Accumulated prompt tokens */
    int total_completion_tokens; /**< Accumulated completion tokens */
} ac_trace_ctx_t;

/**
 * @brief Initialize trace context
 */
void ac_trace_ctx_init(ac_trace_ctx_t *ctx, const char *agent_name);

/**
 * @brief Get next sequence number
 */
int ac_trace_ctx_next_seq(ac_trace_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_TRACE_H */
