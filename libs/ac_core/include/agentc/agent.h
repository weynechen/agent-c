/**
 * @file agent.h
 * @brief AgentC Agent API
 *
 * Provides high-level agent interface with automatic memory management.
 * Agents are created within sessions and use arena allocation internally.
 */

#ifndef AGENTC_AGENT_H
#define AGENTC_AGENT_H

#include "error.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct ac_agent ac_agent_t;

/*============================================================================
 * Forward Declarations - LLM Parameters
 *============================================================================*/

/* Import LLM parameters type */
#include "llm.h"

/*============================================================================
 * Agent Result
 *============================================================================*/

typedef struct {
    const char* content;            /* Response content (owned by agent's arena) */
} ac_agent_result_t;

/*============================================================================
 * Agent Configuration
 *============================================================================*/

typedef struct {
    const char* name;               /* Agent name (optional) */
    const char* instructions;       /* Agent instructions (optional) */
    ac_llm_params_t llm_params;     /* LLM configuration */
    const char* tools_name;         /* Tool group name (optional, e.g., "weather") */
    int max_iterations;             /* Max ReACT loops (default: 10) */
} ac_agent_params_t;

/*============================================================================
 * Agent API
 *============================================================================*/

/**
 * @brief Create an agent within a session
 *
 * Creates an agent with its own arena allocator. The agent automatically
 * creates LLM, tools, and memory managers using the arena.
 *
 * Example:
 * @code
 * ac_session_t* session = ac_session_open();
 * 
 * ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
 *     .name = "My Agent",
 *     .instructions = "You are a helpful assistant",
 *     .llm_params = {
 *         .model = "gpt-4o-mini",
 *         .api_key = getenv("OPENAI_API_KEY"),
 *         .instructions = "Be concise"
 *     },
 *     .tools_name = "weather",
 *     .max_iterations = 10
 * });
 * 
 * // Use agent...
 * ac_agent_result_t* result = ac_agent_run_sync(agent, "What's the weather?");
 * printf("%s\n", result->content);
 * 
 * // Close session (automatically destroys agent)
 * ac_session_close(session);
 * @endcode
 *
 * @param session  Session handle
 * @param params   Agent configuration
 * @return Agent handle, NULL on error
 */
ac_agent_t* ac_agent_create(ac_session_t* session, const ac_agent_params_t* params);

/**
 * @brief Run agent synchronously
 *
 * Executes the agent with the given message and returns the result.
 * The result is allocated from the agent's arena and remains valid
 * until the agent is destroyed.
 *
 * @param agent    Agent handle
 * @param message  User message
 * @return Result (owned by agent's arena), NULL on error
 */
ac_agent_result_t* ac_agent_run_sync(ac_agent_t* agent, const char* message);

/**
 * @brief Destroy an agent
 *
 * Destroys the agent and frees its arena.
 * Note: Normally you don't need to call this directly - agents are
 * automatically destroyed when their session is closed.
 *
 * @param agent  Agent handle
 */
void ac_agent_destroy(ac_agent_t* agent);

/*============================================================================
 * Default Values
 *============================================================================*/

#define AC_AGENT_DEFAULT_MAX_ITERATIONS  10

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_AGENT_H */
