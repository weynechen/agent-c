/**
 * @file message_json.c
 * @brief Message JSON serialization implementation
 */

#include "message_json.h"
#include "message.h"
#include "agentc/tool.h"
#include "agentc/log.h"
#include "cJSON.h"

/*============================================================================
 * Serialization Implementation
 *============================================================================*/

cJSON *ac_message_to_json(const ac_message_t *msg) {
    if (!msg) return NULL;

    cJSON *msg_obj = cJSON_CreateObject();
    if (!msg_obj) return NULL;

    cJSON_AddStringToObject(msg_obj, "role", ac_role_to_string(msg->role));

    /* Handle content */
    if (msg->content) {
        cJSON_AddStringToObject(msg_obj, "content", msg->content);
    } else if (msg->role == AC_ROLE_ASSISTANT && msg->tool_calls) {
        /* Assistant with tool_calls but no content - add null */
        cJSON_AddNullToObject(msg_obj, "content");
    }

    /* Handle tool message */
    if (msg->role == AC_ROLE_TOOL && msg->tool_call_id) {
        cJSON_AddStringToObject(msg_obj, "tool_call_id", msg->tool_call_id);
    }

    /* Handle assistant tool_calls */
    if (msg->role == AC_ROLE_ASSISTANT && msg->tool_calls) {
        cJSON *tool_calls_arr = cJSON_CreateArray();
        if (!tool_calls_arr) {
            cJSON_Delete(msg_obj);
            return NULL;
        }

        for (const ac_tool_call_t *tc = msg->tool_calls; tc; tc = tc->next) {
            cJSON *tc_obj = cJSON_CreateObject();
            if (!tc_obj) continue;

            cJSON_AddStringToObject(tc_obj, "id", tc->id);
            cJSON_AddStringToObject(tc_obj, "type", "function");

            cJSON *func = cJSON_CreateObject();
            if (func) {
                cJSON_AddStringToObject(func, "name", tc->name);
                cJSON_AddStringToObject(func, "arguments", tc->arguments ? tc->arguments : "{}");
                cJSON_AddItemToObject(tc_obj, "function", func);
            }

            cJSON_AddItemToArray(tool_calls_arr, tc_obj);
        }

        cJSON_AddItemToObject(msg_obj, "tool_calls", tool_calls_arr);
    }

    return msg_obj;
}

cJSON *ac_messages_to_json_array(const ac_message_t *messages) {
    cJSON *msgs_arr = cJSON_CreateArray();
    if (!msgs_arr) return NULL;

    for (const ac_message_t *msg = messages; msg; msg = msg->next) {
        cJSON *msg_obj = ac_message_to_json(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }

    return msgs_arr;
}
