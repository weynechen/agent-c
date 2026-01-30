/**
 * @file arc.h
 * @brief ArC - LLM Agent Runtime for Embedded and Constrained Systems
 *
 * Main include file. Include this to use ArC.
 *
 * @code
 * #include <arc.h>
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

#ifndef ARC_H
#define ARC_H

/* Core headers */
#include "arc/error.h"
#include "arc/arena.h"
#include "arc/session.h"
#include "arc/agent.h"
#include "arc/agent_hooks.h"
#include "arc/tool.h"
#include "arc/mcp.h"
#include "arc/llm.h"
#include "arc/log.h"
#include "arc/trace.h"


#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version
 *============================================================================*/

#define ARC_VERSION_MAJOR 0
#define ARC_VERSION_MINOR 1
#define ARC_VERSION_PATCH 0
#define ARC_VERSION_STRING "0.1.0"

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
const char *ac_strerror(arc_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* ARC_H */
