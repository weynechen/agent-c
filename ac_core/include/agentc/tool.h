/**
 * @file tool.h
 * @brief AgentC Tool Definition and Registry
 *
 * Defines tools that can be called by the LLM during ReACT loop.
 * 
 * Future: Tools can be marked with @agentc_tool annotation for automatic
 * registration via moc (meta-object compiler).
 */

#ifndef AGENTC_TOOL_H
#define AGENTC_TOOL_H

#include "platform.h"
#include "http_client.h"
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
    AC_PARAM_STRING,
    AC_PARAM_INTEGER,
    AC_PARAM_NUMBER,
    AC_PARAM_BOOLEAN,
    AC_PARAM_OBJECT,
    AC_PARAM_ARRAY,
} ac_param_type_t;

typedef struct ac_param {
    const char *name;              /* Parameter name */
    ac_param_type_t type;          /* Parameter type */
    const char *description;       /* Parameter description */
    int required;                  /* 1 = required, 0 = optional */
    const char *enum_values;       /* Comma-separated enum values (optional) */
    struct ac_param *next;         /* Linked list */
} ac_param_t;

/*============================================================================
 * Tool Call (returned by LLM)
 *============================================================================*/

typedef struct ac_tool_call {
    char *id;                      /* Unique call ID (from LLM) */
    char *name;                    /* Function name */
    char *arguments;               /* JSON string of arguments */
    struct ac_tool_call *next;     /* Linked list for parallel calls */
} ac_tool_call_t;

/*============================================================================
 * Tool Result
 *============================================================================*/

typedef struct ac_tool_result {
    char *tool_call_id;            /* Corresponding call ID */
    char *output;                  /* Result string (JSON or text) */
    int is_error;                  /* 1 if this is an error result */
    struct ac_tool_result *next;
} ac_tool_result_t;

/*============================================================================
 * Tool Handler Function
 *============================================================================*/

/**
 * Tool execution handler.
 *
 * Example:
 * @code
 * // @agentc_tool
 * // @tools_group: weather
 * // @description: Get the current weather for a city
 * agentc_err_t tool_get_weather(const cJSON *args, char **output, void *user_data) {
 *     const char *city = cJSON_GetStringValue(cJSON_GetObjectItem(args, "city"));
 *     *output = malloc(256);
 *     snprintf(*output, 256, "The weather in %s is sunny with 25°C.", city);
 *     return AGENTC_OK;
 * }
 * @endcode
 *
 * @param arguments  Parsed JSON arguments (cJSON object)
 * @param output     Output string (caller must free)
 * @param user_data  User context
 * @return AGENTC_OK on success, error code on failure
 */
typedef agentc_err_t (*ac_tool_handler_t)(
    const struct cJSON *arguments,
    char **output,
    void *user_data
);

/*============================================================================
 * Tool Definition
 *============================================================================*/

typedef struct ac_tool {
    char *name;                    /* Function name (required) */
    char *description;             /* Description for LLM (required) */
    ac_param_t *parameters;        /* Parameter definitions (NULL = no params) */
    ac_tool_handler_t handler;     /* Execution handler (required) */
    void *user_data;               /* User context for handler */
    const char *tools_group;       /* Tool group name (optional) */
    struct ac_tool *next;          /* Linked list */
} ac_tool_t;

/*============================================================================
 * Tool Registry
 *============================================================================*/

typedef struct ac_tools ac_tools_t;

/**
 * @brief Create a tool registry
 *
 * @return New registry, NULL on error
 */
ac_tools_t *ac_tools_create(void);

/**
 * @brief Destroy a tool registry
 *
 * @param tools  Registry to destroy
 */
void ac_tools_destroy(ac_tools_t *tools);

/**
 * @brief Register a tool
 *
 * The tool definition is copied internally.
 *
 * @param tools  Tool registry
 * @param tool   Tool definition
 * @return AGENTC_OK on success
 */
agentc_err_t ac_tool_register(
    ac_tools_t *tools,
    const ac_tool_t *tool
);

/**
 * @brief Get tool by name
 *
 * @param tools  Tool registry
 * @param name   Tool name
 * @return Tool definition, NULL if not found
 */
const ac_tool_t *ac_tool_get(
    ac_tools_t *tools,
    const char *name
);

/**
 * @brief Get all tools as linked list
 *
 * @param tools  Tool registry
 * @return Head of tool list
 */
const ac_tool_t *ac_tool_list(ac_tools_t *tools);

/**
 * @brief Get tool count
 *
 * @param tools  Tool registry
 * @return Number of registered tools
 */
size_t ac_tool_count(ac_tools_t *tools);

/**
 * @brief Execute a tool call
 *
 * @param tools   Tool registry
 * @param call    Tool call from LLM
 * @param result  Output result (caller must free with ac_tool_result_free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_tool_execute(
    ac_tools_t *tools,
    const ac_tool_call_t *call,
    ac_tool_result_t *result
);

/**
 * @brief Execute multiple tool calls
 *
 * @param tools    Tool registry
 * @param calls    Linked list of tool calls
 * @param results  Output linked list of results
 * @return AGENTC_OK on success
 */
agentc_err_t ac_tool_execute_all(
    ac_tools_t *tools,
    const ac_tool_call_t *calls,
    ac_tool_result_t **results
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
ac_param_t *ac_param_create(
    const char *name,
    ac_param_type_t type,
    const char *description,
    int required
);

/**
 * @brief Append parameter to list
 *
 * @param list   Pointer to list head
 * @param param  Parameter to append
 */
void ac_param_append(ac_param_t **list, ac_param_t *param);

/**
 * @brief Free parameter list
 *
 * @param list  Parameter list to free
 */
void ac_param_free(ac_param_t *list);

/**
 * @brief Free tool call
 *
 * @param call  Tool call to free (frees entire linked list)
 */
void ac_tool_call_free(ac_tool_call_t *call);

/**
 * @brief Free tool result
 *
 * @param result  Tool result to free (frees entire linked list)
 */
void ac_tool_result_free(ac_tool_result_t *result);

/**
 * @brief Generate OpenAI-compatible tools JSON array
 *
 * @param tools  Tool registry
 * @return JSON string (caller must free), NULL on error
 */
char *ac_tools_to_json(ac_tools_t *tools);

/**
 * @brief Get parameter type as JSON Schema string
 *
 * @param type  Parameter type
 * @return Type string ("string", "integer", etc.)
 */
const char *ac_param_type_to_string(ac_param_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_TOOL_H */
