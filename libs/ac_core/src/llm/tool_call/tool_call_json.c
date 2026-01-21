/**
 * @file tool_call_json.c
 * @brief Tool call JSON parsing implementation
 */

#include "tool_call_json.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include "cJSON.h"
#include <string.h>

/*============================================================================
 * Deserialization Implementation
 *============================================================================*/

ac_tool_call_t *ac_tool_call_parse_json(cJSON *tool_calls_arr) {
    if (!tool_calls_arr || !cJSON_IsArray(tool_calls_arr)) {
        return NULL;
    }

    ac_tool_call_t *head = NULL;
    ac_tool_call_t *tail = NULL;

    int size = cJSON_GetArraySize(tool_calls_arr);
    for (int i = 0; i < size; i++) {
        cJSON *tc = cJSON_GetArrayItem(tool_calls_arr, i);
        if (!tc) continue;

        cJSON *id = cJSON_GetObjectItem(tc, "id");
        cJSON *func = cJSON_GetObjectItem(tc, "function");

        if (!func) continue;

        cJSON *name = cJSON_GetObjectItem(func, "name");
        cJSON *args = cJSON_GetObjectItem(func, "arguments");

        ac_tool_call_t *call = AGENTC_CALLOC(1, sizeof(ac_tool_call_t));
        if (!call) {
            ac_tool_call_free(head);
            return NULL;
        }

        call->id = (id && cJSON_IsString(id)) ? AGENTC_STRDUP(id->valuestring) : NULL;
        call->name = (name && cJSON_IsString(name)) ? AGENTC_STRDUP(name->valuestring) : NULL;
        call->arguments = (args && cJSON_IsString(args)) ? AGENTC_STRDUP(args->valuestring) : NULL;
        call->next = NULL;

        if (!head) {
            head = call;
            tail = call;
        } else {
            tail->next = call;
            tail = call;
        }
    }

    return head;
}
