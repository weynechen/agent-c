/**
 * @file message.h
 * @brief ArC Message Structure - Internal Interface
 *
 * Simple message structure for conversation history.
 * Messages are stored in agent's arena.
 */

#ifndef ARC_MESSAGE_H
#define ARC_MESSAGE_H

#include "arena.h"
#include "error.h"

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
 * Tool Call Structure
 *============================================================================*/

/**
 * @brief Tool call from LLM response
 */
typedef struct ac_tool_call {
    char* id;                        /* Tool call ID (e.g., "call_abc123") */
    char* name;                      /* Function name */
    char* arguments;                 /* JSON arguments string */
    struct ac_tool_call* next;       /* Linked list for multiple tool calls */
} ac_tool_call_t;

/*============================================================================
 * Chat Response Structure
 *============================================================================*/

/**
 * @brief LLM chat completion response
 */
typedef struct {
    char* content;                   /* Text response (may be NULL if tool_calls) */
    ac_tool_call_t* tool_calls;      /* Tool calls list (may be NULL) */
    int tool_call_count;             /* Number of tool calls */

    /* Usage info */
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;

    /* Finish reason */
    char* finish_reason;             /* "stop", "tool_calls", "length", etc. */
} ac_chat_response_t;

/*============================================================================
 * Message Structure
 *============================================================================*/

typedef struct ac_message {
    ac_role_t role;
    char* content;                   /* Message content (stored in arena) */
    char* tool_call_id;              /* For AC_ROLE_TOOL: which tool call this responds to */
    ac_tool_call_t* tool_calls;      /* For AC_ROLE_ASSISTANT: tool calls to make */
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

/*============================================================================
 * Tool Call API
 *============================================================================*/

/**
 * @brief Create a tool call in arena
 */
ac_tool_call_t* ac_tool_call_create(
    arena_t* arena,
    const char* id,
    const char* name,
    const char* arguments
);

/**
 * @brief Append tool call to list
 */
void ac_tool_call_append(ac_tool_call_t** list, ac_tool_call_t* call);

/**
 * @brief Create assistant message with tool calls
 */
ac_message_t* ac_message_create_with_tool_calls(
    arena_t* arena,
    const char* content,
    ac_tool_call_t* tool_calls
);

/*============================================================================
 * Chat Response API
 *============================================================================*/

/**
 * @brief Initialize response structure
 */
void ac_chat_response_init(ac_chat_response_t* response);

/**
 * @brief Free response contents (not the struct itself)
 */
void ac_chat_response_free(ac_chat_response_t* response);

/**
 * @brief Parse JSON response into ac_chat_response_t
 *
 * @param json_str  Raw JSON response from LLM API
 * @param response  Output response structure
 * @return ARC_OK on success
 */
arc_err_t ac_chat_response_parse(const char* json_str, ac_chat_response_t* response);

/**
 * @brief Check if response has tool calls
 */
static inline int ac_chat_response_has_tool_calls(const ac_chat_response_t* resp) {
    return resp && resp->tool_calls && resp->tool_call_count > 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ARC_MESSAGE_H */
