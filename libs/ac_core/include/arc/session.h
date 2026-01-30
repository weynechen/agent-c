/**
 * @file session.h
 * @brief ArC Session Management
 *
 * Session provides lifecycle management for agents, tool registries, and MCP clients.
 * All resources are automatically cleaned up when the session closes.
 *
 * Example:
 * @code
 * ac_session_t *session = ac_session_open();
 *
 * // Create tool registry (managed by session)
 * ac_tool_registry_t *tools = ac_tool_registry_create(session);
 * ac_tool_registry_add_array(tools, AC_TOOLS(read_file, bash));
 *
 * // Create MCP client (managed by session)
 * ac_mcp_client_t *mcp = ac_mcp_create(session, &config);
 * ac_mcp_connect(mcp);
 * ac_mcp_discover_tools(mcp);
 * ac_tool_registry_add_mcp(tools, mcp);
 *
 * // Create agent
 * ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
 *     .tools = tools,
 *     ...
 * });
 *
 * // Use agent...
 *
 * // Close session - cleans up everything
 * ac_session_close(session);
 * @endcode
 */

#ifndef ARC_SESSION_H
#define ARC_SESSION_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Session Handle (opaque)
 *============================================================================*/

typedef struct ac_session ac_session_t;

/*============================================================================
 * Session API
 *============================================================================*/

/**
 * @brief Open a new session
 *
 * Creates a session to manage resource lifecycle.
 * All resources created within the session (agents, registries, MCP clients)
 * are automatically destroyed when the session closes.
 *
 * @return Session handle, NULL on error
 */
ac_session_t *ac_session_open(void);

/**
 * @brief Close session and destroy all resources
 *
 * Destroys all agents, tool registries, and MCP clients created in this session.
 * All pointers to session resources become invalid after this call.
 *
 * @param session  Session handle
 */
void ac_session_close(ac_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* ARC_SESSION_H */
