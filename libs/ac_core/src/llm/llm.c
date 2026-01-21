/**
 * @file llm.c
 * @brief LLM API client implementation with provider routing
 */

#include "agentc/llm.h"
#include "agentc/tool.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include "llm_internal.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define DEFAULT_BASE_URL "https://api.openai.com/v1"
#define DEFAULT_MODEL "gpt-3.5-turbo"
#define DEFAULT_TIMEOUT_MS 60000
#define DEFAULT_TEMPERATURE 0.7f

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
 * Message Helpers
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

/*============================================================================
 * Response Helpers
 *============================================================================*/

void ac_chat_response_free(ac_chat_response_t *response) {
    if (!response) return;

    AGENTC_FREE(response->id);
    AGENTC_FREE(response->model);
    AGENTC_FREE(response->content);
    AGENTC_FREE(response->finish_reason);
    ac_tool_call_free(response->tool_calls);

    memset(response, 0, sizeof(*response));
}

/*============================================================================
 * Client Create/Destroy
 *============================================================================*/

ac_llm_t *ac_llm_create(const ac_llm_params_t *params) {
    if (!params || !params->model || !params->api_key) {
        AC_LOG_ERROR("Invalid parameters: model and api_key are required");
        return NULL;
    }

    ac_llm_t *llm = AGENTC_CALLOC(1, sizeof(ac_llm_t));
    if (!llm) {
        return NULL;
    }

    /* Copy parameters */
    llm->model_copy = AGENTC_STRDUP(params->model);
    llm->api_key_copy = AGENTC_STRDUP(params->api_key);
    llm->api_base_copy = params->api_base ? AGENTC_STRDUP(params->api_base) : AGENTC_STRDUP(DEFAULT_BASE_URL);
    llm->instructions_copy = params->instructions ? AGENTC_STRDUP(params->instructions) : NULL;
    llm->organization_copy = params->organization ? AGENTC_STRDUP(params->organization) : NULL;

    if (!llm->model_copy || !llm->api_key_copy || !llm->api_base_copy) {
        AGENTC_FREE(llm->model_copy);
        AGENTC_FREE(llm->api_key_copy);
        AGENTC_FREE(llm->api_base_copy);
        AGENTC_FREE(llm->instructions_copy);
        AGENTC_FREE(llm->organization_copy);
        AGENTC_FREE(llm);
        return NULL;
    }

    /* Set parameters */
    llm->params.model = llm->model_copy;
    llm->params.api_key = llm->api_key_copy;
    llm->params.api_base = llm->api_base_copy;
    llm->params.instructions = llm->instructions_copy;
    llm->params.organization = llm->organization_copy;
    llm->params.temperature = (params->temperature > 0.0f) ? params->temperature : DEFAULT_TEMPERATURE;
    llm->params.max_tokens = params->max_tokens;
    llm->params.top_p = params->top_p;
    llm->params.top_k = params->top_k;
    llm->params.timeout_ms = (params->timeout_ms > 0) ? params->timeout_ms : DEFAULT_TIMEOUT_MS;

    /* Find appropriate provider */
    llm->ops = ac_llm_find_provider(&llm->params);
    if (!llm->ops) {
        AC_LOG_ERROR("No provider found for model=%s, base=%s", 
                     llm->params.model, llm->params.api_base);
        AGENTC_FREE(llm->model_copy);
        AGENTC_FREE(llm->api_key_copy);
        AGENTC_FREE(llm->api_base_copy);
        AGENTC_FREE(llm->instructions_copy);
        AGENTC_FREE(llm->organization_copy);
        AGENTC_FREE(llm);
        return NULL;
    }

    /* Create provider private data */
    if (llm->ops->create) {
        llm->priv = llm->ops->create(&llm->params);
        if (!llm->priv) {
            AC_LOG_ERROR("Provider %s failed to create private data", llm->ops->name);
            AGENTC_FREE(llm->model_copy);
            AGENTC_FREE(llm->api_key_copy);
            AGENTC_FREE(llm->api_base_copy);
            AGENTC_FREE(llm->instructions_copy);
            AGENTC_FREE(llm->organization_copy);
            AGENTC_FREE(llm);
            return NULL;
        }
    }

    AC_LOG_INFO("LLM created: provider=%s, model=%s, base=%s", 
                llm->ops->name, llm->params.model, llm->params.api_base);
    return llm;
}

void ac_llm_destroy(ac_llm_t *llm) {
    if (!llm) return;

    /* Cleanup provider private data */
    if (llm->ops && llm->ops->cleanup) {
        llm->ops->cleanup(llm->priv);
    }

    /* Free string copies */
    AGENTC_FREE(llm->model_copy);
    AGENTC_FREE(llm->api_key_copy);
    AGENTC_FREE(llm->api_base_copy);
    AGENTC_FREE(llm->instructions_copy);
    AGENTC_FREE(llm->organization_copy);
    
    AGENTC_FREE(llm);
}

/*============================================================================
 * Provider Routing
 *============================================================================*/

const ac_llm_ops_t* ac_llm_find_provider(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }
    
    const char* base = params->api_base ? params->api_base : "";
    const char* model = params->model ? params->model : "";
    
    // Check Anthropic
    if (strstr(base, "anthropic.com") || strstr(model, "claude")) {
        return &ac_provider_anthropic;
    }
    
    // Default to OpenAI (handles OpenAI, DeepSeek, and other OpenAI-compatible APIs)
    return &ac_provider_openai;
}

/*============================================================================
 * Build Message JSON with Tool Calls Support
 *============================================================================*/

cJSON *build_message_json(const ac_message_t *msg) {
    cJSON *msg_obj = cJSON_CreateObject();
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

        for (const ac_tool_call_t *tc = msg->tool_calls; tc; tc = tc->next) {
            cJSON *tc_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(tc_obj, "id", tc->id);
            cJSON_AddStringToObject(tc_obj, "type", "function");

            cJSON *func = cJSON_CreateObject();
            cJSON_AddStringToObject(func, "name", tc->name);
            cJSON_AddStringToObject(func, "arguments", tc->arguments ? tc->arguments : "{}");
            cJSON_AddItemToObject(tc_obj, "function", func);

            cJSON_AddItemToArray(tool_calls_arr, tc_obj);
        }

        cJSON_AddItemToObject(msg_obj, "tool_calls", tool_calls_arr);
    }

    return msg_obj;
}

/*============================================================================
 * Build Request JSON (used by providers)
 *============================================================================*/

char *build_chat_request_json(
    const ac_llm_t *llm,
    const ac_message_t *messages,
    const char *tools
) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* Model */
    cJSON_AddStringToObject(root, "model", llm->params.model);

    /* Messages array */
    cJSON *msgs_arr = cJSON_AddArrayToObject(root, "messages");
    
    /* Add system message if instructions provided */
    if (llm->params.instructions) {
        cJSON *sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", llm->params.instructions);
        cJSON_AddItemToArray(msgs_arr, sys_msg);
    }
    
    /* Add user messages */
    for (const ac_message_t *msg = messages; msg; msg = msg->next) {
        cJSON *msg_obj = build_message_json(msg);
        cJSON_AddItemToArray(msgs_arr, msg_obj);
    }

    /* Temperature */
    if (llm->params.temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)llm->params.temperature);
    }

    /* Max tokens */
    if (llm->params.max_tokens > 0) {
        cJSON_AddNumberToObject(root, "max_tokens", llm->params.max_tokens);
    }

    /* Top-p */
    if (llm->params.top_p > 0.0f) {
        cJSON_AddNumberToObject(root, "top_p", (double)llm->params.top_p);
    }

    /* Stream */
    cJSON_AddBoolToObject(root, "stream", 0);

    /* Tools */
    if (tools && strlen(tools) > 0) {
        cJSON *tools_arr = cJSON_Parse(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
            cJSON_AddStringToObject(root, "tool_choice", "auto");
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

/*============================================================================
 * Parse Tool Calls from Response (used by providers)
 *============================================================================*/

ac_tool_call_t *parse_tool_calls(cJSON *tool_calls_arr) {
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

/*============================================================================
 * Parse Response JSON (used by providers)
 *============================================================================*/

agentc_err_t parse_chat_response(
    const char *json,
    ac_chat_response_t *response
) {
    if (!json || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        AC_LOG_ERROR("Failed to parse JSON response");
        return AGENTC_ERR_HTTP;
    }

    /* Check for error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            AC_LOG_ERROR("API error: %s", msg->valuestring);
        }
        cJSON_Delete(root);
        return AGENTC_ERR_HTTP;
    }

    /* Parse id and model */
    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id && cJSON_IsString(id)) {
        response->id = AGENTC_STRDUP(id->valuestring);
    }

    cJSON *model = cJSON_GetObjectItem(root, "model");
    if (model && cJSON_IsString(model)) {
        response->model = AGENTC_STRDUP(model->valuestring);
    }

    /* Parse choices[0] */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        if (first_choice) {
            cJSON *message = cJSON_GetObjectItem(first_choice, "message");
            if (message) {
                /* Content */
                cJSON *content = cJSON_GetObjectItem(message, "content");
                if (content && cJSON_IsString(content)) {
                    response->content = AGENTC_STRDUP(content->valuestring);
                }

                /* Tool calls */
                cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    response->tool_calls = parse_tool_calls(tool_calls);
                }
            }

            /* Finish reason */
            cJSON *finish_reason = cJSON_GetObjectItem(first_choice, "finish_reason");
            if (finish_reason && cJSON_IsString(finish_reason)) {
                response->finish_reason = AGENTC_STRDUP(finish_reason->valuestring);
            }
        }
    }

    /* Parse usage */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *prompt_tokens = cJSON_GetObjectItem(usage, "prompt_tokens");
        if (prompt_tokens && cJSON_IsNumber(prompt_tokens)) {
            response->prompt_tokens = (int)prompt_tokens->valuedouble;
        }

        cJSON *completion_tokens = cJSON_GetObjectItem(usage, "completion_tokens");
        if (completion_tokens && cJSON_IsNumber(completion_tokens)) {
            response->completion_tokens = (int)completion_tokens->valuedouble;
        }

        cJSON *total_tokens = cJSON_GetObjectItem(usage, "total_tokens");
        if (total_tokens && cJSON_IsNumber(total_tokens)) {
            response->total_tokens = (int)total_tokens->valuedouble;
        }
    }

    cJSON_Delete(root);

    /* Consider success if we have content OR tool_calls */
    if (response->content || response->tool_calls) {
        return AGENTC_OK;
    }

    return AGENTC_ERR_HTTP;
}

/*============================================================================
 * Chat Completion (Blocking)
 *============================================================================*/

agentc_err_t ac_llm_chat(
    ac_llm_t *llm,
    const ac_message_t *messages,
    const char *tools,
    ac_chat_response_t *response
) {
    if (!llm || !messages || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    if (!llm->ops || !llm->ops->chat) {
        AC_LOG_ERROR("No provider chat function available");
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Delegate to provider implementation */
    return llm->ops->chat(llm->priv, &llm->params, messages, tools, response);
}

/*============================================================================
 * Simple Completion
 *============================================================================*/

agentc_err_t ac_llm_complete(
    ac_llm_t *llm,
    const char *prompt,
    char **response
) {
    if (!llm || !prompt || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Build message list */
    ac_message_t *messages = ac_message_create(AC_ROLE_USER, prompt);
    if (!messages) {
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Make request */
    ac_chat_response_t resp = {0};
    agentc_err_t err = ac_llm_chat(llm, messages, NULL, &resp);

    /* Cleanup messages */
    ac_message_free(messages);

    if (err != AGENTC_OK) {
        ac_chat_response_free(&resp);
        return err;
    }

    /* Return content */
    *response = resp.content;
    resp.content = NULL;  /* Transfer ownership */

    ac_chat_response_free(&resp);
    return AGENTC_OK;
}

/*============================================================================
 * Tool Call Clone
 *============================================================================*/

ac_tool_call_t *ac_tool_call_clone(const ac_tool_call_t *calls) {
    if (!calls) return NULL;

    ac_tool_call_t *head = NULL;
    ac_tool_call_t *tail = NULL;

    for (const ac_tool_call_t *src = calls; src; src = src->next) {
        ac_tool_call_t *clone = AGENTC_CALLOC(1, sizeof(ac_tool_call_t));
        if (!clone) {
            ac_tool_call_free(head);
            return NULL;
}

        clone->id = src->id ? AGENTC_STRDUP(src->id) : NULL;
        clone->name = src->name ? AGENTC_STRDUP(src->name) : NULL;
        clone->arguments = src->arguments ? AGENTC_STRDUP(src->arguments) : NULL;

        if (!head) {
            head = clone;
            tail = clone;
        } else {
            tail->next = clone;
            tail = clone;
        }
    }

    return head;
}
