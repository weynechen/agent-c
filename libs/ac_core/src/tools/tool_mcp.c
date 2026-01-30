/**
 * @file tool_mcp.c
 * @brief MCP Tool Integration
 *
 * Bridges MCP tools to the unified tool registry.
 */

#include "arc/tool.h"
#include "arc/mcp.h"
#include "arc/log.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * MCP Tool Wrapper Data
 *============================================================================*/

typedef struct {
    ac_mcp_client_t *client;
    char *tool_name;
} mcp_wrapper_data_t;

/*============================================================================
 * MCP Tool Executor
 *============================================================================*/

/**
 * @brief Execute an MCP tool
 *
 * This function is called when an MCP tool is invoked through the registry.
 * It proxies the call to the MCP server.
 */
static char *mcp_tool_execute(
    const ac_tool_ctx_t *ctx,
    const char *args_json,
    void *priv
) {
    (void)ctx;  /* MCP doesn't use local context */

    mcp_wrapper_data_t *data = (mcp_wrapper_data_t *)priv;
    if (!data || !data->client || !data->tool_name) {
        return strdup("{\"error\":\"Invalid MCP tool data\"}");
    }

    char *result = NULL;
    arc_err_t err = ac_mcp_call_tool(
        data->client,
        data->tool_name,
        args_json ? args_json : "{}",
        &result
    );

    if (err != ARC_OK) {
        AC_LOG_ERROR("MCP tool call failed: %s (err=%d)", data->tool_name, err);
        if (!result) {
            result = strdup("{\"error\":\"MCP tool call failed\"}");
        }
    }

    return result;
}

/*============================================================================
 * Registry Integration
 *============================================================================*/

/* Forward declaration - get arena from registry */
extern arena_t *ac_tool_registry_get_arena(const ac_tool_registry_t *registry);

arc_err_t ac_tool_registry_add_mcp(
    ac_tool_registry_t *registry,
    ac_mcp_client_t *client
) {
    if (!registry || !client) {
        return ARC_ERR_INVALID_ARG;
    }

    size_t tool_count = ac_mcp_tool_count(client);
    if (tool_count == 0) {
        AC_LOG_WARN("No MCP tools to add");
        return ARC_OK;
    }

    arena_t *arena = ac_tool_registry_get_arena(registry);
    if (!arena) {
        AC_LOG_ERROR("Failed to get registry arena");
        return ARC_ERR_INVALID_ARG;
    }

    AC_LOG_INFO("Adding %zu MCP tools to registry", tool_count);

    for (size_t i = 0; i < tool_count; i++) {
        const char *name = NULL;
        const char *description = NULL;
        const char *parameters = NULL;

        arc_err_t err = ac_mcp_get_tool_info(
            client, i, &name, &description, &parameters
        );

        if (err != ARC_OK || !name) {
            AC_LOG_WARN("Failed to get MCP tool info at index %zu", i);
            continue;
        }

        /* Create wrapper data */
        mcp_wrapper_data_t *wrapper_data = (mcp_wrapper_data_t *)arena_alloc(
            arena, sizeof(mcp_wrapper_data_t)
        );
        if (!wrapper_data) {
            AC_LOG_ERROR("Failed to allocate MCP wrapper data");
            continue;
        }

        wrapper_data->client = client;
        wrapper_data->tool_name = arena_strdup(arena, name);

        if (!wrapper_data->tool_name) {
            AC_LOG_ERROR("Failed to copy MCP tool name");
            continue;
        }

        /* Create tool definition */
        ac_tool_t tool = {
            .name = name,
            .description = description,
            .parameters = parameters,
            .execute = mcp_tool_execute,
            .priv = wrapper_data
        };

        err = ac_tool_registry_add(registry, &tool);
        if (err != ARC_OK) {
            AC_LOG_WARN("Failed to add MCP tool: %s", name);
        }
    }

    AC_LOG_INFO("MCP tools added to registry");
    return ARC_OK;
}
