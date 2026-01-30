/**
 * @file agent.h
 * @brief ArC Agent API
 *
 * Provides high-level agent interface with automatic memory management.
 * Agents are created within sessions and use arena allocation internally.
 *
 * Example:
 * @code
 * #include <arc.h>
 * #include "tools_gen.h"  // MOC-generated tools
 *
 * int main() {
 *     ac_session_t *session = ac_session_open();
 *
 *     // Create tool registry
 *     ac_tool_registry_t *tools = ac_tool_registry_create(session);
 *     ac_tool_registry_add_array(tools, AC_TOOLS(read_file, write_file, bash));
 *
 *     // Create agent
 *     ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
 *         .name = "CodeAgent",
 *         .instructions = "You are a helpful coding assistant.",
 *         .tools = tools,
 *         .llm = {
 *             .model = "gpt-4o",
 *             .api_key = getenv("OPENAI_API_KEY")
 *         }
 *     });
 *
 *     // Run agent
 *     ac_agent_result_t *result = ac_agent_run(agent, "Read main.c");
 *     printf("%s\n", result->content);
 *
 *     // Close session (destroys all resources)
 *     ac_session_close(session);
 *     return 0;
 * }
 * @endcode
 */

#ifndef ARC_AGENT_H
#define ARC_AGENT_H

#include "error.h"
#include "session.h"
#include "llm.h"
#include "tool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct ac_agent ac_agent_t;

/*============================================================================
 * Agent Result
 *============================================================================*/

/**
 * @brief Result from agent execution
 *
 * The content is owned by the agent's arena and remains valid
 * until the agent is destroyed.
 */
typedef struct {
    const char *content;             /* Response content */
} ac_agent_result_t;

/*============================================================================
 * Agent Configuration
 *============================================================================*/

/**
 * @brief Agent configuration parameters
 */
typedef struct {
    const char *name;                /* Agent name (optional) */
    const char *instructions;        /* System instructions (optional) */
    ac_llm_params_t llm;             /* LLM configuration */
    ac_tool_registry_t *tools;       /* Tool registry (optional) */
    int max_iterations;              /* Max ReACT loops (default: 10) */
} ac_agent_params_t;

/*============================================================================
 * Agent API
 *============================================================================*/

/**
 * @brief Create an agent within a session
 *
 * Creates an agent with its own arena allocator. The agent automatically
 * uses the provided tool registry for function calling.
 *
 * @param session  Session handle
 * @param params   Agent configuration
 * @return Agent handle, NULL on error
 */
ac_agent_t *ac_agent_create(ac_session_t *session, const ac_agent_params_t *params);

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
ac_agent_result_t *ac_agent_run(ac_agent_t *agent, const char *message);

/**
 * @brief Destroy an agent
 *
 * Destroys the agent and frees its arena.
 * Note: Normally you don't need to call this directly - agents are
 * automatically destroyed when their session is closed.
 *
 * @param agent  Agent handle
 */
void ac_agent_destroy(ac_agent_t *agent);

/*============================================================================
 * Default Values
 *============================================================================*/

#define AC_AGENT_DEFAULT_MAX_ITERATIONS  10

#ifdef __cplusplus
}
#endif

#endif /* ARC_AGENT_H */
