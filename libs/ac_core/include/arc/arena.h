/**
 * @file arena.h
 * @brief Arena allocator for ArC memory management
 *
 * Provides efficient arena-based memory allocation with automatic expansion.
 * Features:
 * - Automatic block chaining when capacity is exceeded
 * - Thread-safe mode (optional, via ARC_ARENA_THREAD_SAFE)
 * - All memory is freed at once when the arena is destroyed
 *
 * Example:
 * @code
 * arena_t *arena = arena_create(1024 * 1024);  // 1MB initial
 * char *buf = arena_alloc(arena, 4096);        // Auto-expands if needed
 * arena_destroy(arena);                         // Frees all blocks
 * @endcode
 */

#ifndef ARC_ARENA_H
#define ARC_ARENA_H

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
 * Arena Statistics
 *============================================================================*/

/**
 * @brief Arena memory statistics
 */
typedef struct {
    size_t total_capacity;      /* Total capacity across all blocks */
    size_t total_allocated;     /* Total bytes allocated */
    size_t block_count;         /* Number of blocks */
    size_t largest_block;       /* Size of largest block */
} arena_stats_t;

/*============================================================================
 * Arena API
 *============================================================================*/

/**
 * @brief Create an arena allocator
 *
 * Creates a new arena with the specified initial capacity.
 * The arena will automatically expand by allocating new blocks
 * when the current capacity is exceeded.
 *
 * @param capacity  Initial capacity in bytes (minimum 4KB enforced)
 * @return Arena handle, NULL on error
 */
arena_t* arena_create(size_t capacity);

/**
 * @brief Allocate memory from arena
 *
 * Allocates memory from the arena. If the current block is full,
 * a new block is automatically allocated and chained.
 * Memory is 8-byte aligned.
 *
 * @param arena  Arena handle
 * @param size   Number of bytes to allocate
 * @return Pointer to allocated memory, NULL on error
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
 * @brief Reset arena (clear all allocations, keep memory)
 *
 * Resets all blocks to empty state without freeing memory.
 * Useful for reusing the arena without reallocation overhead.
 *
 * @param arena  Arena handle
 * @return 1 on success, 0 on error
 */
int arena_reset(arena_t *arena);

/**
 * @brief Destroy arena and free all memory
 *
 * Frees all blocks and the arena structure.
 *
 * @param arena  Arena handle
 * @return 1 on success, 0 on error
 */
int arena_destroy(arena_t *arena);

/**
 * @brief Get arena memory statistics
 *
 * @param arena  Arena handle
 * @param stats  Output statistics structure
 * @return 1 on success, 0 on error
 */
int arena_get_stats(const arena_t *arena, arena_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* ARC_ARENA_H */
