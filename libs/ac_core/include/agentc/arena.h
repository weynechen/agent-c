/**
 * @file arena.h
 * @brief Arena allocator for AgentC memory management
 *
 * Provides simple, efficient arena-based memory allocation.
 * All memory is freed at once when the arena is destroyed.
 */

#ifndef AGENTC_ARENA_H
#define AGENTC_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Arena Types (opaque)
 *============================================================================*/

typedef struct arena_ arena_t;

/*============================================================================
 * Arena API
 *============================================================================*/

/**
 * @brief Create an arena allocator
 *
 * @param capacity  Initial capacity in bytes
 * @return Arena handle, NULL on error
 */
arena_t* arena_create(size_t capacity);

/**
 * @brief Allocate memory from arena
 *
 * @param arena  Arena handle
 * @param size   Number of bytes to allocate
 * @return Pointer to allocated memory, NULL if out of space
 */
char* arena_alloc(arena_t *arena, size_t size);

/**
 * @brief Duplicate a string in arena
 *
 * @param arena  Arena handle
 * @param str    String to duplicate
 * @return Duplicated string, NULL on error
 */
char* arena_strdup(arena_t *arena, const char* str);

/**
 * @brief Reset arena (clear all allocations)
 *
 * @param arena  Arena handle
 * @return 1 on success, 0 on error
 */
int arena_reset(arena_t *arena);

/**
 * @brief Destroy arena and free all memory
 *
 * @param arena  Arena handle
 * @return 1 on success, 0 on error
 */
int arena_destroy(arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_ARENA_H */
