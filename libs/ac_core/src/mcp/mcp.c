/**
 * @file mcp.c
 * @brief MCP Client Implementation - Public API
 *
 * This file contains the public MCP client API and common protocol logic.
 * Transport-specific implementations are in mcp_http.c and mcp_sse.c.
 *
 * Protocol Reference: https://modelcontextprotocol.io/
 */

#include "mcp_internal.h"
#include <stdlib.h>
#include <stdio.h>

/*============================================================================
 * HTTP Pool Integration (weak symbols for optional linking)
 *============================================================================*/

/* Weak declarations - resolved at link time if ac_hosted is linked */
__attribute__((weak)) int ac_http_pool_is_initialized(void);
__attribute__((weak)) arc_http_client_t *ac_http_pool_acquire(uint32_t timeout_ms);
__attribute__((weak)) void ac_http_pool_release(arc_http_client_t *client);

/**
 * @brief Check if HTTP pool is available and initialized
 */
static int http_pool_available(void) {
    return ac_http_pool_is_initialized && ac_http_pool_is_initialized();
}

/*============================================================================
 * Session API (External)
 *============================================================================*/

extern arena_t *ac_session_get_arena(ac_session_t *session);
extern arc_err_t ac_session_add_mcp(ac_session_t *session, ac_mcp_client_t *client);

/*============================================================================
 * Tool Info Structure
 *============================================================================*/

typedef struct {
    char *name;
    char *description;
    char *parameters;  /* JSON Schema string */
} mcp_tool_info_t;

/*============================================================================
 * MCP Client Structure
 *============================================================================*/

struct ac_mcp_client {
    ac_session_t *session;
    arena_t *arena;

    /* Transport layer (polymorphic) */
    mcp_transport_t *transport;

    /* HTTP client ownership */
    int owns_http;                 /**< 1 if we created the client, 0 if from pool */

    /* Request ID counter */
    int request_id;

    /* Client info */
    char *client_name;
    char *client_version;

    /* Server info (from initialize) */
    ac_mcp_server_info_t server_info;

    /* Tool cache */
    mcp_tool_info_t *tools;
    size_t tool_count;
    size_t tool_capacity;
};

/*============================================================================
 * Helper: Detect Transport Type
 *============================================================================*/

static int is_sse_url(const char *url) {
    if (!url) return 0;

    size_t len = strlen(url);

    /* Check for /sse suffix */
    if (len >= 4 && strcmp(url + len - 4, "/sse") == 0) return 1;
    if (len >= 5 && strcmp(url + len - 5, "/sse/") == 0) return 1;

    /* Check for /events suffix */
    if (len >= 7 && strcmp(url + len - 7, "/events") == 0) return 1;

    return 0;
}

/*============================================================================
 * JSON-RPC: Build Request
 *============================================================================*/

static char *mcp_build_request(ac_mcp_client_t *client, const char *method, cJSON *params) {
    cJSON *request = cJSON_CreateObject();
    if (!request) return NULL;

    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", ++client->request_id);
    cJSON_AddStringToObject(request, "method", method);

    /* Per JSON-RPC 2.0 spec: params may be omitted if not needed.
     * Some MCP servers (like 12306-mcp) are strict about this and
     * return -32602 "Invalid request parameters" if params is {}
     * instead of being omitted. */
    if (params) {
        cJSON_AddItemToObject(request, "params", params);
    }
    /* else: omit params field entirely */

    char *json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    return json;
}

/*============================================================================
 * JSON-RPC: Build Notification (no id field, no response expected)
 *============================================================================*/

static char *mcp_build_notification(const char *method) {
    cJSON *notification = cJSON_CreateObject();
    if (!notification) return NULL;

    cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notification, "method", method);
    /* Notifications have no id field and no params for initialized */

    char *json = cJSON_PrintUnformatted(notification);
    cJSON_Delete(notification);

    return json;
}

/*============================================================================
 * JSON-RPC: Parse Response
 *============================================================================*/

static arc_err_t mcp_parse_response(
    ac_mcp_client_t *client,
    const char *response_json,
    cJSON **result_out
) {
    cJSON *json = cJSON_Parse(response_json);
    if (!json) {
        AC_LOG_ERROR("MCP: Failed to parse response JSON");
        return ARC_ERR_PROTOCOL;
    }

    /* Check for error */
    cJSON *error = cJSON_GetObjectItem(json, "error");
    if (error && cJSON_IsObject(error)) {
        cJSON *err_code = cJSON_GetObjectItem(error, "code");
        cJSON *err_msg = cJSON_GetObjectItem(error, "message");
        AC_LOG_ERROR("MCP: RPC error %d: %s",
                     err_code ? (int)cJSON_GetNumberValue(err_code) : -1,
                     err_msg ? cJSON_GetStringValue(err_msg) : "Unknown");
        cJSON_Delete(json);
        return ARC_ERR_PROTOCOL;
    }

    /* Extract result */
    cJSON *result = cJSON_DetachItemFromObject(json, "result");
    cJSON_Delete(json);

    if (result_out) {
        *result_out = result ? result : cJSON_CreateObject();
    } else if (result) {
        cJSON_Delete(result);
    }

    return ARC_OK;
}

/*============================================================================
 * MCP RPC Call (Uses Transport)
 *============================================================================*/

static arc_err_t mcp_rpc_call(
    ac_mcp_client_t *client,
    const char *method,
    cJSON *params,
    cJSON **result_out
) {
    if (!client || !client->transport || !method) {
        if (params) cJSON_Delete(params);
        return ARC_ERR_INVALID_ARG;
    }

    /* Build request */
    char *request_json = mcp_build_request(client, method, params);
    if (!request_json) {
        AC_LOG_ERROR("MCP: Failed to build request");
        return ARC_ERR_MEMORY;
    }

    AC_LOG_DEBUG("MCP request: %s (id=%d) -> %s", method, client->request_id, request_json);

    /* Send via transport */
    char *response_json = NULL;
    arc_err_t err = client->transport->ops->request(
        client->transport,
        request_json,
        client->request_id,
        &response_json
    );

    ARC_FREE(request_json);

    if (err != ARC_OK) {
        AC_LOG_ERROR("MCP: Transport error: %s", client->transport->error_msg);
        return err;
    }

    if (!response_json) {
        AC_LOG_ERROR("MCP: No response received");
        return ARC_ERR_PROTOCOL;
    }

    AC_LOG_DEBUG("MCP response: %.500s%s",
                 response_json, strlen(response_json) > 500 ? "..." : "");

    /* Parse response */
    err = mcp_parse_response(client, response_json, result_out);
    ARC_FREE(response_json);

    return err;
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
    ac_mcp_client_t *client = (ac_mcp_client_t *)arena_alloc(arena, sizeof(ac_mcp_client_t));
    if (!client) {
        AC_LOG_ERROR("Failed to allocate MCP client");
        return NULL;
    }

    memset(client, 0, sizeof(*client));

    client->session = session;
    client->arena = arena;
    client->client_name = arena_strdup(arena, config->client_name ? config->client_name : "ArC");
    client->client_version = arena_strdup(arena, config->client_version ? config->client_version : "1.0.0");

    /* Get HTTP client: from pool or create new */
    arc_http_client_t *http = NULL;

    if (http_pool_available()) {
        /* Acquire from pool */
        http = ac_http_pool_acquire(config->timeout_ms ? config->timeout_ms : MCP_DEFAULT_TIMEOUT_MS);
        if (!http) {
            AC_LOG_ERROR("Failed to acquire HTTP client from pool");
            return NULL;
        }
        client->owns_http = 0;
        AC_LOG_DEBUG("MCP client using HTTP pool");
    } else {
        /* Create own HTTP client */
        arc_http_client_config_t http_cfg = {
            .default_timeout_ms = config->timeout_ms ? config->timeout_ms : MCP_DEFAULT_TIMEOUT_MS
        };

        arc_err_t err = arc_http_client_create(&http_cfg, &http);
        if (err != ARC_OK || !http) {
            AC_LOG_ERROR("Failed to create HTTP client: %s", ac_strerror(err));
            return NULL;
        }
        client->owns_http = 1;
        AC_LOG_DEBUG("MCP client using own HTTP client");
    }

    /* Create transport based on URL */
    int use_sse = is_sse_url(config->server_url);
    if (use_sse) {
        client->transport = mcp_sse_create(arena, http, config);
    } else {
        client->transport = mcp_http_create(arena, http, config);
    }

    if (!client->transport) {
        AC_LOG_ERROR("Failed to create transport");
        if (client->owns_http) {
            arc_http_client_destroy(http);
        } else {
            ac_http_pool_release(http);
        }
        return NULL;
    }

    /* Allocate tool array */
    client->tool_capacity = MCP_INITIAL_TOOL_CAP;
    client->tools = (mcp_tool_info_t *)arena_alloc(arena, sizeof(mcp_tool_info_t) * client->tool_capacity);
    if (!client->tools) {
        AC_LOG_ERROR("Failed to allocate tool array");
        if (client->owns_http) {
            arc_http_client_destroy(http);
        } else {
            ac_http_pool_release(http);
        }
        return NULL;
    }

    /* Register with session */
    if (ac_session_add_mcp(session, client) != ARC_OK) {
        AC_LOG_ERROR("Failed to register MCP client with session");
        if (client->owns_http) {
            arc_http_client_destroy(http);
        } else {
            ac_http_pool_release(http);
        }
        return NULL;
    }

    AC_LOG_INFO("MCP client created: %s (transport: %s)",
                config->server_url, use_sse ? "SSE" : "HTTP");

    return client;
}

/*============================================================================
 * Connection Management
 *============================================================================*/

arc_err_t ac_mcp_connect(ac_mcp_client_t *client) {
    if (!client || !client->transport) {
        return ARC_ERR_INVALID_ARG;
    }

    if (client->transport->connected) {
        return ARC_OK;
    }

    AC_LOG_INFO("MCP connecting to: %s", client->transport->server_url);

    /* Connect transport */
    arc_err_t err = client->transport->ops->connect(client->transport);
    if (err != ARC_OK) {
        return err;
    }

    /* Send initialize request */
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", MCP_PROTOCOL_VERSION);

    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", capabilities);

    cJSON *client_info = cJSON_CreateObject();
    cJSON_AddStringToObject(client_info, "name", client->client_name);
    cJSON_AddStringToObject(client_info, "version", client->client_version);
    cJSON_AddItemToObject(params, "clientInfo", client_info);

    cJSON *result = NULL;
    err = mcp_rpc_call(client, "initialize", params, &result);

    if (err != ARC_OK) {
        client->transport->ops->disconnect(client->transport);
        return err;
    }

    /* Parse server info */
    if (result) {
        cJSON *protocol = cJSON_GetObjectItem(result, "protocolVersion");
        if (protocol && cJSON_IsString(protocol)) {
            client->server_info.protocol_version = arena_strdup(
                client->arena, cJSON_GetStringValue(protocol)
            );
        }

        cJSON *server_info = cJSON_GetObjectItem(result, "serverInfo");
        if (server_info && cJSON_IsObject(server_info)) {
            cJSON *name = cJSON_GetObjectItem(server_info, "name");
            cJSON *version = cJSON_GetObjectItem(server_info, "version");

            if (name && cJSON_IsString(name)) {
                client->server_info.name = arena_strdup(client->arena, cJSON_GetStringValue(name));
            }
            if (version && cJSON_IsString(version)) {
                client->server_info.version = arena_strdup(client->arena, cJSON_GetStringValue(version));
            }
        }
        cJSON_Delete(result);
    }

    /* Send initialized notification (no id, no response expected)
     * Per MCP spec, this notification is REQUIRED after initialize succeeds.
     * Some servers (like 12306-mcp) may reject subsequent requests without it. */
    char *notif_json = mcp_build_notification("notifications/initialized");
    if (notif_json) {
        AC_LOG_DEBUG("MCP sending: notifications/initialized -> %s", notif_json);
        char *response = NULL;
        /* Send notification; we don't expect a meaningful response but some
         * servers may return an empty response or error which we ignore */
        arc_err_t notif_err = client->transport->ops->request(
            client->transport, notif_json, 0, &response);
        if (notif_err != ARC_OK) {
            AC_LOG_DEBUG("initialized notification send status: %s (may be ignored)",
                        ac_strerror(notif_err));
        }
        if (response) {
            AC_LOG_DEBUG("initialized notification response: %s", response);
            ARC_FREE(response);
        }
        ARC_FREE(notif_json);
    }

    AC_LOG_INFO("MCP connected: server=%s %s, protocol=%s",
                client->server_info.name ? client->server_info.name : "unknown",
                client->server_info.version ? client->server_info.version : "",
                client->server_info.protocol_version ? client->server_info.protocol_version : "unknown");

    return ARC_OK;
}

int ac_mcp_is_connected(const ac_mcp_client_t *client) {
    return client && client->transport && client->transport->connected;
}

const ac_mcp_server_info_t *ac_mcp_server_info(const ac_mcp_client_t *client) {
    return (client && client->transport && client->transport->connected) ? &client->server_info : NULL;
}

void ac_mcp_disconnect(ac_mcp_client_t *client) {
    if (!client || !client->transport || !client->transport->connected) {
        return;
    }

    client->transport->ops->disconnect(client->transport);
    AC_LOG_INFO("MCP disconnected from: %s", client->transport->server_url);
}

/*============================================================================
 * Tool Discovery
 *============================================================================*/

arc_err_t ac_mcp_discover_tools(ac_mcp_client_t *client) {
    if (!client) {
        return ARC_ERR_INVALID_ARG;
    }

    if (!ac_mcp_is_connected(client)) {
        AC_LOG_ERROR("MCP: Not connected");
        return ARC_ERR_NOT_CONNECTED;
    }

    AC_LOG_INFO("MCP discovering tools...");

    cJSON *result = NULL;
    arc_err_t err = mcp_rpc_call(client, "tools/list", NULL, &result);

    if (err != ARC_OK) {
        return err;
    }

    /* Clear existing */
    client->tool_count = 0;

    /* Parse tools array */
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    if (!tools || !cJSON_IsArray(tools)) {
        AC_LOG_WARN("No tools array in response");
        cJSON_Delete(result);
        return ARC_OK;
    }

    int array_size = cJSON_GetArraySize(tools);

    /* Grow if needed */
    if ((size_t)array_size > client->tool_capacity) {
        size_t new_cap = array_size + MCP_INITIAL_TOOL_CAP;
        mcp_tool_info_t *new_tools = (mcp_tool_info_t *)arena_alloc(
            client->arena, sizeof(mcp_tool_info_t) * new_cap
        );
        if (!new_tools) {
            AC_LOG_ERROR("Failed to allocate tool array");
            cJSON_Delete(result);
            return ARC_ERR_MEMORY;
        }
        client->tools = new_tools;
        client->tool_capacity = new_cap;
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

        if (input_schema) {
            char *schema_str = cJSON_PrintUnformatted(input_schema);
            if (schema_str) {
                tool->parameters = arena_strdup(client->arena, schema_str);
                ARC_FREE(schema_str);
            }
        } else {
            tool->parameters = arena_strdup(client->arena, "{\"type\":\"object\",\"properties\":{}}");
        }

        if (!tool->name) continue;

        client->tool_count++;
        AC_LOG_DEBUG("Discovered tool: %s", tool->name);
    }

    cJSON_Delete(result);

    AC_LOG_INFO("MCP discovered %zu tools", client->tool_count);
    return ARC_OK;
}

size_t ac_mcp_tool_count(const ac_mcp_client_t *client) {
    return client ? client->tool_count : 0;
}

/*============================================================================
 * Tool Execution
 *============================================================================*/

arc_err_t ac_mcp_call_tool(
    ac_mcp_client_t *client,
    const char *name,
    const char *args_json,
    char **result_out
) {
    if (!client || !name || !result_out) {
        return ARC_ERR_INVALID_ARG;
    }

    *result_out = NULL;

    if (!ac_mcp_is_connected(client)) {
        *result_out = ARC_STRDUP("{\"error\":\"MCP not connected\"}");
        return ARC_ERR_NOT_CONNECTED;
    }

    AC_LOG_INFO("MCP calling tool: %s", name);

    /* Build params */
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", name);

    cJSON *arguments = NULL;
    if (args_json && strlen(args_json) > 0) {
        arguments = cJSON_Parse(args_json);
        if (!arguments) {
            AC_LOG_WARN("Failed to parse args, using empty object");
            arguments = cJSON_CreateObject();
        }
    } else {
        arguments = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(params, "arguments", arguments);

    cJSON *result = NULL;
    arc_err_t err = mcp_rpc_call(client, "tools/call", params, &result);

    if (err != ARC_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"error\":\"Tool call failed: %s\"}", ac_strerror(err));
        *result_out = ARC_STRDUP(buf);
        return err;
    }

    /* Parse content */
    cJSON *content = cJSON_GetObjectItem(result, "content");
    if (!content || !cJSON_IsArray(content)) {
        *result_out = ARC_STRDUP("{\"result\":null}");
        cJSON_Delete(result);
        return ARC_OK;
    }

    /* Concatenate text content */
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
        *result_out = cJSON_PrintUnformatted(result);
        cJSON_Delete(result);
        return ARC_OK;
    }

    char *text_result = (char *)ARC_MALLOC(total_len + 64);
    if (!text_result) {
        cJSON_Delete(result);
        return ARC_ERR_MEMORY;
    }

    char *p = text_result;
    int first = 1;

    cJSON_ArrayForEach(item, content) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *text = cJSON_GetObjectItem(item, "text");

        if (type && cJSON_IsString(type) &&
            strcmp(cJSON_GetStringValue(type), "text") == 0 &&
            text && cJSON_IsString(text)) {
            if (!first) *p++ = '\n';
            first = 0;
            const char *txt = cJSON_GetStringValue(text);
            strcpy(p, txt);
            p += strlen(txt);
        }
    }
    *p = '\0';

    cJSON_Delete(result);

    /* Wrap in JSON */
    cJSON *json_result = cJSON_CreateObject();
    cJSON_AddStringToObject(json_result, "result", text_result);
    ARC_FREE(text_result);

    *result_out = cJSON_PrintUnformatted(json_result);
    cJSON_Delete(json_result);

    return ARC_OK;
}

/*============================================================================
 * Error Handling
 *============================================================================*/

const char *ac_mcp_error(const ac_mcp_client_t *client) {
    return (client && client->transport) ? client->transport->error_msg : NULL;
}

/*============================================================================
 * Tool Info Access
 *============================================================================*/

arc_err_t ac_mcp_get_tool_info(
    const ac_mcp_client_t *client,
    size_t index,
    const char **name,
    const char **description,
    const char **parameters
) {
    if (!client || index >= client->tool_count) {
        return ARC_ERR_INVALID_ARG;
    }

    const mcp_tool_info_t *tool = &client->tools[index];

    if (name) *name = tool->name;
    if (description) *description = tool->description;
    if (parameters) *parameters = tool->parameters;

    return ARC_OK;
}

/*============================================================================
 * Cleanup
 *============================================================================*/

void ac_mcp_cleanup(ac_mcp_client_t *client) {
    if (!client) return;

    if (client->transport) {
        if (client->transport->connected) {
            client->transport->ops->disconnect(client->transport);
        }

        /* Release or destroy HTTP client based on ownership */
        if (client->transport->http) {
            if (client->owns_http) {
                arc_http_client_destroy(client->transport->http);
            } else {
                ac_http_pool_release(client->transport->http);
            }
        }

        client->transport->ops->destroy(client->transport);
    }

    AC_LOG_DEBUG("MCP client cleaned up");
}

/*============================================================================
 * Multi-Server Configuration
 *============================================================================*/

#define MCP_DEFAULT_CONFIG_FILE ".mcp.json"
#define MCP_MAX_SERVERS 32

typedef struct {
    char *name;
    char *url;
    char *api_key;
    uint32_t timeout_ms;
    int enabled;
} mcp_server_entry_t;

struct ac_mcp_servers_config {
    mcp_server_entry_t *servers;
    size_t count;
    size_t enabled_count;
};

ac_mcp_servers_config_t *ac_mcp_load_config(const char *path) {
    const char *config_path = path ? path : MCP_DEFAULT_CONFIG_FILE;

    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        AC_LOG_DEBUG("MCP config file not found: %s", config_path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        AC_LOG_ERROR("MCP config file too large or empty: %s", config_path);
        fclose(fp);
        return NULL;
    }

    char *content = (char *)ARC_MALLOC(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(content, 1, size, fp);
    fclose(fp);
    content[read_size] = '\0';

    cJSON *root = cJSON_Parse(content);
    ARC_FREE(content);

    if (!root) {
        AC_LOG_ERROR("Failed to parse MCP config: %s", config_path);
        return NULL;
    }

    cJSON *servers = cJSON_GetObjectItem(root, "servers");
    if (!servers || !cJSON_IsArray(servers)) {
        AC_LOG_ERROR("MCP config missing 'servers' array");
        cJSON_Delete(root);
        return NULL;
    }

    int array_size = cJSON_GetArraySize(servers);
    if (array_size <= 0) {
        AC_LOG_WARN("MCP config has no servers");
        cJSON_Delete(root);
        return NULL;
    }

    if (array_size > MCP_MAX_SERVERS) {
        AC_LOG_WARN("MCP config has too many servers (%d), limiting to %d",
                    array_size, MCP_MAX_SERVERS);
        array_size = MCP_MAX_SERVERS;
    }

    ac_mcp_servers_config_t *config = (ac_mcp_servers_config_t *)ARC_CALLOC(
        1, sizeof(ac_mcp_servers_config_t)
    );
    if (!config) {
        cJSON_Delete(root);
        return NULL;
    }

    config->servers = (mcp_server_entry_t *)ARC_CALLOC(array_size, sizeof(mcp_server_entry_t));
    if (!config->servers) {
        ARC_FREE(config);
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *server_json;
    int index = 0;
    cJSON_ArrayForEach(server_json, servers) {
        if (index >= array_size) break;

        cJSON *url = cJSON_GetObjectItem(server_json, "url");
        if (!url || !cJSON_IsString(url)) {
            AC_LOG_WARN("MCP server entry missing 'url', skipping");
            continue;
        }

        mcp_server_entry_t *entry = &config->servers[config->count];

        entry->url = ARC_STRDUP(cJSON_GetStringValue(url));
        if (!entry->url) continue;

        cJSON *name = cJSON_GetObjectItem(server_json, "name");
        if (name && cJSON_IsString(name)) {
            entry->name = ARC_STRDUP(cJSON_GetStringValue(name));
        }

        cJSON *api_key = cJSON_GetObjectItem(server_json, "api_key");
        if (api_key && cJSON_IsString(api_key)) {
            entry->api_key = ARC_STRDUP(cJSON_GetStringValue(api_key));
        }

        cJSON *timeout = cJSON_GetObjectItem(server_json, "timeout_ms");
        if (timeout && cJSON_IsNumber(timeout)) {
            entry->timeout_ms = (uint32_t)cJSON_GetNumberValue(timeout);
        }

        cJSON *enabled = cJSON_GetObjectItem(server_json, "enabled");
        if (enabled && cJSON_IsBool(enabled)) {
            entry->enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        } else {
            entry->enabled = 1;
        }

        if (entry->enabled) {
            config->enabled_count++;
        }

        config->count++;
        index++;

        AC_LOG_DEBUG("MCP config: %s (%s) - %s",
                     entry->name ? entry->name : "unnamed",
                     entry->url,
                     entry->enabled ? "enabled" : "disabled");
    }

    cJSON_Delete(root);

    AC_LOG_INFO("Loaded MCP config: %zu servers (%zu enabled)",
                config->count, config->enabled_count);

    return config;
}

size_t ac_mcp_config_server_count(const ac_mcp_servers_config_t *config) {
    return config ? config->count : 0;
}

size_t ac_mcp_config_enabled_count(const ac_mcp_servers_config_t *config) {
    return config ? config->enabled_count : 0;
}

void ac_mcp_config_free(ac_mcp_servers_config_t *config) {
    if (!config) return;

    if (config->servers) {
        for (size_t i = 0; i < config->count; i++) {
            mcp_server_entry_t *entry = &config->servers[i];
            if (entry->name) ARC_FREE(entry->name);
            if (entry->url) ARC_FREE(entry->url);
            if (entry->api_key) ARC_FREE(entry->api_key);
        }
        ARC_FREE(config->servers);
    }

    ARC_FREE(config);
}

/* Forward declaration */
arc_err_t ac_tool_registry_add_mcp(
    ac_tool_registry_t *registry,
    ac_mcp_client_t *mcp
);

size_t ac_mcp_connect_all(
    ac_session_t *session,
    const ac_mcp_servers_config_t *config,
    ac_tool_registry_t *registry
) {
    if (!session || !config || !registry) {
        return 0;
    }

    size_t connected = 0;

    for (size_t i = 0; i < config->count; i++) {
        const mcp_server_entry_t *entry = &config->servers[i];

        if (!entry->enabled) {
            AC_LOG_DEBUG("Skipping disabled MCP server: %s",
                         entry->name ? entry->name : entry->url);
            continue;
        }

        const char *server_name = entry->name ? entry->name : entry->url;
        AC_LOG_INFO("Connecting to MCP server: %s", server_name);

        ac_mcp_client_t *client = ac_mcp_create(session, &(ac_mcp_config_t){
            .server_url = entry->url,
            .api_key = entry->api_key,
            .timeout_ms = entry->timeout_ms ? entry->timeout_ms : MCP_DEFAULT_TIMEOUT_MS,
            .verify_ssl = 1
        });

        if (!client) {
            AC_LOG_WARN("Failed to create MCP client for: %s", server_name);
            continue;
        }

        arc_err_t err = ac_mcp_connect(client);
        if (err != ARC_OK) {
            AC_LOG_WARN("Failed to connect to MCP server %s: %s",
                        server_name, ac_strerror(err));
            continue;
        }

        err = ac_mcp_discover_tools(client);
        if (err != ARC_OK) {
            AC_LOG_WARN("Failed to discover tools from %s: %s",
                        server_name, ac_strerror(err));
            continue;
        }

        size_t tool_count = ac_mcp_tool_count(client);

        err = ac_tool_registry_add_mcp(registry, client);
        if (err != ARC_OK) {
            AC_LOG_WARN("Failed to add tools from %s: %s",
                        server_name, ac_strerror(err));
            continue;
        }

        connected++;
        AC_LOG_INFO("MCP server %s: connected, %zu tools added",
                    server_name, tool_count);
    }

    AC_LOG_INFO("MCP connect_all: %zu/%zu servers connected",
                connected, config->enabled_count);

    return connected;
}
