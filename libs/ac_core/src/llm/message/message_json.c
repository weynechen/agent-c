/**
 * @file message_json.c
 * @brief Message JSON serialization/deserialization implementation
 */

#include "message_json.h"
#include "arc/log.h"
#include "arc/platform.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Message to JSON
 *============================================================================*/

cJSON* ac_tool_call_to_json(const ac_tool_call_t* call) {
    if (!call) {
        return NULL;
    }

    cJSON* obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }

    cJSON_AddStringToObject(obj, "id", call->id);
    cJSON_AddStringToObject(obj, "type", "function");

    cJSON* func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", call->name);
    cJSON_AddStringToObject(func, "arguments", call->arguments ? call->arguments : "{}");
    cJSON_AddItemToObject(obj, "function", func);

    return obj;
}

cJSON* ac_message_to_json(const ac_message_t* msg) {
    if (!msg) {
        return NULL;
    }

    cJSON* obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }

    /* Role */
    cJSON_AddStringToObject(obj, "role", ac_role_to_string(msg->role));

    /* Content - can be NULL for assistant messages with tool_calls */
    if (msg->content) {
        cJSON_AddStringToObject(obj, "content", msg->content);
    } else if (msg->role == AC_ROLE_ASSISTANT && msg->tool_calls) {
        /* OpenAI requires content field even if null */
        cJSON_AddNullToObject(obj, "content");
    }

    /* Tool call ID (for tool result messages) */
    if (msg->role == AC_ROLE_TOOL && msg->tool_call_id) {
        cJSON_AddStringToObject(obj, "tool_call_id", msg->tool_call_id);
    }

    /* Tool calls (for assistant messages) */
    if (msg->role == AC_ROLE_ASSISTANT && msg->tool_calls) {
        cJSON* tool_calls_arr = cJSON_CreateArray();

        for (ac_tool_call_t* call = msg->tool_calls; call; call = call->next) {
            cJSON* call_obj = ac_tool_call_to_json(call);
            if (call_obj) {
                cJSON_AddItemToArray(tool_calls_arr, call_obj);
            }
        }

        cJSON_AddItemToObject(obj, "tool_calls", tool_calls_arr);
    }

    return obj;
}

/*============================================================================
 * Message List Serialization
 *============================================================================*/

char* ac_messages_to_json_string(const ac_message_t* messages) {
    if (!messages) {
        return NULL;
    }

    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        cJSON* msg_json = ac_message_to_json(msg);
        if (msg_json) {
            cJSON_AddItemToArray(arr, msg_json);
        }
    }

    char* json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    return json_str;
}

char* ac_tool_calls_to_json_string(const ac_tool_call_t* calls) {
    if (!calls) {
        return NULL;
    }

    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    for (const ac_tool_call_t* call = calls; call; call = call->next) {
        cJSON* call_json = ac_tool_call_to_json(call);
        if (call_json) {
            cJSON_AddItemToArray(arr, call_json);
        }
    }

    char* json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    return json_str;
}

/*============================================================================
 * Chat Response Helpers
 *============================================================================*/

void ac_chat_response_init(ac_chat_response_t* response) {
    if (!response) return;
    memset(response, 0, sizeof(ac_chat_response_t));
}

void ac_chat_response_free(ac_chat_response_t* response) {
    if (!response) return;

    /* Free content */
    if (response->content) {
        ARC_FREE(response->content);
        response->content = NULL;
    }

    /* Free tool calls */
    ac_tool_call_t* call = response->tool_calls;
    while (call) {
        ac_tool_call_t* next = call->next;
        if (call->id) ARC_FREE(call->id);
        if (call->name) ARC_FREE(call->name);
        if (call->arguments) ARC_FREE(call->arguments);
        ARC_FREE(call);
        call = next;
    }
    response->tool_calls = NULL;
    response->tool_call_count = 0;

    /* Free finish reason */
    if (response->finish_reason) {
        ARC_FREE(response->finish_reason);
        response->finish_reason = NULL;
    }
}

/*============================================================================
 * JSON to Response
 *============================================================================*/

static ac_tool_call_t* parse_tool_call(const cJSON* call_json) {
    if (!call_json) {
        return NULL;
    }

    cJSON* id = cJSON_GetObjectItem(call_json, "id");
    cJSON* func = cJSON_GetObjectItem(call_json, "function");

    if (!id || !cJSON_IsString(id) || !func) {
        return NULL;
    }

    cJSON* name = cJSON_GetObjectItem(func, "name");
    cJSON* args = cJSON_GetObjectItem(func, "arguments");

    if (!name || !cJSON_IsString(name)) {
        return NULL;
    }

    ac_tool_call_t* call = (ac_tool_call_t*)ARC_CALLOC(1, sizeof(ac_tool_call_t));
    if (!call) {
        return NULL;
    }

    call->id = ARC_STRDUP(cJSON_GetStringValue(id));
    call->name = ARC_STRDUP(cJSON_GetStringValue(name));
    call->arguments = args && cJSON_IsString(args) ?
                      ARC_STRDUP(cJSON_GetStringValue(args)) : NULL;
    call->next = NULL;

    return call;
}

arc_err_t ac_chat_response_parse(const char* json_str, ac_chat_response_t* response) {
    if (!json_str || !response) {
        return ARC_ERR_INVALID_ARG;
    }

    /* Initialize response */
    ac_chat_response_init(response);

    /* Parse JSON */
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        AC_LOG_ERROR("Failed to parse response JSON");
        return ARC_ERR_HTTP;
    }

    /* Check for error response */
    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON* msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            AC_LOG_ERROR("API error: %s", cJSON_GetStringValue(msg));
        }
        cJSON_Delete(root);
        return ARC_ERR_HTTP;
    }

    /* Get choices array */
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        AC_LOG_ERROR("No choices in response");
        cJSON_Delete(root);
        return ARC_ERR_HTTP;
    }

    /* Get first choice */
    cJSON* choice = cJSON_GetArrayItem(choices, 0);
    cJSON* message = cJSON_GetObjectItem(choice, "message");

    if (!message) {
        AC_LOG_ERROR("No message in choice");
        cJSON_Delete(root);
        return ARC_ERR_HTTP;
    }

    /* Extract content */
    cJSON* content = cJSON_GetObjectItem(message, "content");
    if (content && cJSON_IsString(content)) {
        response->content = ARC_STRDUP(cJSON_GetStringValue(content));
    }

    /* Extract finish reason */
    cJSON* finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    if (finish_reason && cJSON_IsString(finish_reason)) {
        response->finish_reason = ARC_STRDUP(cJSON_GetStringValue(finish_reason));
    }

    /* Extract tool calls */
    cJSON* tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int count = cJSON_GetArraySize(tool_calls);
        ac_tool_call_t* last_call = NULL;

        for (int i = 0; i < count; i++) {
            cJSON* call_json = cJSON_GetArrayItem(tool_calls, i);
            ac_tool_call_t* call = parse_tool_call(call_json);

            if (call) {
                if (!response->tool_calls) {
                    response->tool_calls = call;
                } else {
                    last_call->next = call;
                }
                last_call = call;
                response->tool_call_count++;
            }
        }
    }

    /* Extract usage */
    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON* pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON* ct = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON* tt = cJSON_GetObjectItem(usage, "total_tokens");

        if (pt && cJSON_IsNumber(pt)) response->prompt_tokens = pt->valueint;
        if (ct && cJSON_IsNumber(ct)) response->completion_tokens = ct->valueint;
        if (tt && cJSON_IsNumber(tt)) response->total_tokens = tt->valueint;
    }

    cJSON_Delete(root);

    AC_LOG_DEBUG("Parsed response: content=%s, tool_calls=%d, finish=%s",
                 response->content ? "yes" : "no",
                 response->tool_call_count,
                 response->finish_reason ? response->finish_reason : "none");

    return ARC_OK;
}
