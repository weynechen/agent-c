/**
 * @file mcp.c
 * @brief MCP Client Implementation
 *
 * Implements the Model Context Protocol client over HTTP/HTTPS.
 * Uses JSON-RPC 2.0 for communication.
 *
 * Protocol Reference: https://modelcontextprotocol.io/
 */

#include "agentc/mcp.h"
#include "agentc/log.h"
#include "agentc/arena.h"
#include "http_client.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define MCP_PROTOCOL_VERSION    "2024-11-05"
#define MCP_DEFAULT_TIMEOUT_MS  30000
#define MCP_INITIAL_TOOL_CAP    16

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
    char *parameters;      /* JSON Schema string */
} mcp_tool_info_t;

/*============================================================================
 * MCP Client Structure
 *============================================================================*/

struct ac_mcp_client {
    ac_session_t *session;
    arena_t *arena;
    
    /* Configuration */
    char *server_url;
    char *api_key;
    char *client_name;
    char *client_version;
    uint32_t timeout_ms;
    int verify_ssl;
    
    /* HTTP client */
    agentc_http_client_t *http;
    
    /* State */
    int connected;
    int request_id;            /* JSON-RPC request ID counter */
    char *error_msg;
    
    /* Server info (from initialize) */
    ac_mcp_server_info_t server_info;
    
    /* Discovered tools */
    mcp_tool_info_t *tools;
    size_t tool_count;
    size_t tool_capacity;
};

/*============================================================================
 * Internal: Set Error Message
 *============================================================================*/

static void mcp_set_error(ac_mcp_client_t *client, const char *fmt, ...) {
    if (!client) return;
    
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    if (client->error_msg) {
        /* Previous error in arena, just overwrite pointer */
    }
    client->error_msg = arena_strdup(client->arena, buf);
    AC_LOG_ERROR("MCP: %s", buf);
}

/*============================================================================
 * Internal: JSON-RPC Request/Response
 *============================================================================*/

/**
 * @brief Build JSON-RPC 2.0 request
 */
static char *mcp_build_request(ac_mcp_client_t *client, const char *method, cJSON *params) {
    cJSON *request = cJSON_CreateObject();
    if (!request) return NULL;
    
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", ++client->request_id);
    cJSON_AddStringToObject(request, "method", method);
    
    if (params) {
        cJSON_AddItemToObject(request, "params", params);
    } else {
        cJSON_AddItemToObject(request, "params", cJSON_CreateObject());
    }
    
    char *json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    
    return json;
}

/**
 * @brief Send JSON-RPC request and parse response
 *
 * @param client      MCP client
 * @param method      RPC method name
 * @param params      Request params (ownership transferred)
 * @param result_out  Output: result object (caller must cJSON_Delete)
 * @return AGENTC_OK on success
 */
static agentc_err_t mcp_rpc_call(
    ac_mcp_client_t *client,
    const char *method,
    cJSON *params,
    cJSON **result_out
) {
    if (!client || !method) {
        if (params) cJSON_Delete(params);
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Build request JSON */
    char *request_json = mcp_build_request(client, method, params);
    if (!request_json) {
        mcp_set_error(client, "Failed to build JSON-RPC request");
        return AGENTC_ERR_MEMORY;
    }
    
    AC_LOG_DEBUG("MCP request: %s -> %.200s%s", method, request_json,
                 strlen(request_json) > 200 ? "..." : "");
    
    /* Build HTTP request */
    agentc_http_header_t *headers = NULL;
    
    /* Content-Type header */
    agentc_http_header_t *ct_header = agentc_http_header_create("Content-Type", "application/json");
    agentc_http_header_append(&headers, ct_header);
    
    /* Accept header - MCP Streamable HTTP requires both JSON and SSE */
    agentc_http_header_t *accept_header = agentc_http_header_create(
        "Accept", "application/json, text/event-stream"
    );
    agentc_http_header_append(&headers, accept_header);
    
    /* Authorization header (if API key provided) */
    if (client->api_key) {
        char auth_value[512];
        snprintf(auth_value, sizeof(auth_value), "Bearer %s", client->api_key);
        agentc_http_header_t *auth_header = agentc_http_header_create("Authorization", auth_value);
        agentc_http_header_append(&headers, auth_header);
    }
    
    agentc_http_request_t request = {
        .url = client->server_url,
        .method = AGENTC_HTTP_POST,
        .headers = headers,
        .body = request_json,
        .body_len = strlen(request_json),
        .timeout_ms = client->timeout_ms,
        .verify_ssl = client->verify_ssl
    };
    
    agentc_http_response_t response = {0};
    
    /* Send request */
    agentc_err_t err = agentc_http_request(client->http, &request, &response);
    
    /* Cleanup request */
    free(request_json);
    agentc_http_header_free(headers);
    
    if (err != AGENTC_OK) {
        mcp_set_error(client, "HTTP request failed: %s", 
                      response.error_msg ? response.error_msg : ac_strerror(err));
        agentc_http_response_free(&response);
        return err;
    }
    
    /* Check HTTP status */
    if (response.status_code < 200 || response.status_code >= 300) {
        mcp_set_error(client, "HTTP error %d: %s", response.status_code,
                      response.body ? response.body : "No body");
        agentc_http_response_free(&response);
        return AGENTC_ERR_HTTP;
    }
    
    if (!response.body || response.body_len == 0) {
        mcp_set_error(client, "Empty response from server");
        agentc_http_response_free(&response);
        return AGENTC_ERR_PROTOCOL;
    }
    
    AC_LOG_DEBUG("MCP response: %.200s%s", response.body,
                 response.body_len > 200 ? "..." : "");
    
    /* Parse JSON-RPC response */
    cJSON *json = cJSON_Parse(response.body);
    agentc_http_response_free(&response);
    
    if (!json) {
        mcp_set_error(client, "Failed to parse JSON response");
        return AGENTC_ERR_PROTOCOL;
    }
    
    /* Check for JSON-RPC error */
    cJSON *error = cJSON_GetObjectItem(json, "error");
    if (error && cJSON_IsObject(error)) {
        cJSON *err_code = cJSON_GetObjectItem(error, "code");
        cJSON *err_msg = cJSON_GetObjectItem(error, "message");
        mcp_set_error(client, "RPC error %d: %s",
                      err_code ? (int)cJSON_GetNumberValue(err_code) : -1,
                      err_msg ? cJSON_GetStringValue(err_msg) : "Unknown error");
        cJSON_Delete(json);
        return AGENTC_ERR_PROTOCOL;
    }
    
    /* Extract result */
    cJSON *result = cJSON_DetachItemFromObject(json, "result");
    cJSON_Delete(json);
    
    if (!result) {
        mcp_set_error(client, "Missing result in response");
        return AGENTC_ERR_PROTOCOL;
    }
    
    if (result_out) {
        *result_out = result;
    } else {
        cJSON_Delete(result);
    }
    
    return AGENTC_OK;
}

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
    
    /* Allocate client */
    ac_mcp_client_t *client = (ac_mcp_client_t *)arena_alloc(
        arena, sizeof(ac_mcp_client_t)
    );
    if (!client) {
        AC_LOG_ERROR("Failed to allocate MCP client");
        return NULL;
    }
    
    memset(client, 0, sizeof(ac_mcp_client_t));
    
    /* Initialize fields */
    client->session = session;
    client->arena = arena;
    client->server_url = arena_strdup(arena, config->server_url);
    client->api_key = config->api_key ? arena_strdup(arena, config->api_key) : NULL;
    client->client_name = arena_strdup(arena, config->client_name ? config->client_name : "AgentC");
    client->client_version = arena_strdup(arena, config->client_version ? config->client_version : "1.0.0");
    client->timeout_ms = config->timeout_ms ? config->timeout_ms : MCP_DEFAULT_TIMEOUT_MS;
    client->verify_ssl = config->verify_ssl;
    client->request_id = 0;
    
    if (!client->server_url || !client->client_name || !client->client_version) {
        AC_LOG_ERROR("Failed to copy MCP config");
        return NULL;
    }
    
    /* Create HTTP client */
    agentc_http_client_config_t http_config = {
        .default_timeout_ms = client->timeout_ms
    };
    
    agentc_err_t err = agentc_http_client_create(&http_config, &client->http);
    if (err != AGENTC_OK || !client->http) {
        AC_LOG_ERROR("Failed to create HTTP client: %s", ac_strerror(err));
        return NULL;
    }
    
    /* Allocate tool array */
    client->tool_capacity = MCP_INITIAL_TOOL_CAP;
    client->tools = (mcp_tool_info_t *)arena_alloc(
        arena, sizeof(mcp_tool_info_t) * client->tool_capacity
    );
    if (!client->tools) {
        AC_LOG_ERROR("Failed to allocate tool array");
        agentc_http_client_destroy(client->http);
        return NULL;
    }
    
    /* Register with session */
    if (ac_session_add_mcp(session, client) != AGENTC_OK) {
        AC_LOG_ERROR("Failed to register MCP client with session");
        agentc_http_client_destroy(client->http);
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
    
    AC_LOG_INFO("MCP connecting to: %s", client->server_url);
    
    /*
     * Step 1: Send initialize request
     *
     * Request:
     * {
     *   "jsonrpc": "2.0",
     *   "id": 1,
     *   "method": "initialize",
     *   "params": {
     *     "protocolVersion": "2024-11-05",
     *     "capabilities": {},
     *     "clientInfo": {
     *       "name": "AgentC",
     *       "version": "1.0.0"
     *     }
     *   }
     * }
     */
    
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", MCP_PROTOCOL_VERSION);
    
    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", capabilities);
    
    cJSON *client_info = cJSON_CreateObject();
    cJSON_AddStringToObject(client_info, "name", client->client_name);
    cJSON_AddStringToObject(client_info, "version", client->client_version);
    cJSON_AddItemToObject(params, "clientInfo", client_info);
    
    cJSON *result = NULL;
    agentc_err_t err = mcp_rpc_call(client, "initialize", params, &result);
    
    if (err != AGENTC_OK) {
        return err;
    }
    
    /*
     * Parse initialize response:
     * {
     *   "protocolVersion": "2024-11-05",
     *   "capabilities": {...},
     *   "serverInfo": {
     *     "name": "my-server",
     *     "version": "1.0.0"
     *   }
     * }
     */
    
    cJSON *protocol_version = cJSON_GetObjectItem(result, "protocolVersion");
    if (protocol_version && cJSON_IsString(protocol_version)) {
        client->server_info.protocol_version = arena_strdup(
            client->arena, cJSON_GetStringValue(protocol_version)
        );
    }
    
    cJSON *server_info = cJSON_GetObjectItem(result, "serverInfo");
    if (server_info && cJSON_IsObject(server_info)) {
        cJSON *name = cJSON_GetObjectItem(server_info, "name");
        cJSON *version = cJSON_GetObjectItem(server_info, "version");
        
        if (name && cJSON_IsString(name)) {
            client->server_info.name = arena_strdup(
                client->arena, cJSON_GetStringValue(name)
            );
        }
        if (version && cJSON_IsString(version)) {
            client->server_info.version = arena_strdup(
                client->arena, cJSON_GetStringValue(version)
            );
        }
    }
    
    cJSON_Delete(result);
    
    /*
     * Step 2: Send initialized notification
     *
     * This is a notification (no id), so we don't expect a response.
     * For simplicity, we send it as a regular request and ignore the response.
     */
    
    AC_LOG_DEBUG("Sending initialized notification");
    
    /* For notification, we still use RPC call but ignore result */
    err = mcp_rpc_call(client, "notifications/initialized", NULL, NULL);
    if (err != AGENTC_OK) {
        /* Some servers may not support notifications over HTTP, ignore error */
        AC_LOG_WARN("Initialized notification failed (may be ignored): %s", ac_strerror(err));
    }
    
    client->connected = 1;
    
    AC_LOG_INFO("MCP connected to %s (server: %s %s, protocol: %s)",
                client->server_url,
                client->server_info.name ? client->server_info.name : "unknown",
                client->server_info.version ? client->server_info.version : "",
                client->server_info.protocol_version ? client->server_info.protocol_version : "unknown");
    
    return AGENTC_OK;
}

int ac_mcp_is_connected(const ac_mcp_client_t *client) {
    return client ? client->connected : 0;
}

const ac_mcp_server_info_t *ac_mcp_server_info(const ac_mcp_client_t *client) {
    return (client && client->connected) ? &client->server_info : NULL;
}

void ac_mcp_disconnect(ac_mcp_client_t *client) {
    if (!client || !client->connected) {
        return;
    }
    
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
        mcp_set_error(client, "Not connected");
        return AGENTC_ERR_NOT_CONNECTED;
    }
    
    AC_LOG_INFO("MCP discovering tools...");
    
    /*
     * Send tools/list request
     *
     * Request:
     * {
     *   "jsonrpc": "2.0",
     *   "id": 2,
     *   "method": "tools/list",
     *   "params": {}
     * }
     *
     * Response:
     * {
     *   "tools": [
     *     {
     *       "name": "read_file",
     *       "description": "Read file contents",
     *       "inputSchema": {
     *         "type": "object",
     *         "properties": {
     *           "path": { "type": "string" }
     *         },
     *         "required": ["path"]
     *       }
     *     }
     *   ]
     * }
     */
    
    cJSON *result = NULL;
    agentc_err_t err = mcp_rpc_call(client, "tools/list", NULL, &result);
    
    if (err != AGENTC_OK) {
        return err;
    }
    
    /* Clear existing tools */
    client->tool_count = 0;
    
    /* Parse tools array */
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    if (!tools || !cJSON_IsArray(tools)) {
        AC_LOG_WARN("No tools array in response");
        cJSON_Delete(result);
        return AGENTC_OK;
    }
    
    int array_size = cJSON_GetArraySize(tools);
    
    /* Grow tool array if needed */
    if ((size_t)array_size > client->tool_capacity) {
        size_t new_capacity = array_size + MCP_INITIAL_TOOL_CAP;
        mcp_tool_info_t *new_tools = (mcp_tool_info_t *)arena_alloc(
            client->arena, sizeof(mcp_tool_info_t) * new_capacity
        );
        if (!new_tools) {
            mcp_set_error(client, "Failed to allocate tool array");
            cJSON_Delete(result);
            return AGENTC_ERR_MEMORY;
        }
        client->tools = new_tools;
        client->tool_capacity = new_capacity;
    }
    
    /* Parse each tool */
    cJSON *tool_json;
    cJSON_ArrayForEach(tool_json, tools) {
        if (!cJSON_IsObject(tool_json)) continue;
        
        cJSON *name = cJSON_GetObjectItem(tool_json, "name");
        cJSON *description = cJSON_GetObjectItem(tool_json, "description");
        cJSON *input_schema = cJSON_GetObjectItem(tool_json, "inputSchema");
        
        if (!name || !cJSON_IsString(name)) {
            AC_LOG_WARN("Tool missing name, skipping");
            continue;
        }
        
        mcp_tool_info_t *tool = &client->tools[client->tool_count];
        
        tool->name = arena_strdup(client->arena, cJSON_GetStringValue(name));
        tool->description = description && cJSON_IsString(description) ?
            arena_strdup(client->arena, cJSON_GetStringValue(description)) : NULL;
        
        /* Convert inputSchema to string */
        if (input_schema) {
            char *schema_str = cJSON_PrintUnformatted(input_schema);
            if (schema_str) {
                tool->parameters = arena_strdup(client->arena, schema_str);
                free(schema_str);
            } else {
                tool->parameters = NULL;
            }
        } else {
            tool->parameters = arena_strdup(client->arena, 
                "{\"type\":\"object\",\"properties\":{}}");
        }
        
        if (!tool->name) {
            AC_LOG_WARN("Failed to copy tool name");
            continue;
        }
        
        client->tool_count++;
        AC_LOG_DEBUG("Discovered tool: %s", tool->name);
    }
    
    cJSON_Delete(result);
    
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
    char **result_out
) {
    if (!client || !name || !result_out) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    *result_out = NULL;
    
    if (!client->connected) {
        *result_out = strdup("{\"error\":\"MCP not connected\"}");
        return AGENTC_ERR_NOT_CONNECTED;
    }
    
    AC_LOG_INFO("MCP calling tool: %s", name);
    
    /*
     * Send tools/call request
     *
     * Request:
     * {
     *   "jsonrpc": "2.0",
     *   "id": 3,
     *   "method": "tools/call",
     *   "params": {
     *     "name": "read_file",
     *     "arguments": { "path": "/etc/hosts" }
     *   }
     * }
     *
     * Response:
     * {
     *   "content": [
     *     { "type": "text", "text": "file contents..." }
     *   ]
     * }
     */
    
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", name);
    
    /* Parse arguments JSON */
    cJSON *arguments = NULL;
    if (args_json && strlen(args_json) > 0) {
        arguments = cJSON_Parse(args_json);
        if (!arguments) {
            AC_LOG_WARN("Failed to parse args JSON, using empty object");
            arguments = cJSON_CreateObject();
        }
    } else {
        arguments = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(params, "arguments", arguments);
    
    cJSON *result = NULL;
    agentc_err_t err = mcp_rpc_call(client, "tools/call", params, &result);
    
    if (err != AGENTC_OK) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), 
                 "{\"error\":\"%s\"}", 
                 client->error_msg ? client->error_msg : "Tool call failed");
        *result_out = strdup(error_buf);
        return err;
    }
    
    /*
     * Parse response content
     *
     * MCP tools return content as an array:
     * {
     *   "content": [
     *     { "type": "text", "text": "..." },
     *     { "type": "image", "data": "...", "mimeType": "..." }
     *   ]
     * }
     *
     * For simplicity, we concatenate all text content.
     */
    
    cJSON *content = cJSON_GetObjectItem(result, "content");
    if (!content || !cJSON_IsArray(content)) {
        /* No content, return empty result */
        *result_out = strdup("{\"result\":null}");
        cJSON_Delete(result);
        return AGENTC_OK;
    }
    
    /* Build result by concatenating text content */
    size_t total_len = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, content) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *text = cJSON_GetObjectItem(item, "text");
        
        if (type && cJSON_IsString(type) && 
            strcmp(cJSON_GetStringValue(type), "text") == 0 &&
            text && cJSON_IsString(text)) {
            total_len += strlen(cJSON_GetStringValue(text)) + 1;
        }
    }
    
    if (total_len == 0) {
        /* No text content, return the whole result as JSON */
        *result_out = cJSON_PrintUnformatted(result);
        cJSON_Delete(result);
        return AGENTC_OK;
    }
    
    /* Concatenate text content */
    char *text_result = (char *)malloc(total_len + 64);
    if (!text_result) {
        cJSON_Delete(result);
        return AGENTC_ERR_MEMORY;
    }
    
    char *p = text_result;
    int first = 1;
    
    cJSON_ArrayForEach(item, content) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *text = cJSON_GetObjectItem(item, "text");
        
        if (type && cJSON_IsString(type) && 
            strcmp(cJSON_GetStringValue(type), "text") == 0 &&
            text && cJSON_IsString(text)) {
            if (!first) {
                *p++ = '\n';
            }
            first = 0;
            const char *txt = cJSON_GetStringValue(text);
            strcpy(p, txt);
            p += strlen(txt);
        }
    }
    *p = '\0';
    
    cJSON_Delete(result);
    
    /* Wrap in JSON result */
    cJSON *json_result = cJSON_CreateObject();
    cJSON_AddStringToObject(json_result, "result", text_result);
    free(text_result);
    
    *result_out = cJSON_PrintUnformatted(json_result);
    cJSON_Delete(json_result);
    
    AC_LOG_DEBUG("MCP tool %s returned: %.100s%s", name, *result_out,
                 strlen(*result_out) > 100 ? "..." : "");
    
    return AGENTC_OK;
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
    
    /* Destroy HTTP client */
    if (client->http) {
        agentc_http_client_destroy(client->http);
        client->http = NULL;
    }
    
    /* Memory is freed when arena is destroyed */
    AC_LOG_DEBUG("MCP client cleaned up");
}
