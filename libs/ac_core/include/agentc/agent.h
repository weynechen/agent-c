/**
 * @file agent.h
 * @brief AgentC ReACT Agent
 *
 * Implements a ReACT (Reasoning + Acting) agent loop.
 * Supports both synchronous and streaming execution.
 */

#ifndef AGENTC_AGENT_H
#define AGENTC_AGENT_H

#include <stdint.h>
#include "llm.h"
#include "tool.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Agent Run Result
 *============================================================================*/

typedef enum {
    AC_RUN_SUCCESS,           /* Completed successfully */
    AC_RUN_MAX_ITERATIONS,    /* Hit max iterations limit */
    AC_RUN_ERROR,             /* Error occurred */
    AC_RUN_ABORTED,           /* Aborted by user callback */
} ac_run_status_t;

typedef struct {
    ac_run_status_t status;   /* Run status */
    char *response;           /* Final response (caller must free) */
    int iterations;           /* Number of ReACT iterations */
    int total_tokens;         /* Total tokens used */
    agentc_err_t error_code;  /* Error code if status == ERROR */
} ac_agent_result_t;

/*============================================================================
 * Agent Parameters
 *============================================================================*/

typedef struct {
    /* Required */
    const char *name;                  /* Agent name (optional) */
    ac_llm_t *llm;                     /* LLM client (required) */
    ac_tools_t *tools;                 /* Tool registry (optional) */
    ac_memory_t *memory;               /* Memory manager (optional) */

    /* Execution limits */
    int max_iterations;                /* Max ReACT loops (default: 10) */
    uint32_t timeout_ms;               /* Timeout in milliseconds (default: 0 = no timeout) */
} ac_agent_params_t;

/*============================================================================
 * Agent Handle
 *============================================================================*/

typedef struct ac_agent ac_agent_t;

/*============================================================================
 * API Functions - Synchronous
 *============================================================================*/

/**
 * @brief Create an agent
 *
 * Example:
 * @code
 * ac_llm_t *llm = ac_llm_create(&(ac_llm_params_t){
 *     .model = "deepseek/deepseek-chat",
 *     .api_key = getenv("DEEPSEEK_API_KEY"),
 *     .instructions = "You are a helpful assistant"
 * });
 * 
 * ac_memory_t *memory = ac_memory_create(&(ac_memory_config_t){
 *     .session_id = "session-123"
 * });
 * 
 * ac_agent_t *agent = ac_agent_create(&(ac_agent_params_t){
 *     .llm = llm,
 *     .tools = tools,
 *     .memory = memory,
 *     .max_iterations = 10
 * });
 * @endcode
 *
 * @param params  Agent parameters
 * @return Agent handle, NULL on error
 */
ac_agent_t *ac_agent_create(const ac_agent_params_t *params);

/**
 * @brief Destroy an agent
 *
 * @param agent  Agent handle
 */
void ac_agent_destroy(ac_agent_t *agent);

/**
 * @brief Run agent with user input (blocking, synchronous)
 *
 * Executes the ReACT loop until completion or max iterations.
 *
 * Example:
 * @code
 * ac_agent_result_t result;
 * ac_agent_run_sync(agent, "What's the weather in Beijing?", &result);
 * printf("%s\n", result.response);
 * ac_agent_result_free(&result);
 * @endcode
 *
 * @param agent   Agent handle
 * @param input   User input message
 * @param result  Output result (caller must free with ac_agent_result_free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_agent_run_sync(
    ac_agent_t *agent,
    const char *input,
    ac_agent_result_t *result
);

/**
 * @brief Reset agent state
 *
 * Clears internal state for a fresh run.
 * Note: Does not clear memory if memory manager is used.
 *
 * @param agent  Agent handle
 */
void ac_agent_reset(ac_agent_t *agent);

/**
 * @brief Free agent result resources
 *
 * @param result  Result to free
 */
void ac_agent_result_free(ac_agent_result_t *result);

/*============================================================================
 * API Functions - Streaming
 *============================================================================*/

/**
 * Stream result type
 */
typedef enum {
    AC_STREAM_CONTENT,        /* Content chunk */
    AC_STREAM_TOOL_CALL,      /* Tool call */
    AC_STREAM_TOOL_RESULT,    /* Tool result */
    AC_STREAM_DONE,           /* Stream complete */
    AC_STREAM_ERROR,          /* Error occurred */
} ac_stream_type_t;

/**
 * Stream result
 */
typedef struct {
    ac_stream_type_t type;    /* Result type */
    
    /* For CONTENT */
    const char *content;      /* Content chunk (not owned, do not free) */
    size_t content_len;       /* Content length */
    
    /* For TOOL_CALL */
    const ac_tool_call_t *tool_call;  /* Tool call (not owned) */
    
    /* For TOOL_RESULT */
    const ac_tool_result_t *tool_result;  /* Tool result (not owned) */
    
    /* For DONE */
    const char *final_response;  /* Final response (not owned) */
    int total_tokens;            /* Total tokens used */
    
    /* For ERROR */
    agentc_err_t error_code;  /* Error code */
    const char *error_msg;    /* Error message (not owned) */
} ac_stream_result_t;

/**
 * Stream handle
 */
typedef struct ac_stream ac_stream_t;

/**
 * @brief Run agent with streaming output
 *
 * Creates a stream that can be polled for results.
 *
 * Example:
 * @code
 * ac_stream_t *stream = ac_agent_run(agent, "What's the weather in Beijing?");
 * 
 * while (ac_stream_is_running(stream)) {
 *     ac_stream_result_t *result = ac_stream_next(stream, 1000); // 1s timeout
 *     if (result) {
 *         switch (result->type) {
 *             case AC_STREAM_CONTENT:
 *                 printf("%.*s", (int)result->content_len, result->content);
 *                 break;
 *             case AC_STREAM_DONE:
 *                 printf("\nDone!\n");
 *                 break;
 *             // ... handle other types
 *         }
 *     }
 * }
 * 
 * ac_stream_destroy(stream);
 * @endcode
 *
 * @param agent  Agent handle
 * @param input  User input message
 * @return Stream handle, NULL on error
 */
ac_stream_t *ac_agent_run(ac_agent_t *agent, const char *input);

/**
 * @brief Check if stream is running
 *
 * @param stream  Stream handle
 * @return 1 if running, 0 if complete or error
 */
int ac_stream_is_running(ac_stream_t *stream);

/**
 * @brief Get next stream result (blocking with timeout)
 *
 * @param stream      Stream handle
 * @param timeout_ms  Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return Stream result, NULL if timeout or stream ended
 */
ac_stream_result_t *ac_stream_next(ac_stream_t *stream, int timeout_ms);

/**
 * @brief Destroy stream
 *
 * Automatically aborts if still running.
 *
 * @param stream  Stream handle
 */
void ac_stream_destroy(ac_stream_t *stream);

/*============================================================================
 * Default Values
 *============================================================================*/

#define AC_AGENT_DEFAULT_MAX_ITERATIONS  10
#define AC_AGENT_DEFAULT_TIMEOUT_MS      0  /* No timeout */

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_AGENT_H */
