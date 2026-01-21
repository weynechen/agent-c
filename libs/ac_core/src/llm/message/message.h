/**
 * @file message.h
 * @brief Message management for LLM conversations
 * 
 * Provides message creation, manipulation and lifecycle management.
 * Separated from LLM to follow single responsibility principle.
 */

#ifndef AGENTC_MESSAGE_H
#define AGENTC_MESSAGE_H

#include "agentc/llm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Message CRUD Operations
 *============================================================================*/

/**
 * @brief Create a message
 *
 * @param role     Message role
 * @param content  Message content
 * @return New message (caller must free), NULL on error
 */
ac_message_t *ac_message_create(ac_role_t role, const char *content);

/**
 * @brief Create a tool result message
 *
 * @param tool_call_id  ID of the tool call this responds to
 * @param content       Tool result content
 * @return New message (caller must free), NULL on error
 */
ac_message_t *ac_message_create_tool_result(
    const char *tool_call_id,
    const char *content
);

/**
 * @brief Create an assistant message with tool calls
 *
 * @param content     Optional text content (can be NULL)
 * @param tool_calls  Tool calls (ownership transferred to message)
 * @return New message (caller must free), NULL on error
 */
ac_message_t *ac_message_create_assistant_tool_calls(
    const char *content,
    struct ac_tool_call *tool_calls
);

/**
 * @brief Append message to list
 *
 * @param list     Pointer to message list head
 * @param message  Message to append
 */
void ac_message_append(ac_message_t **list, ac_message_t *message);

/**
 * @brief Free message list
 *
 * @param list  Message list to free
 */
void ac_message_free(ac_message_t *list);

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Get role string
 *
 * @param role  Role enum
 * @return Role string ("system", "user", "assistant", "tool")
 */
const char *ac_role_to_string(ac_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MESSAGE_H */
