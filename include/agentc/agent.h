/**
 * @file agent.h
 * @brief AgentC ReACT Agent
 *
 * Implements a ReACT (Reasoning + Acting) agent loop.
 * Similar to OpenAI Agents SDK design.
 */

#ifndef AGENTC_AGENT_H
#define AGENTC_AGENT_H

#include "llm.h"
#include "tool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Agent Run Result
 *============================================================================*/

typedef enum {
    AGENTC_RUN_SUCCESS,           /* Completed successfully */
    AGENTC_RUN_MAX_ITERATIONS,    /* Hit max iterations limit */
    AGENTC_RUN_ERROR,             /* Error occurred */
    AGENTC_RUN_ABORTED,           /* Aborted by user callback */
} agentc_run_status_t;

typedef struct {
    agentc_run_status_t status;   /* Run status */
    char *final_output;           /* Final response (caller must free) */
    int iterations;               /* Number of ReACT iterations */
    int total_tokens;             /* Total tokens used */
    agentc_err_t error_code;      /* Error code if status == ERROR */
} agentc_run_result_t;

/*============================================================================
 * Agent Hooks (Callbacks for observability)
 *============================================================================*/

typedef struct agentc_agent_hooks {
    /**
     * Called when agent starts processing
     * @return 0 to continue, non-zero to abort
     */
    int (*on_start)(const char *user_input, void *user_data);

    /**
     * Called when LLM generates text content (streaming or complete)
     * @param content     Content chunk
     * @param len         Content length
     * @param is_complete 1 if this is the final chunk
     * @return 0 to continue, non-zero to abort
     */
    int (*on_content)(const char *content, size_t len, int is_complete, void *user_data);

    /**
     * Called when LLM requests tool calls
     * @param calls  Linked list of tool calls
     * @return 0 to continue, non-zero to abort
     */
    int (*on_tool_call)(const agentc_tool_call_t *calls, void *user_data);

    /**
     * Called when tool execution completes
     * @param results  Linked list of tool results
     * @return 0 to continue, non-zero to abort
     */
    int (*on_tool_result)(const agentc_tool_result_t *results, void *user_data);

    /**
     * Called when agent completes
     * @param result  Final run result
     */
    void (*on_complete)(const agentc_run_result_t *result, void *user_data);

    /**
     * Called on error
     * @param error    Error code
     * @param message  Error message
     */
    void (*on_error)(agentc_err_t error, const char *message, void *user_data);

    void *user_data;              /* User context passed to all callbacks */
} agentc_agent_hooks_t;

/*============================================================================
 * Agent Configuration
 *============================================================================*/

typedef struct {
    /* Required */
    agentc_llm_client_t *llm;          /* LLM client (required) */
    agentc_tool_registry_t *tools;     /* Tool registry (optional) */

    /* System prompt */
    const char *name;                  /* Agent name (optional) */
    const char *instructions;          /* System instructions (optional) */

    /* Execution limits */
    int max_iterations;                /* Max ReACT loops (default: 10) */
    int max_tokens;                    /* Max tokens per response (0 = no limit) */
    float temperature;                 /* Temperature (default: 0.7) */

    /* Tool behavior */
    const char *tool_choice;           /* "auto", "none", "required" (default: "auto") */
    int parallel_tool_calls;           /* Allow parallel tool calls (default: 1) */

    /* Streaming */
    int stream;                        /* Enable streaming (default: 0) */

    /* Hooks */
    agentc_agent_hooks_t hooks;        /* Event callbacks */
} agentc_agent_config_t;

/*============================================================================
 * Agent Handle
 *============================================================================*/

typedef struct agentc_agent agentc_agent_t;

/*============================================================================
 * Agent API
 *============================================================================*/

/**
 * @brief Create an agent
 *
 * @param config  Agent configuration
 * @param out     Output agent handle
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_agent_create(
    const agentc_agent_config_t *config,
    agentc_agent_t **out
);

/**
 * @brief Destroy an agent
 *
 * @param agent  Agent handle
 */
void agentc_agent_destroy(agentc_agent_t *agent);

/**
 * @brief Run agent with user input (blocking)
 *
 * Executes the ReACT loop until completion or max iterations.
 *
 * @param agent   Agent handle
 * @param input   User input message
 * @param result  Output result (caller must free final_output)
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_agent_run(
    agentc_agent_t *agent,
    const char *input,
    agentc_run_result_t *result
);

/**
 * @brief Run agent with message history
 *
 * Allows continuing a conversation with existing history.
 *
 * @param agent     Agent handle
 * @param messages  Message history (will be modified to add new messages)
 * @param result    Output result
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_agent_run_with_history(
    agentc_agent_t *agent,
    agentc_message_t **messages,
    agentc_run_result_t *result
);

/**
 * @brief Reset agent state
 *
 * Clears internal state for a fresh run.
 *
 * @param agent  Agent handle
 */
void agentc_agent_reset(agentc_agent_t *agent);

/**
 * @brief Free run result resources
 *
 * @param result  Result to free
 */
void agentc_run_result_free(agentc_run_result_t *result);

/*============================================================================
 * Convenience: Simple one-shot run
 *============================================================================*/

/**
 * @brief Quick agent run without creating persistent agent
 *
 * Creates a temporary agent, runs it once, and destroys it.
 *
 * @param llm         LLM client
 * @param tools       Tool registry (optional)
 * @param system      System prompt (optional)
 * @param user_input  User input
 * @param output      Output response (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_agent_quick_run(
    agentc_llm_client_t *llm,
    agentc_tool_registry_t *tools,
    const char *system,
    const char *user_input,
    char **output
);

/*============================================================================
 * Default Values
 *============================================================================*/

#define AGENTC_AGENT_DEFAULT_MAX_ITERATIONS  10
#define AGENTC_AGENT_DEFAULT_TEMPERATURE     0.7f
#define AGENTC_AGENT_DEFAULT_TOOL_CHOICE     "auto"

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_AGENT_H */
