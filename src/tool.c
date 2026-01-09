/**
 * @file tool.c
 * @brief Tool definition and registry implementation
 */

#include "agentc/tool.h"
#include "agentc/platform.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal Structures
 *============================================================================*/

struct agentc_tool_registry {
    agentc_tool_t *tools;         /* Linked list of tools */
    size_t count;                 /* Number of tools */
};

/*============================================================================
 * Parameter Type Helpers
 *============================================================================*/

const char *agentc_param_type_to_string(agentc_param_type_t type) {
    switch (type) {
        case AGENTC_PARAM_STRING:  return "string";
        case AGENTC_PARAM_INTEGER: return "integer";
        case AGENTC_PARAM_NUMBER:  return "number";
        case AGENTC_PARAM_BOOLEAN: return "boolean";
        case AGENTC_PARAM_OBJECT:  return "object";
        case AGENTC_PARAM_ARRAY:   return "array";
        default:                   return "string";
    }
}

/*============================================================================
 * Parameter Helpers
 *============================================================================*/

agentc_param_t *agentc_param_create(
    const char *name,
    agentc_param_type_t type,
    const char *description,
    int required
) {
    if (!name) return NULL;

    agentc_param_t *param = AGENTC_CALLOC(1, sizeof(agentc_param_t));
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

void agentc_param_append(agentc_param_t **list, agentc_param_t *param) {
    if (!list || !param) return;

    if (!*list) {
        *list = param;
        return;
    }

    agentc_param_t *tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = param;
}

void agentc_param_free(agentc_param_t *list) {
    while (list) {
        agentc_param_t *next = list->next;
        AGENTC_FREE((void *)list->name);
        AGENTC_FREE((void *)list->description);
        AGENTC_FREE((void *)list->enum_values);
        AGENTC_FREE(list);
        list = next;
    }
}

/* Deep copy a parameter list */
static agentc_param_t *param_clone(const agentc_param_t *src) {
    agentc_param_t *head = NULL;
    agentc_param_t *tail = NULL;

    while (src) {
        agentc_param_t *copy = agentc_param_create(
            src->name, src->type, src->description, src->required
        );
        if (!copy) {
            agentc_param_free(head);
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

void agentc_tool_call_free(agentc_tool_call_t *call) {
    while (call) {
        agentc_tool_call_t *next = call->next;
        AGENTC_FREE(call->id);
        AGENTC_FREE(call->name);
        AGENTC_FREE(call->arguments);
        AGENTC_FREE(call);
        call = next;
    }
}

agentc_tool_call_t *agentc_tool_call_clone(const agentc_tool_call_t *calls) {
    agentc_tool_call_t *head = NULL;
    agentc_tool_call_t *tail = NULL;

    while (calls) {
        agentc_tool_call_t *copy = AGENTC_CALLOC(1, sizeof(agentc_tool_call_t));
        if (!copy) {
            agentc_tool_call_free(head);
            return NULL;
        }

        copy->id = calls->id ? AGENTC_STRDUP(calls->id) : NULL;
        copy->name = calls->name ? AGENTC_STRDUP(calls->name) : NULL;
        copy->arguments = calls->arguments ? AGENTC_STRDUP(calls->arguments) : NULL;
        copy->next = NULL;

        if (!head) {
            head = copy;
            tail = copy;
        } else {
            tail->next = copy;
            tail = copy;
        }

        calls = calls->next;
    }

    return head;
}

/*============================================================================
 * Tool Result Helpers
 *============================================================================*/

void agentc_tool_result_free(agentc_tool_result_t *result) {
    while (result) {
        agentc_tool_result_t *next = result->next;
        AGENTC_FREE(result->tool_call_id);
        AGENTC_FREE(result->output);
        AGENTC_FREE(result);
        result = next;
    }
}

/*============================================================================
 * Tool Registry
 *============================================================================*/

agentc_tool_registry_t *agentc_tool_registry_create(void) {
    agentc_tool_registry_t *registry = AGENTC_CALLOC(1, sizeof(agentc_tool_registry_t));
    if (!registry) return NULL;

    registry->tools = NULL;
    registry->count = 0;

    return registry;
}

static void tool_free(agentc_tool_t *tool) {
    while (tool) {
        agentc_tool_t *next = tool->next;
        AGENTC_FREE(tool->name);
        AGENTC_FREE(tool->description);
        agentc_param_free(tool->parameters);
        AGENTC_FREE(tool);
        tool = next;
    }
}

void agentc_tool_registry_destroy(agentc_tool_registry_t *registry) {
    if (!registry) return;

    tool_free(registry->tools);
    AGENTC_FREE(registry);
}

agentc_err_t agentc_tool_register(
    agentc_tool_registry_t *registry,
    const agentc_tool_t *tool
) {
    if (!registry || !tool || !tool->name || !tool->handler) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Check for duplicate */
    if (agentc_tool_get(registry, tool->name)) {
        AGENTC_LOG_WARN("Tool '%s' already registered, skipping", tool->name);
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Create copy */
    agentc_tool_t *copy = AGENTC_CALLOC(1, sizeof(agentc_tool_t));
    if (!copy) return AGENTC_ERR_NO_MEMORY;

    copy->name = AGENTC_STRDUP(tool->name);
    copy->description = tool->description ? AGENTC_STRDUP(tool->description) : NULL;
    copy->parameters = tool->parameters ? param_clone(tool->parameters) : NULL;
    copy->handler = tool->handler;
    copy->user_data = tool->user_data;
    copy->next = NULL;

    if (!copy->name) {
        tool_free(copy);
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Append to list */
    if (!registry->tools) {
        registry->tools = copy;
    } else {
        agentc_tool_t *tail = registry->tools;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = copy;
    }

    registry->count++;
    AGENTC_LOG_INFO("Registered tool: %s", copy->name);

    return AGENTC_OK;
}

const agentc_tool_t *agentc_tool_get(
    agentc_tool_registry_t *registry,
    const char *name
) {
    if (!registry || !name) return NULL;

    for (agentc_tool_t *t = registry->tools; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }

    return NULL;
}

const agentc_tool_t *agentc_tool_list(agentc_tool_registry_t *registry) {
    if (!registry) return NULL;
    return registry->tools;
}

size_t agentc_tool_count(agentc_tool_registry_t *registry) {
    if (!registry) return 0;
    return registry->count;
}

/*============================================================================
 * Tool Execution
 *============================================================================*/

agentc_err_t agentc_tool_execute(
    agentc_tool_registry_t *registry,
    const agentc_tool_call_t *call,
    agentc_tool_result_t *result
) {
    if (!registry || !call || !result) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->tool_call_id = call->id ? AGENTC_STRDUP(call->id) : NULL;

    /* Find tool */
    const agentc_tool_t *tool = agentc_tool_get(registry, call->name);
    if (!tool) {
        AGENTC_LOG_ERROR("Tool not found: %s", call->name);
        result->is_error = 1;
        result->output = AGENTC_STRDUP("{\"error\": \"tool not found\"}");
        return AGENTC_OK;  /* Not a fatal error */
    }

    /* Parse arguments */
    cJSON *args = NULL;
    if (call->arguments && strlen(call->arguments) > 0) {
        args = cJSON_Parse(call->arguments);
        if (!args) {
            AGENTC_LOG_ERROR("Failed to parse arguments for tool: %s", call->name);
            result->is_error = 1;
            result->output = AGENTC_STRDUP("{\"error\": \"invalid arguments JSON\"}");
            return AGENTC_OK;
        }
    } else {
        args = cJSON_CreateObject();
    }

    /* Execute handler */
    AGENTC_LOG_DEBUG("Executing tool: %s", call->name);
    char *output = NULL;
    agentc_err_t err = tool->handler(args, &output, tool->user_data);

    cJSON_Delete(args);

    if (err != AGENTC_OK) {
        AGENTC_LOG_ERROR("Tool execution failed: %s (error %d)", call->name, err);
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

    AGENTC_LOG_DEBUG("Tool result: %s", result->output);
    return AGENTC_OK;
}

agentc_err_t agentc_tool_execute_all(
    agentc_tool_registry_t *registry,
    const agentc_tool_call_t *calls,
    agentc_tool_result_t **results
) {
    if (!registry || !results) {
        return AGENTC_ERR_INVALID_ARG;
    }

    *results = NULL;
    agentc_tool_result_t *tail = NULL;

    for (const agentc_tool_call_t *call = calls; call; call = call->next) {
        agentc_tool_result_t *result = AGENTC_CALLOC(1, sizeof(agentc_tool_result_t));
        if (!result) {
            agentc_tool_result_free(*results);
            *results = NULL;
            return AGENTC_ERR_NO_MEMORY;
        }

        agentc_err_t err = agentc_tool_execute(registry, call, result);
        if (err != AGENTC_OK) {
            AGENTC_FREE(result);
            agentc_tool_result_free(*results);
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

static cJSON *param_to_json_schema(const agentc_param_t *params) {
    cJSON *properties = cJSON_CreateObject();
    cJSON *required = cJSON_CreateArray();

    for (const agentc_param_t *p = params; p; p = p->next) {
        cJSON *prop = cJSON_CreateObject();
        cJSON_AddStringToObject(prop, "type", agentc_param_type_to_string(p->type));

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

char *agentc_tools_to_json(agentc_tool_registry_t *registry) {
    if (!registry) return NULL;

    cJSON *tools = cJSON_CreateArray();

    for (const agentc_tool_t *t = registry->tools; t; t = t->next) {
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
        cJSON_AddItemToArray(tools, tool);
    }

    char *json = cJSON_PrintUnformatted(tools);
    cJSON_Delete(tools);

    return json;
}
