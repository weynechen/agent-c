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

    /* Free response ID */
    if (response->id) {
        ARC_FREE(response->id);
        response->id = NULL;
    }

    /* Free content blocks (v2) */
    ac_content_block_t* block = response->blocks;
    while (block) {
        ac_content_block_t* next = block->next;
        if (block->text) ARC_FREE(block->text);
        if (block->signature) ARC_FREE(block->signature);
        if (block->data) ARC_FREE(block->data);
        if (block->id) ARC_FREE(block->id);
        if (block->name) ARC_FREE(block->name);
        if (block->input) ARC_FREE(block->input);
        ARC_FREE(block);
        block = next;
    }
    response->blocks = NULL;
    response->block_count = 0;

    /* Free legacy content */
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
    if (response->stop_reason) {
        ARC_FREE(response->stop_reason);
        response->stop_reason = NULL;
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

    /* Sync v2 fields from legacy */
    response->input_tokens = response->prompt_tokens;
    response->output_tokens = response->completion_tokens;

    AC_LOG_DEBUG("Parsed response: content=%s, tool_calls=%d, finish=%s",
                 response->content ? "yes" : "no",
                 response->tool_call_count,
                 response->finish_reason ? response->finish_reason : "none");

    return ARC_OK;
}

/*============================================================================
 * Anthropic Format Parsing
 *============================================================================*/

/**
 * @brief Parse a content block from Anthropic response
 */
static ac_content_block_t* parse_anthropic_content_block(const cJSON* block_json) {
    if (!block_json) return NULL;

    cJSON* type_obj = cJSON_GetObjectItem(block_json, "type");
    if (!type_obj || !cJSON_IsString(type_obj)) return NULL;

    const char* type_str = cJSON_GetStringValue(type_obj);
    if (!type_str) return NULL;

    ac_content_block_t* block = (ac_content_block_t*)ARC_CALLOC(1, sizeof(ac_content_block_t));
    if (!block) return NULL;

    if (strcmp(type_str, "text") == 0) {
        block->type = AC_BLOCK_TEXT;
        cJSON* text = cJSON_GetObjectItem(block_json, "text");
        if (text && cJSON_IsString(text)) {
            block->text = ARC_STRDUP(cJSON_GetStringValue(text));
        }
    } else if (strcmp(type_str, "thinking") == 0) {
        block->type = AC_BLOCK_THINKING;
        cJSON* thinking = cJSON_GetObjectItem(block_json, "thinking");
        cJSON* signature = cJSON_GetObjectItem(block_json, "signature");
        if (thinking && cJSON_IsString(thinking)) {
            block->text = ARC_STRDUP(cJSON_GetStringValue(thinking));
        }
        if (signature && cJSON_IsString(signature)) {
            block->signature = ARC_STRDUP(cJSON_GetStringValue(signature));
        }
    } else if (strcmp(type_str, "redacted_thinking") == 0) {
        block->type = AC_BLOCK_REDACTED_THINKING;
        cJSON* data = cJSON_GetObjectItem(block_json, "data");
        if (data && cJSON_IsString(data)) {
            block->data = ARC_STRDUP(cJSON_GetStringValue(data));
        }
    } else if (strcmp(type_str, "tool_use") == 0) {
        block->type = AC_BLOCK_TOOL_USE;
        cJSON* id = cJSON_GetObjectItem(block_json, "id");
        cJSON* name = cJSON_GetObjectItem(block_json, "name");
        cJSON* input = cJSON_GetObjectItem(block_json, "input");

        if (id && cJSON_IsString(id)) {
            block->id = ARC_STRDUP(cJSON_GetStringValue(id));
        }
        if (name && cJSON_IsString(name)) {
            block->name = ARC_STRDUP(cJSON_GetStringValue(name));
        }
        if (input) {
            char* input_str = cJSON_PrintUnformatted(input);
            if (input_str) {
                block->input = input_str;
            }
        }
    } else {
        /* Unknown block type, skip */
        ARC_FREE(block);
        return NULL;
    }

    return block;
}

arc_err_t ac_chat_response_parse_anthropic(const char* json_str, ac_chat_response_t* response) {
    if (!json_str || !response) {
        return ARC_ERR_INVALID_ARG;
    }

    ac_chat_response_init(response);

    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        AC_LOG_ERROR("Failed to parse Anthropic response JSON");
        return ARC_ERR_HTTP;
    }

    /* Check for error response */
    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON* msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            AC_LOG_ERROR("Anthropic API error: %s", cJSON_GetStringValue(msg));
        }
        cJSON_Delete(root);
        return ARC_ERR_HTTP;
    }

    /* Extract response ID */
    cJSON* id = cJSON_GetObjectItem(root, "id");
    if (id && cJSON_IsString(id)) {
        response->id = ARC_STRDUP(cJSON_GetStringValue(id));
    }

    /* Extract stop reason */
    cJSON* stop_reason = cJSON_GetObjectItem(root, "stop_reason");
    if (stop_reason && cJSON_IsString(stop_reason)) {
        response->stop_reason = ARC_STRDUP(cJSON_GetStringValue(stop_reason));
        response->finish_reason = ARC_STRDUP(cJSON_GetStringValue(stop_reason));
    }

    /* Parse content array */
    cJSON* content = cJSON_GetObjectItem(root, "content");
    if (content && cJSON_IsArray(content)) {
        int count = cJSON_GetArraySize(content);
        ac_content_block_t* last_block = NULL;

        for (int i = 0; i < count; i++) {
            cJSON* block_json = cJSON_GetArrayItem(content, i);
            ac_content_block_t* block = parse_anthropic_content_block(block_json);

            if (block) {
                if (!response->blocks) {
                    response->blocks = block;
                } else {
                    last_block->next = block;
                }
                last_block = block;
                response->block_count++;

                /* Also populate legacy fields for compatibility */
                if (block->type == AC_BLOCK_TEXT && block->text && !response->content) {
                    response->content = ARC_STRDUP(block->text);
                } else if (block->type == AC_BLOCK_TOOL_USE) {
                    /* Add to legacy tool_calls list */
                    ac_tool_call_t* call = (ac_tool_call_t*)ARC_CALLOC(1, sizeof(ac_tool_call_t));
                    if (call) {
                        call->id = block->id ? ARC_STRDUP(block->id) : NULL;
                        call->name = block->name ? ARC_STRDUP(block->name) : NULL;
                        call->arguments = block->input ? ARC_STRDUP(block->input) : NULL;
                        call->next = NULL;

                        if (!response->tool_calls) {
                            response->tool_calls = call;
                        } else {
                            ac_tool_call_t* last = response->tool_calls;
                            while (last->next) last = last->next;
                            last->next = call;
                        }
                        response->tool_call_count++;
                    }
                }
            }
        }
    }

    /* Extract usage */
    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON* it = cJSON_GetObjectItem(usage, "input_tokens");
        cJSON* ot = cJSON_GetObjectItem(usage, "output_tokens");

        if (it && cJSON_IsNumber(it)) {
            response->input_tokens = it->valueint;
            response->prompt_tokens = it->valueint;
        }
        if (ot && cJSON_IsNumber(ot)) {
            response->output_tokens = ot->valueint;
            response->completion_tokens = ot->valueint;
        }
        response->total_tokens = response->input_tokens + response->output_tokens;

        /* Anthropic-specific token fields */
        cJSON* cache_creation = cJSON_GetObjectItem(usage, "cache_creation_input_tokens");
        cJSON* cache_read = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
        if (cache_creation && cJSON_IsNumber(cache_creation)) {
            response->cache_creation_tokens = cache_creation->valueint;
        }
        if (cache_read && cJSON_IsNumber(cache_read)) {
            response->cache_read_tokens = cache_read->valueint;
        }
    }

    cJSON_Delete(root);

    AC_LOG_DEBUG("Parsed Anthropic response: blocks=%d, content=%s, tool_calls=%d, stop=%s",
                 response->block_count,
                 response->content ? "yes" : "no",
                 response->tool_call_count,
                 response->stop_reason ? response->stop_reason : "none");

    return ARC_OK;
}

/*============================================================================
 * Content Block to JSON (Anthropic format)
 *============================================================================*/

cJSON* ac_content_block_to_json(const ac_content_block_t* block) {
    if (!block) return NULL;

    cJSON* obj = cJSON_CreateObject();
    if (!obj) return NULL;

    switch (block->type) {
        case AC_BLOCK_TEXT:
            /* Skip empty text blocks */
            if (!block->text || strlen(block->text) == 0) {
                cJSON_Delete(obj);
                return NULL;
            }
            cJSON_AddStringToObject(obj, "type", "text");
            cJSON_AddStringToObject(obj, "text", block->text);
            break;

        case AC_BLOCK_THINKING:
            /*
             * Anthropic API requires thinking blocks to have a signature.
             * If no signature is present (e.g., from compatible endpoints like
             * Alibaba Cloud Bailian), skip the thinking block entirely.
             * The thinking content is shown during streaming but not persisted
             * in history for non-Anthropic endpoints.
             */
            if (!block->signature) {
                /* Skip thinking block without signature */
                cJSON_Delete(obj);
                return NULL;
            }
            cJSON_AddStringToObject(obj, "type", "thinking");
            if (block->text) {
                cJSON_AddStringToObject(obj, "thinking", block->text);
            }
            cJSON_AddStringToObject(obj, "signature", block->signature);
            break;

        case AC_BLOCK_REDACTED_THINKING:
            cJSON_AddStringToObject(obj, "type", "redacted_thinking");
            if (block->data) {
                cJSON_AddStringToObject(obj, "data", block->data);
            }
            break;

        case AC_BLOCK_TOOL_USE:
            cJSON_AddStringToObject(obj, "type", "tool_use");
            if (block->id) {
                cJSON_AddStringToObject(obj, "id", block->id);
            }
            if (block->name) {
                cJSON_AddStringToObject(obj, "name", block->name);
            }
            if (block->input) {
                cJSON* input = cJSON_Parse(block->input);
                if (input) {
                    cJSON_AddItemToObject(obj, "input", input);
                } else {
                    cJSON_AddStringToObject(obj, "input", block->input);
                }
            }
            break;

        case AC_BLOCK_TOOL_RESULT:
            cJSON_AddStringToObject(obj, "type", "tool_result");
            if (block->id) {
                cJSON_AddStringToObject(obj, "tool_use_id", block->id);
            }
            if (block->text) {
                cJSON_AddStringToObject(obj, "content", block->text);
            }
            if (block->is_error) {
                cJSON_AddBoolToObject(obj, "is_error", 1);
            }
            break;

        default:
            cJSON_Delete(obj);
            return NULL;
    }

    return obj;
}

cJSON* ac_message_to_json_anthropic(const ac_message_t* msg) {
    if (!msg) return NULL;

    cJSON* obj = cJSON_CreateObject();
    if (!obj) return NULL;

    /* Role */
    cJSON_AddStringToObject(obj, "role", ac_role_to_string(msg->role));

    /* Content array */
    cJSON* content_arr = cJSON_CreateArray();

    /* If blocks present, use them */
    if (msg->blocks) {
        for (ac_content_block_t* block = msg->blocks; block; block = block->next) {
            cJSON* block_json = ac_content_block_to_json(block);
            if (block_json) {
                cJSON_AddItemToArray(content_arr, block_json);
            }
        }
    } else if (msg->role == AC_ROLE_TOOL && msg->tool_call_id && msg->content) {
        /* Tool result message */
        cJSON* result_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(result_obj, "type", "tool_result");
        cJSON_AddStringToObject(result_obj, "tool_use_id", msg->tool_call_id);
        cJSON_AddStringToObject(result_obj, "content", msg->content);
        cJSON_AddItemToArray(content_arr, result_obj);
    } else if (msg->content) {
        /* Simple text content */
        cJSON* text_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(text_obj, "type", "text");
        cJSON_AddStringToObject(text_obj, "text", msg->content);
        cJSON_AddItemToArray(content_arr, text_obj);
    }

    cJSON_AddItemToObject(obj, "content", content_arr);

    return obj;
}
