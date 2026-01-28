/**
 * @file mcp.h
 * @brief Model Context Protocol (MCP) Client
 *
 * Client for connecting to MCP servers and discovering tools.
 * MCP tools can be added to an ac_tool_registry_t.
 *
 * The MCP client lifecycle is managed by the session.
 */

#ifndef AGENTC_MCP_H
#define AGENTC_MCP_H

#include "error.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct ac_mcp_client ac_mcp_client_t;
typedef struct ac_session ac_session_t;

/*============================================================================
 * MCP Configuration
 *============================================================================*/

/**
 * @brief MCP client configuration
 */
typedef struct {
    const char *server_url;          /* MCP server URL (required) */
    const char *transport;           /* Transport: "stdio", "http", "sse" (default: "http") */
    uint32_t timeout_ms;             /* Connection timeout (default: 30000) */
    const char *api_key;             /* Optional API key for authentication */
} ac_mcp_config_t;

/*============================================================================
 * MCP Client Creation
 *============================================================================*/

/**
 * @brief Create MCP client within a session
 *
 * The client lifecycle is managed by the session.
 * When the session closes, all MCP clients are automatically destroyed.
 *
 * @param session  Session handle
 * @param config   MCP configuration
 * @return MCP client handle, NULL on error
 *
 * Example:
 * @code
 * ac_session_t *session = ac_session_open();
 * 
 * ac_mcp_client_t *mcp = ac_mcp_create(session, &(ac_mcp_config_t){
 *     .server_url = "http://localhost:3000/mcp"
 * });
 * 
 * if (ac_mcp_connect(mcp) == AGENTC_OK) {
 *     ac_mcp_discover_tools(mcp);
 *     ac_tool_registry_add_mcp(tools, mcp);
 * }
 * 
 * // Use tools...
 * 
 * ac_session_close(session);  // Cleans up MCP client
 * @endcode
 */
ac_mcp_client_t *ac_mcp_create(
    ac_session_t *session,
    const ac_mcp_config_t *config
);

/*============================================================================
 * Connection Management
 *============================================================================*/

/**
 * @brief Connect to MCP server
 *
 * Performs handshake and capability negotiation.
 *
 * @param client  MCP client
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_connect(ac_mcp_client_t *client);

/**
 * @brief Check if connected
 *
 * @param client  MCP client
 * @return 1 if connected, 0 otherwise
 */
int ac_mcp_is_connected(const ac_mcp_client_t *client);

/**
 * @brief Disconnect from server
 *
 * @param client  MCP client
 */
void ac_mcp_disconnect(ac_mcp_client_t *client);

/*============================================================================
 * Tool Discovery
 *============================================================================*/

/**
 * @brief Discover available tools from MCP server
 *
 * Must be connected first. Tools are cached internally.
 *
 * @param client  MCP client
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_discover_tools(ac_mcp_client_t *client);

/**
 * @brief Get discovered tool count
 *
 * @param client  MCP client
 * @return Number of tools, 0 if not discovered
 */
size_t ac_mcp_tool_count(const ac_mcp_client_t *client);

/*============================================================================
 * Tool Execution
 *============================================================================*/

/**
 * @brief Call a tool on the MCP server
 *
 * Used internally by tool registry when executing MCP tools.
 *
 * @param client     MCP client
 * @param name       Tool name
 * @param args_json  JSON arguments
 * @param result     Output result (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_call_tool(
    ac_mcp_client_t *client,
    const char *name,
    const char *args_json,
    char **result
);

/*============================================================================
 * Error Handling
 *============================================================================*/

/**
 * @brief Get last error message
 *
 * @param client  MCP client
 * @return Error message (do not free), NULL if no error
 */
const char *ac_mcp_error(const ac_mcp_client_t *client);

/*============================================================================
 * Internal API (for tool registry)
 *============================================================================*/

/**
 * @brief Get tool info by index (internal use)
 *
 * @param client       MCP client
 * @param index        Tool index
 * @param name         Output: tool name (do not free)
 * @param description  Output: tool description (do not free)
 * @param parameters   Output: JSON schema (do not free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_get_tool_info(
    const ac_mcp_client_t *client,
    size_t index,
    const char **name,
    const char **description,
    const char **parameters
);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MCP_H */
