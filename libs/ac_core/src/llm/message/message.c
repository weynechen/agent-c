/**
 * @file message.c
 * @brief Message management implementation
 */

#include "message.h"
#include "agentc/tool.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include <string.h>

/*============================================================================
 * Role Helpers
 *============================================================================*/

const char *ac_role_to_string(ac_role_t role) {
    switch (role) {
        case AC_ROLE_SYSTEM:    return "system";
        case AC_ROLE_USER:      return "user";
        case AC_ROLE_ASSISTANT: return "assistant";
        case AC_ROLE_TOOL:      return "tool";
        default:                return "user";
    }
}

/*============================================================================
 * Message CRUD Operations
 *============================================================================*/

ac_message_t *ac_message_create(ac_role_t role, const char *content) {
    ac_message_t *msg = AGENTC_CALLOC(1, sizeof(ac_message_t));
    if (!msg) return NULL;

    msg->role = role;
    msg->content = content ? AGENTC_STRDUP(content) : NULL;
    msg->next = NULL;

    return msg;
}

ac_message_t *ac_message_create_tool_result(
    const char *tool_call_id,
    const char *content
) {
    if (!tool_call_id) return NULL;

    ac_message_t *msg = AGENTC_CALLOC(1, sizeof(ac_message_t));
    if (!msg) return NULL;

    msg->role = AC_ROLE_TOOL;
    msg->content = content ? AGENTC_STRDUP(content) : NULL;
    msg->tool_call_id = AGENTC_STRDUP(tool_call_id);
    msg->next = NULL;

    if (!msg->tool_call_id) {
        AGENTC_FREE(msg->content);
        AGENTC_FREE(msg);
        return NULL;
    }

    return msg;
}

ac_message_t *ac_message_create_assistant_tool_calls(
    const char *content,
    ac_tool_call_t *tool_calls
) {
    ac_message_t *msg = AGENTC_CALLOC(1, sizeof(ac_message_t));
    if (!msg) return NULL;

    msg->role = AC_ROLE_ASSISTANT;
    msg->content = content ? AGENTC_STRDUP(content) : NULL;
    msg->tool_calls = tool_calls;  /* Takes ownership */
    msg->next = NULL;

    return msg;
}

void ac_message_append(ac_message_t **list, ac_message_t *message) {
    if (!list || !message) return;

    if (!*list) {
        *list = message;
        return;
    }

    ac_message_t *tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = message;
}

void ac_message_free(ac_message_t *list) {
    while (list) {
        ac_message_t *next = list->next;
        AGENTC_FREE(list->content);
        AGENTC_FREE(list->name);
        AGENTC_FREE(list->tool_call_id);
        ac_tool_call_free(list->tool_calls);
        AGENTC_FREE(list);
        list = next;
    }
}
