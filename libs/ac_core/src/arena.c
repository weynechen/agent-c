/**
 * @file arena.c
 * @brief Arena allocator implementation with automatic block chaining
 *
 * This implementation uses a linked list of memory blocks. When the current
 * block is exhausted, a new block is automatically allocated and chained.
 * All blocks are freed together when the arena is destroyed.
 *
 * Thread safety:
 * - Define ARC_ARENA_THREAD_SAFE to enable mutex protection
 * - Without it, the arena is NOT thread-safe (typical use: one arena per agent)
 */

#include "arc/arena.h"
#include "arc/platform.h"
#include "arc/log.h"
#include <string.h>

#ifdef ARC_ARENA_THREAD_SAFE
#include "pthread_port.h"
#endif

/*============================================================================
 * Constants (from platform.h, can be overridden at compile time)
 *============================================================================*/

/* Use platform-specific defaults from platform.h */
#define ARENA_MIN_BLOCK_SIZE    ARC_ARENA_MIN_BLOCK_SIZE
#define ARENA_ALIGNMENT         8           /* Memory alignment */
#define ARENA_ALIGN(size)       (((size) + (ARENA_ALIGNMENT - 1)) & ~(ARENA_ALIGNMENT - 1))

/*============================================================================
 * Forward Declarations
 *============================================================================*/

static size_t arena_block_count(const arena_t *arena);

/*============================================================================
 * Arena Block Structure
 *============================================================================*/

typedef struct arena_block {
    struct arena_block *next;   /* Next block in chain */
    size_t capacity;            /* Block capacity (excluding header) */
    size_t used;                /* Bytes used in this block */
    char data[];                /* Flexible array member */
} arena_block_t;

/*============================================================================
 * Arena Main Structure
 *============================================================================*/

struct arena_ {
    arena_block_t *head;        /* First block */
    arena_block_t *current;     /* Current allocation block */
    size_t default_block_size;  /* Default size for new blocks */
    size_t total_capacity;      /* Sum of all block capacities */
    size_t total_allocated;     /* Sum of all allocations */

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_t lock;
#endif
};

/*============================================================================
 * Internal: Create a new block
 *============================================================================*/

static arena_block_t *arena_block_create(size_t capacity) {
    /* Enforce minimum size */
    if (capacity < ARENA_MIN_BLOCK_SIZE) {
        capacity = ARENA_MIN_BLOCK_SIZE;
    }

    arena_block_t *block = (arena_block_t *)ARC_MALLOC(sizeof(arena_block_t) + capacity);
    if (!block) {
        return NULL;
    }

    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;

    return block;
}

/*============================================================================
 * Arena API Implementation
 *============================================================================*/

arena_t *arena_create(size_t capacity) {
    arena_t *arena = (arena_t *)ARC_CALLOC(1, sizeof(arena_t));
    if (!arena) {
        return NULL;
    }

    /* Enforce minimum capacity */
    if (capacity < ARENA_MIN_BLOCK_SIZE) {
        capacity = ARENA_MIN_BLOCK_SIZE;
    }

    /* Create initial block */
    arena->head = arena_block_create(capacity);
    if (!arena->head) {
        ARC_FREE(arena);
        return NULL;
    }

    arena->current = arena->head;
    arena->default_block_size = capacity;
    arena->total_capacity = capacity;
    arena->total_allocated = 0;

#ifdef ARC_ARENA_THREAD_SAFE
    if (pthread_mutex_init(&arena->lock, NULL) != 0) {
        ARC_FREE(arena->head);
        ARC_FREE(arena);
        return NULL;
    }
#endif

    AC_LOG_DEBUG("Arena created: initial_capacity=%zuKB", capacity / 1024);
    return arena;
}

char *arena_alloc(arena_t *arena, size_t size) {
    if (!arena || size == 0) {
        return NULL;
    }

    /* Align size to 8 bytes */
    size = ARENA_ALIGN(size);

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_lock(&arena->lock);
#endif

    arena_block_t *block = arena->current;

    /* Check if current block has space */
    if (block->used + size > block->capacity) {
        /* Try to find a block with enough space in the chain */
        arena_block_t *search = arena->head;
        arena_block_t *found = NULL;

        while (search) {
            if (search->used + size <= search->capacity) {
                found = search;
                break;
            }
            search = search->next;
        }

        if (found) {
            /* Use existing block with space */
            block = found;
            arena->current = found;
        } else {
            /* Need to allocate a new block */
            size_t new_cap = arena->default_block_size;

            /* For large allocations, create a block big enough */
            if (size > new_cap) {
                new_cap = size;
            }

            arena_block_t *new_block = arena_block_create(new_cap);
            if (!new_block) {
                AC_LOG_ERROR("Arena expansion failed: requested %zu bytes", size);
#ifdef ARC_ARENA_THREAD_SAFE
                pthread_mutex_unlock(&arena->lock);
#endif
                return NULL;
            }

            /* Append to chain */
            arena_block_t *tail = arena->head;
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = new_block;

            arena->current = new_block;
            arena->total_capacity += new_cap;
            block = new_block;

            AC_LOG_DEBUG("Arena expanded: +%zuKB (total=%zuKB, blocks=%zu)",
                         new_cap / 1024,
                         arena->total_capacity / 1024,
                         arena_block_count(arena));
        }
    }

    /* Allocate from current block */
    char *ptr = block->data + block->used;
    block->used += size;
    arena->total_allocated += size;

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_unlock(&arena->lock);
#endif

    return ptr;
}

char *arena_strdup(arena_t *arena, const char *str) {
    if (!arena || !str) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *copy = arena_alloc(arena, len);

    if (copy) {
        memcpy(copy, str, len);
    }

    return copy;
}

int arena_reset(arena_t *arena) {
    if (!arena) {
        return 0;
    }

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_lock(&arena->lock);
#endif

    /* Reset all blocks */
    for (arena_block_t *block = arena->head; block; block = block->next) {
        block->used = 0;
    }

    arena->current = arena->head;
    arena->total_allocated = 0;

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_unlock(&arena->lock);
#endif

    AC_LOG_DEBUG("Arena reset: capacity=%zuKB preserved", arena->total_capacity / 1024);
    return 1;
}

int arena_destroy(arena_t *arena) {
    if (!arena) {
        return 0;
    }

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_destroy(&arena->lock);
#endif

    /* Free all blocks */
    arena_block_t *block = arena->head;
    size_t block_count = 0;

    while (block) {
        arena_block_t *next = block->next;
        ARC_FREE(block);
        block = next;
        block_count++;
    }

    AC_LOG_DEBUG("Arena destroyed: freed %zu blocks", block_count);

    ARC_FREE(arena);
    return 1;
}

int arena_get_stats(const arena_t *arena, arena_stats_t *stats) {
    if (!arena || !stats) {
        return 0;
    }

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_lock((pthread_mutex_t *)&((arena_t *)arena)->lock);
#endif

    stats->total_capacity = arena->total_capacity;
    stats->total_allocated = arena->total_allocated;
    stats->block_count = 0;
    stats->largest_block = 0;

    for (arena_block_t *block = arena->head; block; block = block->next) {
        stats->block_count++;
        if (block->capacity > stats->largest_block) {
            stats->largest_block = block->capacity;
        }
    }

#ifdef ARC_ARENA_THREAD_SAFE
    pthread_mutex_unlock((pthread_mutex_t *)&((arena_t *)arena)->lock);
#endif

    return 1;
}

/*============================================================================
 * Internal helper (used in debug log)
 *============================================================================*/

static size_t arena_block_count(const arena_t *arena) {
    size_t count = 0;
    for (arena_block_t *b = arena->head; b; b = b->next) {
        count++;
    }
    return count;
}
