/**
 * @file arena.c
 * @brief Arena allocator implementation
 */

#include "agentc/arena.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Arena Internal Structures
 *============================================================================*/

typedef struct arena_list_ {
    struct arena_list_ *next;
} arena_list_t;

struct arena_ {
    arena_list_t *next;
    size_t capacity;
    size_t count;
    char data[];
};

arena_t *arena_create(size_t capacity)
{
    arena_t *arena = (arena_t*)calloc(1u, sizeof(arena_t) + capacity);
    if (arena) {
        arena->capacity = capacity;
        arena->count = 0;
        arena->next = NULL;
    }
    return arena;
}

char* arena_alloc(arena_t *arena, size_t size)
{
    if (!arena || arena->count + size > arena->capacity) {
        return NULL;
    }
    
    char* ptr = &arena->data[arena->count];
    arena->count += size;
    return ptr;
}

char* arena_strdup(arena_t *arena, const char* str)
{
    if (!arena || !str) {
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char* copy = arena_alloc(arena, len);
    
    if (copy) {
        memcpy(copy, str, len);
    }
    
    return copy;
}

int arena_reset(arena_t *arena)
{
    if (!arena) {
        return 0;
    }
    
    arena->count = 0;
    memset(arena->data, 0, arena->capacity);
    return 1;
}

int arena_destroy(arena_t *arena)
{
    if (!arena) {
        return 0;
    }
    
    free(arena);
    return 1;
}
