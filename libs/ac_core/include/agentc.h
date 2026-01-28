/**
 * @file agentc.h
 * @brief AgentC - LLM Agent Runtime for Embedded and Constrained Systems
 *
 * Main include file. Include this to use AgentC.
 *
 * @code
 * #include <agentc.h>
 * #include "tools_gen.h"  // MOC-generated tools
 *
 * int main(void) {
 *     ac_session_t *session = ac_session_open();
 *     
 *     // Create tool registry
 *     ac_tool_registry_t *tools = ac_tool_registry_create(session);
 *     ac_tool_registry_add_array(tools, AC_TOOLS(read_file, bash));
 *     
 *     // Create agent
 *     ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
 *         .name = "MyAgent",
 *         .tools = tools,
 *         .llm = { .model = "gpt-4o" }
 *     });
 *     
 *     // Run
 *     ac_agent_result_t *result = ac_agent_run(agent, "Hello!");
 *     printf("%s\n", result->content);
 *     
 *     ac_session_close(session);
 *     return 0;
 * }
 * @endcode
 */

#ifndef AGENTC_H
#define AGENTC_H

/* Core headers */
#include "agentc/error.h"
#include "agentc/arena.h"
#include "agentc/session.h"
#include "agentc/agent.h"
#include "agentc/tool.h"
#include "agentc/mcp.h"
#include "agentc/llm.h"
#include "agentc/log.h"


#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version
 *============================================================================*/

#define AGENTC_VERSION_MAJOR 0
#define AGENTC_VERSION_MINOR 1
#define AGENTC_VERSION_PATCH 0
#define AGENTC_VERSION_STRING "0.1.0"

/*============================================================================
 * Global Initialization
 *============================================================================*/

const char *ac_version(void);

/**
 * @brief Get error message for error code
 *
 * @param err  Error code
 * @return Human-readable error message
 */
const char *ac_strerror(agentc_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_H */
