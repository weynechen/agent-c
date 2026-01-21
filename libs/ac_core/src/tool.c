/**
 * @file tool.c
 * @brief Tool definition and registry implementation
 */

#include "agentc/tool.h"
#include "agentc/log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal Structures
 *============================================================================*/

struct ac_tools {
    ac_tool_t *tools;         /* Linked list of tools */
    size_t count;             /* Number of tools */
};

/*============================================================================
 * Parameter Type Helpers
 *============================================================================*/

const char *ac_param_type_to_string(ac_param_type_t type) {
    switch (type) {
        case AC_PARAM_STRING:  return "string";
        case AC_PARAM_INTEGER: return "integer";
        case AC_PARAM_NUMBER:  return "number";
        case AC_PARAM_BOOLEAN: return "boolean";
        case AC_PARAM_OBJECT:  return "object";
        case AC_PARAM_ARRAY:   return "array";
        default:               return "string";
    }
}

/*============================================================================
 * Parameter Helpers
 *============================================================================*/

ac_param_t *ac_param_create(
    const char *name,
    ac_param_type_t type,
    const char *description,
    int required
) {
    if (!name) return NULL;

    ac_param_t *param = AGENTC_CALLOC(1, sizeof(ac_param_t));
    if (!param) return NULL;

    param->name = AGENTC_STRDUP(name);
    param->type = type;
    param->description = description ? AGENTC_STRDUP(description) : NULL;
    param->required = required;
    param->enum_values = NULL;
    param->next = NULL;

    if (!param->name) {
        AGENTC_FREE(param);
        return NULL;
    }

    return param;
}

void ac_param_append(ac_param_t **list, ac_param_t *param) {
    if (!list || !param) return;

    if (!*list) {
        *list = param;
        return;
    }

    ac_param_t *tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = param;
}

void ac_param_free(ac_param_t *list) {
    while (list) {
        ac_param_t *next = list->next;
        AGENTC_FREE((void *)list->name);
        AGENTC_FREE((void *)list->description);
        AGENTC_FREE((void *)list->enum_values);
        AGENTC_FREE(list);
        list = next;
    }
}

/* Deep copy a parameter list */
static ac_param_t *param_clone(const ac_param_t *src) {
    ac_param_t *head = NULL;
    ac_param_t *tail = NULL;

    while (src) {
        ac_param_t *copy = ac_param_create(
            src->name, src->type, src->description, src->required
        );
        if (!copy) {
            ac_param_free(head);
            return NULL;
        }

        if (src->enum_values) {
            copy->enum_values = AGENTC_STRDUP(src->enum_values);
        }

        if (!head) {
            head = copy;
            tail = copy;
        } else {
            tail->next = copy;
            tail = copy;
        }

        src = src->next;
    }

    return head;
}

/*============================================================================
 * Tool Call Helpers
 *============================================================================*/

void ac_tool_call_free(ac_tool_call_t *call) {
    while (call) {
        ac_tool_call_t *next = call->next;
        AGENTC_FREE(call->id);
        AGENTC_FREE(call->name);
        AGENTC_FREE(call->arguments);
        AGENTC_FREE(call);
        call = next;
    }
}

/*============================================================================
 * Tool Result Helpers
 *============================================================================*/

void ac_tool_result_free(ac_tool_result_t *result) {
    while (result) {
        ac_tool_result_t *next = result->next;
        AGENTC_FREE(result->tool_call_id);
        AGENTC_FREE(result->output);
        AGENTC_FREE(result);
        result = next;
    }
}

/*============================================================================
 * Tool Registry
 *============================================================================*/

ac_tools_t *ac_tools_create(void) {
    ac_tools_t *tools = AGENTC_CALLOC(1, sizeof(ac_tools_t));
    if (!tools) return NULL;

    tools->tools = NULL;
    tools->count = 0;

    return tools;
}

static void tool_free(ac_tool_t *tool) {
    while (tool) {
        ac_tool_t *next = tool->next;
        AGENTC_FREE(tool->name);
        AGENTC_FREE(tool->description);
        AGENTC_FREE((void*)tool->tools_group);
        ac_param_free(tool->parameters);
        AGENTC_FREE(tool);
        tool = next;
    }
}

void ac_tools_destroy(ac_tools_t *tools) {
    if (!tools) return;

    tool_free(tools->tools);
    AGENTC_FREE(tools);
}

agentc_err_t ac_tool_register(
    ac_tools_t *tools,
    const ac_tool_t *tool
) {
    if (!tools || !tool || !tool->name || !tool->handler) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Check for duplicate */
    if (ac_tool_get(tools, tool->name)) {
        AC_LOG_WARN("Tool '%s' already registered, skipping", tool->name);
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Create copy */
    ac_tool_t *copy = AGENTC_CALLOC(1, sizeof(ac_tool_t));
    if (!copy) return AGENTC_ERR_NO_MEMORY;

    copy->name = AGENTC_STRDUP(tool->name);
    copy->description = tool->description ? AGENTC_STRDUP(tool->description) : NULL;
    copy->parameters = tool->parameters ? param_clone(tool->parameters) : NULL;
    copy->handler = tool->handler;
    copy->user_data = tool->user_data;
    copy->tools_group = tool->tools_group ? tool->tools_group : NULL;
    copy->next = NULL;

    if (!copy->name) {
        tool_free(copy);
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Append to list */
    if (!tools->tools) {
        tools->tools = copy;
    } else {
        ac_tool_t *tail = tools->tools;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = copy;
    }

    tools->count++;
    AC_LOG_INFO("Registered tool: %s", copy->name);

    return AGENTC_OK;
}

const ac_tool_t *ac_tool_get(
    ac_tools_t *tools,
    const char *name
) {
    if (!tools || !name) return NULL;

    for (ac_tool_t *t = tools->tools; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }

    return NULL;
}

const ac_tool_t *ac_tool_list(ac_tools_t *tools) {
    if (!tools) return NULL;
    return tools->tools;
}

size_t ac_tool_count(ac_tools_t *tools) {
    if (!tools) return 0;
    return tools->count;
}

/*============================================================================
 * Tool Execution
 *============================================================================*/

agentc_err_t ac_tool_execute(
    ac_tools_t *tools,
    const ac_tool_call_t *call,
    ac_tool_result_t *result
) {
    if (!tools || !call || !result) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->tool_call_id = call->id ? AGENTC_STRDUP(call->id) : NULL;

    /* Find tool */
    const ac_tool_t *tool = ac_tool_get(tools, call->name);
    if (!tool) {
        AC_LOG_ERROR("Tool not found: %s", call->name);
        result->is_error = 1;
        result->output = AGENTC_STRDUP("{\"error\": \"tool not found\"}");
        return AGENTC_OK;  /* Not a fatal error */
    }

    /* Parse arguments */
    cJSON *args = NULL;
    if (call->arguments && strlen(call->arguments) > 0) {
        args = cJSON_Parse(call->arguments);
        if (!args) {
            AC_LOG_ERROR("Failed to parse arguments for tool: %s", call->name);
            result->is_error = 1;
            result->output = AGENTC_STRDUP("{\"error\": \"invalid arguments JSON\"}");
            return AGENTC_OK;
        }
    } else {
        args = cJSON_CreateObject();
    }

    /* Execute handler */
    AC_LOG_DEBUG("Executing tool: %s", call->name);
    char *output = NULL;
    agentc_err_t err = tool->handler(args, &output, tool->user_data);

    cJSON_Delete(args);

    if (err != AGENTC_OK) {
        AC_LOG_ERROR("Tool execution failed: %s (error %d)", call->name, err);
        result->is_error = 1;
        if (output) {
            result->output = output;
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "{\"error\": \"execution failed with code %d\"}", err);
            result->output = AGENTC_STRDUP(buf);
        }
        return AGENTC_OK;
    }

    result->output = output ? output : AGENTC_STRDUP("{}");
    result->is_error = 0;

    AC_LOG_DEBUG("Tool result: %s", result->output);
    return AGENTC_OK;
}

agentc_err_t ac_tool_execute_all(
    ac_tools_t *tools,
    const ac_tool_call_t *calls,
    ac_tool_result_t **results
) {
    if (!tools || !results) {
        return AGENTC_ERR_INVALID_ARG;
    }

    *results = NULL;
    ac_tool_result_t *tail = NULL;

    for (const ac_tool_call_t *call = calls; call; call = call->next) {
        ac_tool_result_t *result = AGENTC_CALLOC(1, sizeof(ac_tool_result_t));
        if (!result) {
            ac_tool_result_free(*results);
            *results = NULL;
            return AGENTC_ERR_NO_MEMORY;
        }

        agentc_err_t err = ac_tool_execute(tools, call, result);
        if (err != AGENTC_OK) {
            AGENTC_FREE(result);
            ac_tool_result_free(*results);
            *results = NULL;
            return err;
        }

        /* Append to results list */
        if (!*results) {
            *results = result;
            tail = result;
        } else {
            tail->next = result;
            tail = result;
        }
    }

    return AGENTC_OK;
}

/*============================================================================
 * JSON Schema Generation
 *============================================================================*/

static cJSON *param_to_json_schema(const ac_param_t *params) {
    cJSON *properties = cJSON_CreateObject();
    cJSON *required = cJSON_CreateArray();

    for (const ac_param_t *p = params; p; p = p->next) {
        cJSON *prop = cJSON_CreateObject();
        cJSON_AddStringToObject(prop, "type", ac_param_type_to_string(p->type));

        if (p->description) {
            cJSON_AddStringToObject(prop, "description", p->description);
        }

        /* Handle enum values */
        if (p->enum_values && strlen(p->enum_values) > 0) {
            cJSON *enum_arr = cJSON_CreateArray();
            char *values = AGENTC_STRDUP(p->enum_values);
            char *token = strtok(values, ",");
            while (token) {
                /* Trim whitespace */
                while (*token == ' ') token++;
                char *end = token + strlen(token) - 1;
                while (end > token && *end == ' ') *end-- = '\0';

                cJSON_AddItemToArray(enum_arr, cJSON_CreateString(token));
                token = strtok(NULL, ",");
            }
            AGENTC_FREE(values);
            cJSON_AddItemToObject(prop, "enum", enum_arr);
        }

        cJSON_AddItemToObject(properties, p->name, prop);

        if (p->required) {
            cJSON_AddItemToArray(required, cJSON_CreateString(p->name));
        }
    }

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddItemToObject(schema, "properties", properties);

    if (cJSON_GetArraySize(required) > 0) {
        cJSON_AddItemToObject(schema, "required", required);
    } else {
        cJSON_Delete(required);
    }

    cJSON_AddBoolToObject(schema, "additionalProperties", 0);

    return schema;
}

char *ac_tools_to_json(ac_tools_t *tools) {
    if (!tools) return NULL;

    cJSON *tools_arr = cJSON_CreateArray();

    for (const ac_tool_t *t = tools->tools; t; t = t->next) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");

        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", t->name);

        if (t->description) {
            cJSON_AddStringToObject(func, "description", t->description);
        }

        /* Parameters schema */
        if (t->parameters) {
            cJSON *params = param_to_json_schema(t->parameters);
            cJSON_AddItemToObject(func, "parameters", params);
        } else {
            /* Empty parameters */
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "type", "object");
            cJSON_AddItemToObject(params, "properties", cJSON_CreateObject());
            cJSON_AddBoolToObject(params, "additionalProperties", 0);
            cJSON_AddItemToObject(func, "parameters", params);
        }

        cJSON_AddItemToObject(tool, "function", func);
        cJSON_AddItemToArray(tools_arr, tool);
    }

    char *json = cJSON_PrintUnformatted(tools_arr);
    cJSON_Delete(tools_arr);

    return json;
}
