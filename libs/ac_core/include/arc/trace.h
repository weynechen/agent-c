/**
 * @file trace.h
 * @brief ArC Trace API - Observability for Agent Execution
 *
 * Provides non-intrusive tracing for agent execution by implementing
 * agent hooks. The trace module is completely decoupled from the agent
 * module - agent.c has no knowledge of trace.c.
 *
 * Usage:
 * @code
 * #include <arc.h>
 *
 * // Simple: enable tracing with a handler
 * void my_handler(const ac_trace_event_t *event, void *user_data) {
 *     printf("Event: %s\n", ac_trace_event_name(event->type));
 * }
 * ac_trace_enable(my_handler, NULL);
 *
 * // Run agent - trace events will be emitted
 * ac_agent_run(agent, "Hello");
 *
 * // Disable tracing
 * ac_trace_disable();
 * @endcode
 *
 * For hosted environments, use the built-in exporters from trace_exporters.h
 * which provide ready-to-use JSON file and console exporters.
 */

#ifndef ARC_TRACE_H
#define ARC_TRACE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Trace Event Types
 *============================================================================*/

typedef enum {
    AC_TRACE_AGENT_START,        /**< Agent execution started */
    AC_TRACE_AGENT_END,          /**< Agent execution completed */
    AC_TRACE_ITER_START,         /**< ReACT iteration started */
    AC_TRACE_ITER_END,           /**< ReACT iteration completed */
    AC_TRACE_LLM_REQUEST,        /**< LLM request sent */
    AC_TRACE_LLM_RESPONSE,       /**< LLM response received */
    AC_TRACE_TOOL_START,         /**< Tool execution started */
    AC_TRACE_TOOL_END            /**< Tool execution completed */
} ac_trace_event_type_t;

/*============================================================================
 * Trace Event Data Structures
 *============================================================================*/

typedef struct {
    const char *message;
    const char *instructions;
    int max_iterations;
    size_t tool_count;
} ac_trace_agent_start_t;

typedef struct {
    const char *content;
    int iterations;
    int total_prompt_tokens;
    int total_completion_tokens;
    uint64_t duration_ms;
} ac_trace_agent_end_t;

typedef struct {
    int iteration;
    int max_iterations;
} ac_trace_iter_t;

typedef struct {
    const char *model;
    const char *messages_json;
    const char *tools_json;
    size_t message_count;
} ac_trace_llm_request_t;

typedef struct {
    const char *content;
    const char *tool_calls_json;
    int tool_call_count;
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
    const char *finish_reason;
    uint64_t duration_ms;
} ac_trace_llm_response_t;

typedef struct {
    const char *id;
    const char *name;
    const char *arguments;
} ac_trace_tool_start_t;

typedef struct {
    const char *id;
    const char *name;
    const char *result;
    uint64_t duration_ms;
    int success;
} ac_trace_tool_end_t;

/*============================================================================
 * Trace Event Structure
 *============================================================================*/

typedef struct {
    ac_trace_event_type_t type;
    uint64_t timestamp_ms;
    const char *trace_id;
    const char *agent_name;
    int sequence;

    union {
        ac_trace_agent_start_t agent_start;
        ac_trace_agent_end_t agent_end;
        ac_trace_iter_t iter;
        ac_trace_llm_request_t llm_request;
        ac_trace_llm_response_t llm_response;
        ac_trace_tool_start_t tool_start;
        ac_trace_tool_end_t tool_end;
    } data;
} ac_trace_event_t;

/*============================================================================
 * Trace Handler
 *============================================================================*/

/**
 * @brief Trace event handler function type
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
 * @brief Enable tracing with specified handler
 *
 * Registers agent hooks internally to capture execution events
 * and convert them to trace events.
 *
 * @param handler   Event handler callback
 * @param user_data User data passed to handler
 */
void ac_trace_enable(ac_trace_handler_t handler, void *user_data);

/**
 * @brief Disable tracing
 *
 * Unregisters agent hooks and stops trace event generation.
 */
void ac_trace_disable(void);

/**
 * @brief Check if tracing is enabled
 *
 * @return 1 if enabled, 0 if disabled
 */
int ac_trace_is_enabled(void);

/**
 * @brief Get event type name as string
 *
 * @param type Event type
 * @return Static string name
 */
const char *ac_trace_event_name(ac_trace_event_type_t type);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Get current timestamp in milliseconds
 *
 * @return Milliseconds since Unix epoch
 */
uint64_t ac_trace_timestamp_ms(void);

/**
 * @brief Generate a unique trace ID
 *
 * @param buffer Output buffer (at least 32 bytes)
 * @param size   Buffer size
 * @return Pointer to buffer, or NULL on error
 */
char *ac_trace_generate_id(char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ARC_TRACE_H */
