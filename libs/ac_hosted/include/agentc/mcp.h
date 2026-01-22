/**
 * @file mcp.h
 * @brief Model Context Protocol (MCP) Client (Hosted Feature)
 *
 * Client implementation for connecting to MCP servers and
 * integrating their tools with AgentC.
 * 
 * This is a hosted feature requiring network and HTTP capabilities.
 */

#ifndef AGENTC_HOSTED_MCP_H
#define AGENTC_HOSTED_MCP_H

#include <agentc/platform.h>
#include <agentc/tool.h>
#include <agentc/error.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * MCP Protocol Version
 *============================================================================*/

#define AC_MCP_PROTOCOL_VERSION "2024-11-05"

/*============================================================================
 * MCP Server Configuration
 *============================================================================*/

typedef struct {
    const char *server_url;            /* MCP server URL */
    const char *transport;             /* Transport: "stdio", "http", "sse" */
    uint32_t timeout_ms;               /* Connection timeout (default: 30000) */
    const char *api_key;               /* Optional API key */
} ac_mcp_config_t;

/*============================================================================
 * MCP Client Handle
 *============================================================================*/

typedef struct ac_mcp_client ac_mcp_client_t;

/**
 * @brief Create MCP client
 *
 * Example:
 * @code
 * ac_mcp_client_t *client = ac_mcp_create(&(ac_mcp_config_t){
 *     .server_url = "http://localhost:3000",
 *     .transport = "http",
 *     .timeout_ms = 30000
 * });
 * @endcode
 *
 * @param config  MCP configuration
 * @return MCP client handle, NULL on error
 */
ac_mcp_client_t *ac_mcp_create(const ac_mcp_config_t *config);

/**
 * @brief Connect to MCP server
 *
 * Performs handshake and capability negotiation with the server.
 *
 * @param client  MCP client handle
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_connect(ac_mcp_client_t *client);

/**
 * @brief Check if client is connected
 *
 * @param client  MCP client handle
 * @return 1 if connected, 0 otherwise
 */
int ac_mcp_is_connected(const ac_mcp_client_t *client);

/**
 * @brief Disconnect from MCP server
 *
 * @param client  MCP client handle
 */
void ac_mcp_disconnect(ac_mcp_client_t *client);

/*============================================================================
 * MCP Tools
 *============================================================================*/

/**
 * @brief Discover available tools from MCP server
 *
 * Queries the server for its tool list. Tools are cached internally.
 *
 * @param client  MCP client handle
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_discover_tools(ac_mcp_client_t *client);

/**
 * @brief Register MCP tools with AgentC tool registry
 *
 * Converts MCP tools to AgentC tools and registers them.
 * Must call ac_mcp_discover_tools() first.
 *
 * @param client        MCP client handle
 * @param tool_registry AgentC tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_register_tools(
    ac_mcp_client_t *client,
    ac_tool_group_t *tool_registry
);

/**
 * @brief Get number of discovered MCP tools
 *
 * @param client  MCP client handle
 * @return Number of tools, 0 if not discovered yet
 */
size_t ac_mcp_tool_count(const ac_mcp_client_t *client);

/**
 * @brief Call an MCP tool directly
 *
 * Low-level API for calling MCP tools without going through AgentC.
 *
 * @param client    MCP client handle
 * @param tool_name Tool name
 * @param arguments JSON arguments string
 * @param result    Output result (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_call_tool(
    ac_mcp_client_t *client,
    const char *tool_name,
    const char *arguments,
    char **result
);

/*============================================================================
 * MCP Resources
 *============================================================================*/

/**
 * @brief List available resources from MCP server
 *
 * Resources are files, URLs, or other data sources provided by the server.
 *
 * @param client    MCP client handle
 * @param resources Output JSON string of resources (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_list_resources(
    ac_mcp_client_t *client,
    char **resources
);

/**
 * @brief Read a resource from MCP server
 *
 * @param client       MCP client handle
 * @param resource_uri Resource URI
 * @param content      Output content (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_read_resource(
    ac_mcp_client_t *client,
    const char *resource_uri,
    char **content
);

/*============================================================================
 * MCP Prompts
 *============================================================================*/

/**
 * @brief List available prompt templates from MCP server
 *
 * @param client  MCP client handle
 * @param prompts Output JSON string of prompts (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_list_prompts(
    ac_mcp_client_t *client,
    char **prompts
);

/**
 * @brief Get a prompt template from MCP server
 *
 * @param client      MCP client handle
 * @param prompt_name Prompt name
 * @param arguments   Optional arguments (JSON string, can be NULL)
 * @param prompt      Output prompt content (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_get_prompt(
    ac_mcp_client_t *client,
    const char *prompt_name,
    const char *arguments,
    char **prompt
);

/*============================================================================
 * MCP Sampling (AI Model Requests)
 *============================================================================*/

/**
 * @brief Request AI model completion from MCP server
 *
 * Some MCP servers can proxy AI model requests.
 *
 * @param client   MCP client handle
 * @param messages Messages JSON array
 * @param response Output response (caller must free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_mcp_create_message(
    ac_mcp_client_t *client,
    const char *messages,
    char **response
);

/*============================================================================
 * Cleanup
 *============================================================================*/

/**
 * @brief Destroy MCP client
 *
 * Automatically disconnects if still connected.
 *
 * @param client  MCP client to destroy
 */
void ac_mcp_destroy(ac_mcp_client_t *client);

/*============================================================================
 * Error Handling
 *============================================================================*/

/**
 * @brief Get last error message
 *
 * @param client  MCP client handle
 * @return Error message string (do not free), or NULL if no error
 */
const char *ac_mcp_get_error(const ac_mcp_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_HOSTED_MCP_H */
