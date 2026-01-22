/**
 * @file tool.h
 * @brief AgentC Tool Group - Internal Interface
 *
 * Simple tool group abstraction using arena allocation.
 * This is an internal interface used by agents.
 */

#ifndef AGENTC_TOOL_H
#define AGENTC_TOOL_H

#include "arena.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Tool Group Handle (opaque)
 *============================================================================*/

typedef struct ac_tool_group ac_tool_group_t;

/*============================================================================
 * Tool Group API
 *============================================================================*/

/**
 * @brief Create a tool group with arena
 *
 * Creates a tool group using the provided arena for all memory allocations.
 * All memory is freed when the arena is destroyed.
 *
 * @param arena  Arena for memory allocation
 * @param name   Tool group name (optional)
 * @return Tool group handle, NULL on error
 */
ac_tool_group_t* ac_tool_group_create(arena_t* arena, const char* name);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_TOOL_H */
