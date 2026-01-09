/**
 * @file tool.h
 * @brief AgentC Tool Definition and Registry
 *
 * Defines tools that can be called by the LLM during ReACT loop.
 * Similar to OpenAI Agents SDK function tools.
 */

#ifndef AGENTC_TOOL_H
#define AGENTC_TOOL_H

#include "platform.h"
#include "http_client.h"  /* for agentc_err_t */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct cJSON;  /* cJSON forward declaration */

/*============================================================================
 * Tool Parameter Definition (for JSON Schema generation)
 *============================================================================*/

typedef enum {
    AGENTC_PARAM_STRING,
    AGENTC_PARAM_INTEGER,
    AGENTC_PARAM_NUMBER,
    AGENTC_PARAM_BOOLEAN,
    AGENTC_PARAM_OBJECT,
    AGENTC_PARAM_ARRAY,
} agentc_param_type_t;

typedef struct agentc_param {
    const char *name;              /* Parameter name */
    agentc_param_type_t type;      /* Parameter type */
    const char *description;       /* Parameter description */
    int required;                  /* 1 = required, 0 = optional */
    const char *enum_values;       /* Comma-separated enum values (optional) */
    struct agentc_param *next;     /* Linked list */
} agentc_param_t;

/*============================================================================
 * Tool Call (returned by LLM)
 *============================================================================*/

typedef struct agentc_tool_call {
    char *id;                      /* Unique call ID (from LLM) */
    char *name;                    /* Function name */
    char *arguments;               /* JSON string of arguments */
    struct agentc_tool_call *next; /* Linked list for parallel calls */
} agentc_tool_call_t;

/*============================================================================
 * Tool Result
 *============================================================================*/

typedef struct agentc_tool_result {
    char *tool_call_id;            /* Corresponding call ID */
    char *output;                  /* Result string (JSON or text) */
    int is_error;                  /* 1 if this is an error result */
    struct agentc_tool_result *next;
} agentc_tool_result_t;

/*============================================================================
 * Tool Handler Function
 *============================================================================*/

/**
 * Tool execution handler.
 *
 * @param arguments  Parsed JSON arguments (cJSON object)
 * @param output     Output string (caller must free)
 * @param user_data  User context
 * @return AGENTC_OK on success, error code on failure
 */
typedef agentc_err_t (*agentc_tool_handler_t)(
    const struct cJSON *arguments,
    char **output,
    void *user_data
);

/*============================================================================
 * Tool Definition
 *============================================================================*/

typedef struct agentc_tool {
    char *name;                    /* Function name (required) */
    char *description;             /* Description for LLM (required) */
    agentc_param_t *parameters;    /* Parameter definitions (NULL = no params) */
    agentc_tool_handler_t handler; /* Execution handler (required) */
    void *user_data;               /* User context for handler */
    struct agentc_tool *next;      /* Linked list */
} agentc_tool_t;

/*============================================================================
 * Tool Registry
 *============================================================================*/

typedef struct agentc_tool_registry agentc_tool_registry_t;

/**
 * @brief Create a tool registry
 *
 * @return New registry, NULL on error
 */
agentc_tool_registry_t *agentc_tool_registry_create(void);

/**
 * @brief Destroy a tool registry
 *
 * @param registry  Registry to destroy
 */
void agentc_tool_registry_destroy(agentc_tool_registry_t *registry);

/**
 * @brief Register a tool
 *
 * The tool definition is copied internally.
 *
 * @param registry  Tool registry
 * @param tool      Tool definition
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_tool_register(
    agentc_tool_registry_t *registry,
    const agentc_tool_t *tool
);

/**
 * @brief Get tool by name
 *
 * @param registry  Tool registry
 * @param name      Tool name
 * @return Tool definition, NULL if not found
 */
const agentc_tool_t *agentc_tool_get(
    agentc_tool_registry_t *registry,
    const char *name
);

/**
 * @brief Get all tools as linked list
 *
 * @param registry  Tool registry
 * @return Head of tool list
 */
const agentc_tool_t *agentc_tool_list(agentc_tool_registry_t *registry);

/**
 * @brief Get tool count
 *
 * @param registry  Tool registry
 * @return Number of registered tools
 */
size_t agentc_tool_count(agentc_tool_registry_t *registry);

/**
 * @brief Execute a tool call
 *
 * @param registry  Tool registry
 * @param call      Tool call from LLM
 * @param result    Output result (caller must free with agentc_tool_result_free)
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_tool_execute(
    agentc_tool_registry_t *registry,
    const agentc_tool_call_t *call,
    agentc_tool_result_t *result
);

/**
 * @brief Execute multiple tool calls
 *
 * @param registry  Tool registry
 * @param calls     Linked list of tool calls
 * @param results   Output linked list of results
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_tool_execute_all(
    agentc_tool_registry_t *registry,
    const agentc_tool_call_t *calls,
    agentc_tool_result_t **results
);

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Create a parameter definition
 *
 * @param name        Parameter name
 * @param type        Parameter type
 * @param description Parameter description
 * @param required    1 = required, 0 = optional
 * @return New parameter, NULL on error
 */
agentc_param_t *agentc_param_create(
    const char *name,
    agentc_param_type_t type,
    const char *description,
    int required
);

/**
 * @brief Append parameter to list
 *
 * @param list   Pointer to list head
 * @param param  Parameter to append
 */
void agentc_param_append(agentc_param_t **list, agentc_param_t *param);

/**
 * @brief Free parameter list
 *
 * @param list  Parameter list to free
 */
void agentc_param_free(agentc_param_t *list);

/**
 * @brief Free tool call
 *
 * @param call  Tool call to free (frees entire linked list)
 */
void agentc_tool_call_free(agentc_tool_call_t *call);

/**
 * @brief Free tool result
 *
 * @param result  Tool result to free (frees entire linked list)
 */
void agentc_tool_result_free(agentc_tool_result_t *result);

/**
 * @brief Generate OpenAI-compatible tools JSON array
 *
 * @param registry  Tool registry
 * @return JSON string (caller must free), NULL on error
 */
char *agentc_tools_to_json(agentc_tool_registry_t *registry);

/**
 * @brief Get parameter type as JSON Schema string
 *
 * @param type  Parameter type
 * @return Type string ("string", "integer", etc.)
 */
const char *agentc_param_type_to_string(agentc_param_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_TOOL_H */
