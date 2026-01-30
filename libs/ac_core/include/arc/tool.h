/**
 * @file tool.h
 * @brief Unified Tool Interface and Registry
 *
 * Provides:
 * - Tool definition with unified signature
 * - Tool registry for builtin and MCP tools
 * - Macros for easy tool selection
 *
 * Tools are registered to a registry, and the registry is passed to an agent.
 * The registry lifecycle is managed by the session.
 */

#ifndef ARC_TOOL_H
#define ARC_TOOL_H

#include "arena.h"
#include "error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct ac_tool_registry ac_tool_registry_t;
typedef struct ac_session ac_session_t;
typedef struct ac_mcp_client ac_mcp_client_t;

/*============================================================================
 * Tool Execution Context
 *============================================================================*/

/**
 * @brief Context passed to tool execution
 */
typedef struct {
    const char *session_id;          /* Current session ID */
    const char *working_dir;         /* Working directory */
    void *user_data;                 /* User-provided context */
} ac_tool_ctx_t;

/*============================================================================
 * Tool Function Signature
 *============================================================================*/

/**
 * @brief Unified tool execution function signature
 *
 * All tools (builtin and MCP) use this signature.
 *
 * @param ctx        Execution context (can be NULL)
 * @param args_json  JSON string of arguments
 * @param priv       Tool-specific private data
 * @return JSON result string (caller must free), NULL on error
 */
typedef char* (*ac_tool_fn)(
    const ac_tool_ctx_t *ctx,
    const char *args_json,
    void *priv
);

/*============================================================================
 * Tool Definition
 *============================================================================*/

/**
 * @brief Tool definition structure
 *
 * MOC generates tools in this format:
 * @code
 * const ac_tool_t TOOL_read_file = {
 *     .name = "read_file",
 *     .description = "Read contents of a file",
 *     .parameters = "{\"type\":\"object\",...}",
 *     .execute = exec_read_file,
 *     .priv = NULL
 * };
 * @endcode
 */
typedef struct {
    const char *name;                /* Unique tool identifier */
    const char *description;         /* Description for LLM */
    const char *parameters;          /* JSON Schema string */
    ac_tool_fn execute;              /* Execution function */
    void *priv;                      /* Private data (for MCP, etc.) */
} ac_tool_t;

/*============================================================================
 * Tool Registry Creation
 *============================================================================*/

/**
 * @brief Create a tool registry within a session
 *
 * The registry lifecycle is managed by the session.
 * When the session closes, all registries are automatically destroyed.
 *
 * @param session  Session handle
 * @return Registry handle, NULL on error
 *
 * Example:
 * @code
 * ac_session_t *session = ac_session_open();
 * ac_tool_registry_t *tools = ac_tool_registry_create(session);
 *
 * ac_tool_registry_add_array(tools, AC_TOOLS(read_file, bash));
 *
 * ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
 *     .tools = tools,
 *     ...
 * });
 *
 * // Use agent...
 *
 * ac_session_close(session);  // Cleans up everything
 * @endcode
 */
ac_tool_registry_t *ac_tool_registry_create(ac_session_t *session);

/*============================================================================
 * Tool Registration
 *============================================================================*/

/**
 * @brief Add a single tool to registry
 *
 * @param registry  Tool registry
 * @param tool      Tool definition (copied)
 * @return ARC_OK on success
 */
arc_err_t ac_tool_registry_add(
    ac_tool_registry_t *registry,
    const ac_tool_t *tool
);

/**
 * @brief Add multiple tools from NULL-terminated array
 *
 * @param registry  Tool registry
 * @param tools     NULL-terminated array of tool pointers
 * @return ARC_OK on success
 *
 * Example:
 * @code
 * ac_tool_registry_add_array(tools, AC_TOOLS(read_file, write_file, bash));
 * @endcode
 */
arc_err_t ac_tool_registry_add_array(
    ac_tool_registry_t *registry,
    const ac_tool_t **tools
);

/*============================================================================
 * MCP Integration
 *============================================================================*/

/**
 * @brief Add tools from MCP client
 *
 * Adds all discovered tools from a connected MCP client.
 * The MCP client must have called ac_mcp_discover_tools() first.
 *
 * @param registry  Tool registry
 * @param client    Connected MCP client with discovered tools
 * @return ARC_OK on success
 */
arc_err_t ac_tool_registry_add_mcp(
    ac_tool_registry_t *registry,
    ac_mcp_client_t *client
);

/*============================================================================
 * Tool Query & Execution
 *============================================================================*/

/**
 * @brief Find tool by name
 *
 * @param registry  Tool registry
 * @param name      Tool name
 * @return Tool pointer, NULL if not found
 */
const ac_tool_t *ac_tool_registry_find(
    const ac_tool_registry_t *registry,
    const char *name
);

/**
 * @brief Get tool count
 */
size_t ac_tool_registry_count(const ac_tool_registry_t *registry);

/**
 * @brief Execute a tool by name
 *
 * @param registry   Tool registry
 * @param name       Tool name
 * @param args_json  JSON arguments
 * @param ctx        Execution context (can be NULL)
 * @return Result JSON (caller must free), NULL on error
 */
char *ac_tool_registry_call(
    ac_tool_registry_t *registry,
    const char *name,
    const char *args_json,
    const ac_tool_ctx_t *ctx
);

/**
 * @brief Build OpenAI-compatible tools JSON schema
 *
 * Generates JSON array of tool definitions for LLM API.
 *
 * @param registry  Tool registry
 * @return JSON array string (caller must free), NULL if empty
 */
char *ac_tool_registry_schema(const ac_tool_registry_t *registry);

/*============================================================================
 * Tool Selection Macros (for MOC-generated tools)
 *============================================================================*/

/**
 * @brief Reference a MOC-generated tool by function name
 *
 * MOC generates: const ac_tool_t TOOL_xxx = {...};
 *
 * Usage:
 * @code
 * ac_tool_registry_add(registry, AC_TOOL(read_file));
 * @endcode
 */
#define AC_TOOL(func) (&TOOL_##func)

/**
 * @brief Create NULL-terminated tool array for batch registration
 *
 * Usage:
 * @code
 * ac_tool_registry_add_array(registry, AC_TOOLS(read_file, write_file, bash));
 * @endcode
 */
#define AC_TOOLS(...) ((const ac_tool_t*[]){ AC_TOOLS_EXPAND(__VA_ARGS__), NULL })

/*============================================================================
 * Internal Macros (do not use directly)
 *============================================================================*/

#define AC_TOOLS_EXPAND(...) AC_TOOLS_MAP(AC_TOOL, __VA_ARGS__)

#define AC_TOOLS_MAP(m, ...) AC_TOOLS_MAP_IMPL(AC_NARG(__VA_ARGS__), m, __VA_ARGS__)
#define AC_TOOLS_MAP_IMPL(N, m, ...) AC_TOOLS_MAP_IMPL2(N, m, __VA_ARGS__)
#define AC_TOOLS_MAP_IMPL2(N, m, ...) AC_TOOLS_MAP_##N(m, __VA_ARGS__)

#define AC_NARG(...) AC_NARG_IMPL(__VA_ARGS__, 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define AC_NARG_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N

#define AC_TOOLS_MAP_1(m, a) m(a)
#define AC_TOOLS_MAP_2(m, a, ...) m(a), AC_TOOLS_MAP_1(m, __VA_ARGS__)
#define AC_TOOLS_MAP_3(m, a, ...) m(a), AC_TOOLS_MAP_2(m, __VA_ARGS__)
#define AC_TOOLS_MAP_4(m, a, ...) m(a), AC_TOOLS_MAP_3(m, __VA_ARGS__)
#define AC_TOOLS_MAP_5(m, a, ...) m(a), AC_TOOLS_MAP_4(m, __VA_ARGS__)
#define AC_TOOLS_MAP_6(m, a, ...) m(a), AC_TOOLS_MAP_5(m, __VA_ARGS__)
#define AC_TOOLS_MAP_7(m, a, ...) m(a), AC_TOOLS_MAP_6(m, __VA_ARGS__)
#define AC_TOOLS_MAP_8(m, a, ...) m(a), AC_TOOLS_MAP_7(m, __VA_ARGS__)
#define AC_TOOLS_MAP_9(m, a, ...) m(a), AC_TOOLS_MAP_8(m, __VA_ARGS__)
#define AC_TOOLS_MAP_10(m, a, ...) m(a), AC_TOOLS_MAP_9(m, __VA_ARGS__)
#define AC_TOOLS_MAP_11(m, a, ...) m(a), AC_TOOLS_MAP_10(m, __VA_ARGS__)
#define AC_TOOLS_MAP_12(m, a, ...) m(a), AC_TOOLS_MAP_11(m, __VA_ARGS__)
#define AC_TOOLS_MAP_13(m, a, ...) m(a), AC_TOOLS_MAP_12(m, __VA_ARGS__)
#define AC_TOOLS_MAP_14(m, a, ...) m(a), AC_TOOLS_MAP_13(m, __VA_ARGS__)
#define AC_TOOLS_MAP_15(m, a, ...) m(a), AC_TOOLS_MAP_14(m, __VA_ARGS__)
#define AC_TOOLS_MAP_16(m, a, ...) m(a), AC_TOOLS_MAP_15(m, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ARC_TOOL_H */
