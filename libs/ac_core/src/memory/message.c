/**
 * @file message.c
 * @brief Message implementation
 */

#include "arc/message.h"
#include "arc/log.h"
#include <string.h>

/*============================================================================
 * Role Helper
 *============================================================================*/

const char* ac_role_to_string(ac_role_t role) {
    switch (role) {
        case AC_ROLE_SYSTEM:    return "system";
        case AC_ROLE_USER:      return "user";
        case AC_ROLE_ASSISTANT: return "assistant";
        case AC_ROLE_TOOL:      return "tool";
        default:                return "unknown";
    }
}

/*============================================================================
 * Message Creation
 *============================================================================*/

ac_message_t* ac_message_create(arena_t* arena, ac_role_t role, const char* content) {
    if (!arena || !content) {
        AC_LOG_ERROR("Invalid arguments to ac_message_create");
        return NULL;
    }

    ac_message_t* msg = (ac_message_t*)arena_alloc(arena, sizeof(ac_message_t));
    if (!msg) {
        AC_LOG_ERROR("Failed to allocate message from arena");
        return NULL;
    }

    msg->role = role;
    msg->content = arena_strdup(arena, content);
    msg->tool_call_id = NULL;
    msg->tool_calls = NULL;
    msg->next = NULL;

    if (!msg->content) {
        AC_LOG_ERROR("Failed to duplicate message content");
        return NULL;
    }

    return msg;
}

ac_message_t* ac_message_create_tool_result(
    arena_t* arena,
    const char* tool_call_id,
    const char* content
) {
    if (!arena || !tool_call_id || !content) {
        AC_LOG_ERROR("Invalid arguments to ac_message_create_tool_result");
        return NULL;
    }

    ac_message_t* msg = (ac_message_t*)arena_alloc(arena, sizeof(ac_message_t));
    if (!msg) {
        AC_LOG_ERROR("Failed to allocate message from arena");
        return NULL;
    }

    msg->role = AC_ROLE_TOOL;
    msg->content = arena_strdup(arena, content);
    msg->tool_call_id = arena_strdup(arena, tool_call_id);
    msg->tool_calls = NULL;
    msg->next = NULL;

    if (!msg->content || !msg->tool_call_id) {
        AC_LOG_ERROR("Failed to duplicate message strings");
        return NULL;
    }

    return msg;
}

/*============================================================================
 * Message List Operations
 *============================================================================*/

void ac_message_append(ac_message_t** list, ac_message_t* message) {
    if (!list || !message) {
        return;
    }

    if (!*list) {
        *list = message;
        return;
    }

    ac_message_t* tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = message;
}

size_t ac_message_count(const ac_message_t* list) {
    size_t count = 0;
    const ac_message_t* curr = list;

    while (curr) {
        count++;
        curr = curr->next;
    }

    return count;
}

/*============================================================================
 * Tool Call Operations
 *============================================================================*/

ac_tool_call_t* ac_tool_call_create(
    arena_t* arena,
    const char* id,
    const char* name,
    const char* arguments
) {
    if (!arena || !id || !name) {
        AC_LOG_ERROR("Invalid arguments to ac_tool_call_create");
        return NULL;
    }

    ac_tool_call_t* call = (ac_tool_call_t*)arena_alloc(arena, sizeof(ac_tool_call_t));
    if (!call) {
        AC_LOG_ERROR("Failed to allocate tool call from arena");
        return NULL;
    }

    call->id = arena_strdup(arena, id);
    call->name = arena_strdup(arena, name);
    call->arguments = arguments ? arena_strdup(arena, arguments) : NULL;
    call->next = NULL;

    if (!call->id || !call->name) {
        AC_LOG_ERROR("Failed to duplicate tool call strings");
        return NULL;
    }

    return call;
}

void ac_tool_call_append(ac_tool_call_t** list, ac_tool_call_t* call) {
    if (!list || !call) {
        return;
    }

    if (!*list) {
        *list = call;
        return;
    }

    ac_tool_call_t* tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = call;
}

ac_message_t* ac_message_create_with_tool_calls(
    arena_t* arena,
    const char* content,
    ac_tool_call_t* tool_calls
) {
    if (!arena) {
        AC_LOG_ERROR("Invalid arena in ac_message_create_with_tool_calls");
        return NULL;
    }

    ac_message_t* msg = (ac_message_t*)arena_alloc(arena, sizeof(ac_message_t));
    if (!msg) {
        AC_LOG_ERROR("Failed to allocate message from arena");
        return NULL;
    }

    msg->role = AC_ROLE_ASSISTANT;
    msg->content = content ? arena_strdup(arena, content) : NULL;
    msg->tool_call_id = NULL;
    msg->tool_calls = tool_calls;
    msg->next = NULL;

    return msg;
}
