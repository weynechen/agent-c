/**
 * @file mcp.c
 * @brief MCP Client Implementation
 *
 * Implements the Model Context Protocol client for tool discovery.
 */

#include "agentc/mcp.h"
#include "agentc/log.h"
#include "agentc/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal: Session API
 *============================================================================*/

extern arena_t *ac_session_get_arena(ac_session_t *session);
extern agentc_err_t ac_session_add_mcp(ac_session_t *session, ac_mcp_client_t *client);

/*============================================================================
 * MCP Tool Info (cached after discovery)
 *============================================================================*/

typedef struct {
    char *name;
    char *description;
    char *parameters;
} mcp_tool_info_t;

/*============================================================================
 * MCP Client Structure
 *============================================================================*/

struct ac_mcp_client {
    ac_session_t *session;
    arena_t *arena;
    
    /* Configuration */
    char *server_url;
    char *transport;
    char *api_key;
    uint32_t timeout_ms;
    
    /* State */
    int connected;
    char *error_msg;
    
    /* Discovered tools */
    mcp_tool_info_t *tools;
    size_t tool_count;
    size_t tool_capacity;
    
    /* HTTP client (if using HTTP transport) */
    void *http_client;
};

/*============================================================================
 * Client Creation
 *============================================================================*/

ac_mcp_client_t *ac_mcp_create(
    ac_session_t *session,
    const ac_mcp_config_t *config
) {
    if (!session || !config || !config->server_url) {
        AC_LOG_ERROR("Invalid MCP configuration");
        return NULL;
    }
    
    arena_t *arena = ac_session_get_arena(session);
    if (!arena) {
        AC_LOG_ERROR("Failed to get session arena");
        return NULL;
    }
    
    ac_mcp_client_t *client = (ac_mcp_client_t *)arena_alloc(
        arena, sizeof(ac_mcp_client_t)
    );
    if (!client) {
        AC_LOG_ERROR("Failed to allocate MCP client");
        return NULL;
    }
    
    memset(client, 0, sizeof(ac_mcp_client_t));
    
    client->session = session;
    client->arena = arena;
    client->server_url = arena_strdup(arena, config->server_url);
    client->transport = arena_strdup(arena, config->transport ? config->transport : "http");
    client->api_key = config->api_key ? arena_strdup(arena, config->api_key) : NULL;
    client->timeout_ms = config->timeout_ms ? config->timeout_ms : 30000;
    
    if (!client->server_url || !client->transport) {
        AC_LOG_ERROR("Failed to copy MCP config");
        return NULL;
    }
    
    /* Initial tool array */
    client->tool_capacity = 16;
    client->tools = (mcp_tool_info_t *)arena_alloc(
        arena, sizeof(mcp_tool_info_t) * client->tool_capacity
    );
    if (!client->tools) {
        AC_LOG_ERROR("Failed to allocate tool array");
        return NULL;
    }
    
    /* Register with session */
    if (ac_session_add_mcp(session, client) != AGENTC_OK) {
        AC_LOG_ERROR("Failed to register MCP client with session");
        return NULL;
    }
    
    AC_LOG_INFO("MCP client created for: %s", config->server_url);
    return client;
}

/*============================================================================
 * Connection Management
 *============================================================================*/

agentc_err_t ac_mcp_connect(ac_mcp_client_t *client) {
    if (!client) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (client->connected) {
        return AGENTC_OK;
    }
    
    /* TODO: Implement actual MCP connection
     * 1. Create HTTP/SSE/stdio transport
     * 2. Send initialize request
     * 3. Negotiate capabilities
     * 4. Send initialized notification
     */
    
    AC_LOG_WARN("MCP connection not fully implemented (stub)");
    
    /* For now, mark as connected for testing */
    client->connected = 1;
    
    AC_LOG_INFO("MCP connected to: %s", client->server_url);
    return AGENTC_OK;
}

int ac_mcp_is_connected(const ac_mcp_client_t *client) {
    return client ? client->connected : 0;
}

void ac_mcp_disconnect(ac_mcp_client_t *client) {
    if (!client || !client->connected) {
        return;
    }
    
    /* TODO: Send proper disconnect notification */
    
    client->connected = 0;
    AC_LOG_INFO("MCP disconnected from: %s", client->server_url);
}

/*============================================================================
 * Tool Discovery
 *============================================================================*/

agentc_err_t ac_mcp_discover_tools(ac_mcp_client_t *client) {
    if (!client) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (!client->connected) {
        AC_LOG_ERROR("MCP client not connected");
        return AGENTC_ERR_NOT_CONNECTED;
    }
    
    /* TODO: Implement actual tools/list request
     * 1. Send JSON-RPC request: {"method": "tools/list", "params": {}}
     * 2. Parse response: {"result": {"tools": [...]}}
     * 3. Cache tool definitions
     */
    
    AC_LOG_WARN("MCP tool discovery not fully implemented (stub)");
    
    /* Clear existing tools */
    client->tool_count = 0;
    
    /* TODO: Parse actual response and populate tools array */
    
    AC_LOG_INFO("MCP discovered %zu tools", client->tool_count);
    return AGENTC_OK;
}

size_t ac_mcp_tool_count(const ac_mcp_client_t *client) {
    return client ? client->tool_count : 0;
}

/*============================================================================
 * Tool Execution
 *============================================================================*/

agentc_err_t ac_mcp_call_tool(
    ac_mcp_client_t *client,
    const char *name,
    const char *args_json,
    char **result
) {
    if (!client || !name || !result) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (!client->connected) {
        return AGENTC_ERR_NOT_CONNECTED;
    }
    
    /* TODO: Implement actual tools/call request
     * 1. Send JSON-RPC request: {"method": "tools/call", "params": {"name": "...", "arguments": {...}}}
     * 2. Parse response
     * 3. Return result
     */
    
    AC_LOG_WARN("MCP tool call not fully implemented (stub)");
    
    *result = strdup("{\"error\":\"MCP tool call not implemented\"}");
    return AGENTC_ERR_NOT_IMPLEMENTED;
}

/*============================================================================
 * Error Handling
 *============================================================================*/

const char *ac_mcp_error(const ac_mcp_client_t *client) {
    return client ? client->error_msg : NULL;
}

/*============================================================================
 * Tool Info Access (for registry integration)
 *============================================================================*/

agentc_err_t ac_mcp_get_tool_info(
    const ac_mcp_client_t *client,
    size_t index,
    const char **name,
    const char **description,
    const char **parameters
) {
    if (!client || index >= client->tool_count) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    const mcp_tool_info_t *tool = &client->tools[index];
    
    if (name) *name = tool->name;
    if (description) *description = tool->description;
    if (parameters) *parameters = tool->parameters;
    
    return AGENTC_OK;
}

/*============================================================================
 * Internal: Cleanup (called by session)
 *============================================================================*/

void ac_mcp_cleanup(ac_mcp_client_t *client) {
    if (!client) {
        return;
    }
    
    if (client->connected) {
        ac_mcp_disconnect(client);
    }
    
    /* Memory is freed when arena is destroyed */
    AC_LOG_DEBUG("MCP client cleaned up");
}
