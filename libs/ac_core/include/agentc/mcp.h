/**
 * @file mcp.h
 * @brief Model Context Protocol (MCP) Client
 *
 * Client for connecting to MCP servers over HTTP/HTTPS and discovering tools.
 * MCP tools can be added to an ac_tool_registry_t.
 *
 * The MCP client lifecycle is managed by the session.
 *
 * Protocol Reference: https://modelcontextprotocol.io/
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
typedef struct ac_tool_registry ac_tool_registry_t;

/*============================================================================
 * MCP Configuration
 *============================================================================*/

/**
 * @brief MCP client configuration
 *
 * Configures connection to an MCP server over HTTP/HTTPS.
 */
typedef struct {
    const char *server_url;          /* MCP server URL (required, e.g., "http://localhost:3000/mcp") */
    uint32_t timeout_ms;             /* Request timeout in ms (default: 30000) */
    const char *api_key;             /* Optional API key for authentication */
    int verify_ssl;                  /* Verify SSL certificate (default: 1) */
    
    /* Client identification (sent in initialize) */
    const char *client_name;         /* Client name (default: "AgentC") */
    const char *client_version;      /* Client version (default: "1.0.0") */
} ac_mcp_config_t;

/*============================================================================
 * MCP Server Info (from initialize response)
 *============================================================================*/

/**
 * @brief MCP server information
 */
typedef struct {
    const char *name;                /* Server name */
    const char *version;             /* Server version */
    const char *protocol_version;    /* MCP protocol version */
} ac_mcp_server_info_t;

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
 * Sends initialize request and performs capability negotiation.
 * The initialized notification is sent automatically.
 *
 * @param client  MCP client
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_connect(ac_mcp_client_t *client);

/**
 * @brief Check if connected
 *
 * @param client  MCP client
 * @return 1 if connected and initialized, 0 otherwise
 */
int ac_mcp_is_connected(const ac_mcp_client_t *client);

/**
 * @brief Get server info (after connect)
 *
 * @param client  MCP client
 * @return Server info, NULL if not connected
 */
const ac_mcp_server_info_t *ac_mcp_server_info(const ac_mcp_client_t *client);

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
 * Call ac_tool_registry_add_mcp() to add discovered tools to a registry.
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

/**
 * @brief Cleanup MCP client (called by session)
 */
void ac_mcp_cleanup(ac_mcp_client_t *client);

/*============================================================================
 * Multi-Server Configuration (.mcp.json)
 *============================================================================*/

/**
 * @brief MCP servers configuration (loaded from .mcp.json)
 *
 * File format:
 * @code{.json}
 * {
 *   "servers": [
 *     {
 *       "name": "context7",
 *       "url": "https://mcp.context7.com/mcp",
 *       "enabled": true
 *     },
 *     {
 *       "name": "local-fs",
 *       "url": "http://localhost:3001/mcp",
 *       "api_key": "secret-key",
 *       "timeout_ms": 60000,
 *       "enabled": true
 *     }
 *   ]
 * }
 * @endcode
 */
typedef struct ac_mcp_servers_config ac_mcp_servers_config_t;

/**
 * @brief Load MCP configuration from .mcp.json file
 *
 * Searches for .mcp.json in current directory.
 * The file should be a dotfile to protect API keys.
 *
 * @param path  Path to .mcp.json file (NULL = ".mcp.json" in current dir)
 * @return Configuration handle, NULL if file not found or parse error
 *
 * Example:
 * @code
 * ac_mcp_servers_config_t *config = ac_mcp_load_config(NULL);
 * if (config) {
 *     size_t connected = ac_mcp_connect_all(session, config, tools);
 *     printf("Connected to %zu MCP servers\n", connected);
 *     ac_mcp_config_free(config);
 * }
 * @endcode
 */
ac_mcp_servers_config_t *ac_mcp_load_config(const char *path);

/**
 * @brief Get number of servers in configuration
 *
 * @param config  Configuration handle
 * @return Number of servers (including disabled)
 */
size_t ac_mcp_config_server_count(const ac_mcp_servers_config_t *config);

/**
 * @brief Get number of enabled servers
 *
 * @param config  Configuration handle
 * @return Number of enabled servers
 */
size_t ac_mcp_config_enabled_count(const ac_mcp_servers_config_t *config);

/**
 * @brief Free configuration
 *
 * @param config  Configuration to free
 */
void ac_mcp_config_free(ac_mcp_servers_config_t *config);

/**
 * @brief Connect to all enabled MCP servers and add tools to registry
 *
 * Convenience function that:
 * 1. Creates MCP clients for all enabled servers
 * 2. Connects to each server
 * 3. Discovers tools from each server
 * 4. Adds all tools to the registry
 *
 * Failed connections are logged but don't stop other servers.
 * All clients are managed by the session (auto-cleanup).
 *
 * @param session   Session handle
 * @param config    Configuration from ac_mcp_load_config
 * @param registry  Tool registry to add discovered tools
 * @return Number of successfully connected servers
 */
size_t ac_mcp_connect_all(
    ac_session_t *session,
    const ac_mcp_servers_config_t *config,
    ac_tool_registry_t *registry
);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MCP_H */
