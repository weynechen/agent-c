/**
 * @file message.h
 * @brief AgentC Message Structure - Internal Interface
 *
 * Simple message structure for conversation history.
 * Messages are stored in agent's arena.
 */

#ifndef AGENTC_MESSAGE_H
#define AGENTC_MESSAGE_H

#include "arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Message Role
 *============================================================================*/

typedef enum {
    AC_ROLE_SYSTEM,
    AC_ROLE_USER,
    AC_ROLE_ASSISTANT,
    AC_ROLE_TOOL
} ac_role_t;

/*============================================================================
 * Message Structure
 *============================================================================*/

typedef struct ac_message {
    ac_role_t role;
    char* content;                   /* Message content (stored in arena) */
    char* tool_call_id;              /* Optional: for tool responses */
    struct ac_message* next;         /* Linked list */
} ac_message_t;

/*============================================================================
 * Message API
 *============================================================================*/

/**
 * @brief Create a message in arena
 *
 * @param arena   Arena for allocation
 * @param role    Message role
 * @param content Message content
 * @return New message, NULL on error
 */
ac_message_t* ac_message_create(arena_t* arena, ac_role_t role, const char* content);

/**
 * @brief Create a tool result message in arena
 *
 * @param arena        Arena for allocation
 * @param tool_call_id Tool call ID
 * @param content      Tool result content
 * @return New message, NULL on error
 */
ac_message_t* ac_message_create_tool_result(
    arena_t* arena,
    const char* tool_call_id,
    const char* content
);

/**
 * @brief Append message to list
 *
 * @param list     Pointer to list head
 * @param message  Message to append
 */
void ac_message_append(ac_message_t** list, ac_message_t* message);

/**
 * @brief Count messages in list
 *
 * @param list  Message list
 * @return Number of messages
 */
size_t ac_message_count(const ac_message_t* list);

/**
 * @brief Get role string
 *
 * @param role  Role enum
 * @return Role string ("system", "user", "assistant", "tool")
 */
const char* ac_role_to_string(ac_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MESSAGE_H */
