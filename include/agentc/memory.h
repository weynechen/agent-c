/**
 * @file memory.h
 * @brief AgentC Memory Management
 *
 * Provides session memory (in-memory message history) and persistent memory (SQLite).
 * Session memory: stores messages in memory, cleared when session ends.
 * Persistent memory: stores in filesystem using SQLite (reserved, not implemented yet).
 */

#ifndef AGENTC_MEMORY_H
#define AGENTC_MEMORY_H

#include "http_client.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct ac_message;  /* Defined in llm.h */

/*============================================================================
 * Memory Configuration
 *============================================================================*/

typedef struct {
    const char *session_id;             /* Session identifier (optional) */
    size_t max_messages;                /* Max messages to keep (0 = unlimited) */
    size_t max_tokens;                  /* Max tokens to keep (0 = unlimited) */
    
    /* Persistent storage (reserved for future) */
    const char *db_path;                /* SQLite database path (optional) */
    int enable_persistence;             /* Enable persistent storage (default: 0) */
} ac_memory_config_t;

/*============================================================================
 * Memory Handle (opaque)
 *============================================================================*/

typedef struct ac_memory ac_memory_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Create a memory manager
 *
 * Example:
 * @code
 * ac_memory_t *memory = ac_memory_create(&(ac_memory_config_t){
 *     .session_id = "session-123",
 *     .max_messages = 100
 * });
 * @endcode
 *
 * @param config  Memory configuration (can be NULL for defaults)
 * @return Memory handle, NULL on error
 */
ac_memory_t *ac_memory_create(const ac_memory_config_t *config);

/**
 * @brief Destroy a memory manager
 *
 * @param memory  Memory handle
 */
void ac_memory_destroy(ac_memory_t *memory);

/**
 * @brief Add a message to memory
 *
 * @param memory   Memory handle
 * @param message  Message to add (copied internally)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_memory_add(ac_memory_t *memory, const struct ac_message *message);

/**
 * @brief Get all messages from memory
 *
 * @param memory  Memory handle
 * @return Linked list of messages (do not free, owned by memory)
 */
const struct ac_message *ac_memory_get_messages(ac_memory_t *memory);

/**
 * @brief Get message count
 *
 * @param memory  Memory handle
 * @return Number of messages in memory
 */
size_t ac_memory_count(ac_memory_t *memory);

/**
 * @brief Clear all messages from memory
 *
 * @param memory  Memory handle
 */
void ac_memory_clear(ac_memory_t *memory);

/**
 * @brief Get last N messages
 *
 * @param memory  Memory handle
 * @param n       Number of messages to get
 * @return Linked list of messages (caller must free with ac_message_free)
 */
struct ac_message *ac_memory_get_last_n(ac_memory_t *memory, size_t n);

/**
 * @brief Save memory to persistent storage (reserved for future)
 *
 * @param memory  Memory handle
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_memory_save(ac_memory_t *memory);

/**
 * @brief Load memory from persistent storage (reserved for future)
 *
 * @param memory  Memory handle
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_memory_load(ac_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MEMORY_H */
