/**
 * @file tool.c
 * @brief Tool Registry Implementation
 *
 * Provides dynamic tool registration for builtin and MCP tools.
 */

#include "arc/tool.h"
#include "arc/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cJSON.h"

/*============================================================================
 * Constants
 *============================================================================*/

#define INITIAL_CAPACITY 16
#define GROWTH_FACTOR 2

/*============================================================================
 * Tool Registry Structure
 *============================================================================*/

struct ac_tool_registry {
    ac_session_t *session;           /* Owning session */
    arena_t *arena;                  /* Arena for allocations */

    ac_tool_t *tools;                /* Dynamic array of tools */
    size_t count;                    /* Current tool count */
    size_t capacity;                 /* Array capacity */
};

/*============================================================================
 * Internal: Session Arena Access
 *============================================================================*/

/* Declared in session.c */
extern arena_t *ac_session_get_arena(ac_session_t *session);
extern arc_err_t ac_session_add_registry(ac_session_t *session, ac_tool_registry_t *registry);

/*============================================================================
 * Registry Creation
 *============================================================================*/

ac_tool_registry_t *ac_tool_registry_create(ac_session_t *session) {
    if (!session) {
        AC_LOG_ERROR("Invalid session");
        return NULL;
    }

    arena_t *arena = ac_session_get_arena(session);
    if (!arena) {
        AC_LOG_ERROR("Failed to get session arena");
        return NULL;
    }

    /* Allocate registry from arena */
    ac_tool_registry_t *registry = (ac_tool_registry_t *)arena_alloc(
        arena, sizeof(ac_tool_registry_t)
    );
    if (!registry) {
        AC_LOG_ERROR("Failed to allocate registry");
        return NULL;
    }

    /* Allocate initial tool array */
    registry->tools = (ac_tool_t *)arena_alloc(
        arena, sizeof(ac_tool_t) * INITIAL_CAPACITY
    );
    if (!registry->tools) {
        AC_LOG_ERROR("Failed to allocate tool array");
        return NULL;
    }

    registry->session = session;
    registry->arena = arena;
    registry->count = 0;
    registry->capacity = INITIAL_CAPACITY;

    /* Register with session for lifecycle management */
    if (ac_session_add_registry(session, registry) != ARC_OK) {
        AC_LOG_ERROR("Failed to register with session");
        return NULL;
    }

    AC_LOG_DEBUG("Tool registry created (capacity=%zu)", registry->capacity);
    return registry;
}

/*============================================================================
 * Internal: Grow Array
 *============================================================================*/

static arc_err_t registry_grow(ac_tool_registry_t *registry) {
    size_t new_capacity = registry->capacity * GROWTH_FACTOR;

    ac_tool_t *new_tools = (ac_tool_t *)arena_alloc(
        registry->arena, sizeof(ac_tool_t) * new_capacity
    );
    if (!new_tools) {
        AC_LOG_ERROR("Failed to grow tool array");
        return ARC_ERR_MEMORY;
    }

    /* Copy existing tools */
    memcpy(new_tools, registry->tools, sizeof(ac_tool_t) * registry->count);

    /* Old array remains in arena (will be freed when arena is destroyed) */
    registry->tools = new_tools;
    registry->capacity = new_capacity;

    AC_LOG_DEBUG("Tool registry grown to capacity=%zu", new_capacity);
    return ARC_OK;
}

/*============================================================================
 * Tool Registration
 *============================================================================*/

arc_err_t ac_tool_registry_add(
    ac_tool_registry_t *registry,
    const ac_tool_t *tool
) {
    if (!registry || !tool || !tool->name) {
        return ARC_ERR_INVALID_ARG;
    }

    /* Check for duplicate */
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->tools[i].name, tool->name) == 0) {
            AC_LOG_WARN("Tool '%s' already registered, skipping", tool->name);
            return ARC_OK;
        }
    }

    /* Grow if needed */
    if (registry->count >= registry->capacity) {
        arc_err_t err = registry_grow(registry);
        if (err != ARC_OK) {
            return err;
        }
    }

    /* Copy tool definition */
    ac_tool_t *dest = &registry->tools[registry->count];

    dest->name = arena_strdup(registry->arena, tool->name);
    dest->description = tool->description ?
        arena_strdup(registry->arena, tool->description) : NULL;
    dest->parameters = tool->parameters ?
        arena_strdup(registry->arena, tool->parameters) : NULL;
    dest->execute = tool->execute;
    dest->priv = tool->priv;

    if (!dest->name) {
        AC_LOG_ERROR("Failed to copy tool name");
        return ARC_ERR_MEMORY;
    }

    registry->count++;

    AC_LOG_DEBUG("Tool registered: %s (total=%zu)", tool->name, registry->count);
    return ARC_OK;
}

arc_err_t ac_tool_registry_add_array(
    ac_tool_registry_t *registry,
    const ac_tool_t **tools
) {
    if (!registry || !tools) {
        return ARC_ERR_INVALID_ARG;
    }

    for (const ac_tool_t **p = tools; *p != NULL; p++) {
        arc_err_t err = ac_tool_registry_add(registry, *p);
        if (err != ARC_OK) {
            return err;
        }
    }

    return ARC_OK;
}

/*============================================================================
 * Tool Query
 *============================================================================*/

const ac_tool_t *ac_tool_registry_find(
    const ac_tool_registry_t *registry,
    const char *name
) {
    if (!registry || !name) {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->tools[i].name, name) == 0) {
            return &registry->tools[i];
        }
    }

    return NULL;
}

size_t ac_tool_registry_count(const ac_tool_registry_t *registry) {
    return registry ? registry->count : 0;
}

/*============================================================================
 * Tool Execution
 *============================================================================*/

char *ac_tool_registry_call(
    ac_tool_registry_t *registry,
    const char *name,
    const char *args_json,
    const ac_tool_ctx_t *ctx
) {
    if (!registry || !name) {
        return strdup("{\"error\":\"Invalid arguments\"}");
    }

    const ac_tool_t *tool = ac_tool_registry_find(registry, name);
    if (!tool) {
        AC_LOG_WARN("Tool not found: %s", name);
        char *err = (char *)malloc(256);
        if (err) {
            snprintf(err, 256, "{\"error\":\"Tool '%s' not found\"}", name);
        }
        return err;
    }

    if (!tool->execute) {
        AC_LOG_ERROR("Tool '%s' has no execute function", name);
        return strdup("{\"error\":\"Tool has no execute function\"}");
    }

    AC_LOG_INFO("Executing tool: %s", name);

    const char *args = args_json ? args_json : "{}";
    char *result = tool->execute(ctx, args, tool->priv);

    AC_LOG_DEBUG("Tool %s returned: %.100s%s",
                 name,
                 result ? result : "NULL",
                 (result && strlen(result) > 100) ? "..." : "");

    return result ? result : strdup("{\"error\":\"Tool returned NULL\"}");
}

/*============================================================================
 * Schema Generation
 *============================================================================*/

/*============================================================================
 * Internal API (for tool_mcp.c)
 *============================================================================*/

arena_t *ac_tool_registry_get_arena(const ac_tool_registry_t *registry) {
    return registry ? registry->arena : NULL;
}

/*============================================================================
 * Schema Generation
 *============================================================================*/

char *ac_tool_registry_schema(const ac_tool_registry_t *registry) {
    if (!registry || registry->count == 0) {
        return NULL;
    }

    /* Use cJSON for safe JSON construction */
    cJSON *array = cJSON_CreateArray();
    if (!array) {
        AC_LOG_ERROR("Failed to create JSON array");
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        const ac_tool_t *tool = &registry->tools[i];

        /* Create tool object */
        cJSON *tool_obj = cJSON_CreateObject();
        if (!tool_obj) {
            cJSON_Delete(array);
            return NULL;
        }

        cJSON_AddStringToObject(tool_obj, "type", "function");

        /* Create function object */
        cJSON *func_obj = cJSON_CreateObject();
        if (!func_obj) {
            cJSON_Delete(tool_obj);
            cJSON_Delete(array);
            return NULL;
        }

        cJSON_AddStringToObject(func_obj, "name", tool->name);
        cJSON_AddStringToObject(func_obj, "description",
                                tool->description ? tool->description : "");

        /* Parse parameters JSON and add as object */
        const char *params_str = tool->parameters ?
            tool->parameters : "{\"type\":\"object\",\"properties\":{}}";
        cJSON *params = cJSON_Parse(params_str);
        if (params) {
            cJSON_AddItemToObject(func_obj, "parameters", params);
        } else {
            /* Fallback: add empty parameters */
            cJSON *empty_params = cJSON_CreateObject();
            cJSON_AddStringToObject(empty_params, "type", "object");
            cJSON_AddItemToObject(empty_params, "properties", cJSON_CreateObject());
            cJSON_AddItemToObject(func_obj, "parameters", empty_params);
        }

        cJSON_AddItemToObject(tool_obj, "function", func_obj);
        cJSON_AddItemToArray(array, tool_obj);
    }

    char *result = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);

    if (result) {
        AC_LOG_DEBUG("Built schema for %zu tools (%zu bytes)",
                     registry->count, strlen(result));
    }

    return result;
}
