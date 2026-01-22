/**
 * @file tool.c
 * @brief Tool group implementation with arena allocation
 */

#include "agentc/tool.h"
#include "agentc/log.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Tool Group Structure (internal)
 *============================================================================*/

struct ac_tool_group {
    const char* name;
    arena_t* arena;
    // TODO: Add tool registry
};

/*============================================================================
 * Tool Group Implementation
 *============================================================================*/

ac_tool_group_t* ac_tool_group_create(arena_t* arena, const char* name) {
    if (!arena) {
        AC_LOG_ERROR("Invalid arena");
        return NULL;
    }
    
    // Allocate from arena
    ac_tool_group_t* group = (ac_tool_group_t*)arena_alloc(arena, sizeof(ac_tool_group_t));
    if (!group) {
        AC_LOG_ERROR("Failed to allocate tool group from arena");
        return NULL;
    }
    
    group->arena = arena;
    group->name = name ? arena_strdup(arena, name) : NULL;
    
    AC_LOG_DEBUG("Tool group created: %s", group->name ? group->name : "(unnamed)");
    
    return group;
}
