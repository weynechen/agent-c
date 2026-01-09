/**
 * @file llm.c
 * @brief LLM API client implementation
 */

#include "agentc/llm.h"
#include "agentc/tool.h"
#include "agentc/platform.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define DEFAULT_BASE_URL "https://api.openai.com/v1"
#define DEFAULT_MODEL "gpt-3.5-turbo"
#define DEFAULT_TIMEOUT_MS 60000

/*============================================================================
 * Internal Structures
 *============================================================================*/

struct agentc_llm_client {
    agentc_llm_config_t config;
    agentc_http_client_t *http;
    char *api_key_copy;
    char *base_url_copy;
    char *model_copy;
};

typedef struct {
    agentc_llm_stream_callback_t on_chunk;
    agentc_llm_stream_done_callback_t on_done;
    void *user_data;
    char *buffer;           /* SSE line buffer */
    size_t buf_len;
    size_t buf_cap;
    int total_tokens;
    char *finish_reason;
} stream_parse_ctx_t;

/*============================================================================
 * Role Helpers
 *============================================================================*/

const char *agentc_role_to_string(agentc_role_t role) {
    switch (role) {
        case AGENTC_ROLE_SYSTEM:    return "system";
        case AGENTC_ROLE_USER:      return "user";
        case AGENTC_ROLE_ASSISTANT: return "assistant";
        case AGENTC_ROLE_TOOL:      return "tool";
        default:                    return "user";
    }
}

/*============================================================================
 * Message Helpers
 *============================================================================*/

agentc_message_t *agentc_message_create(agentc_role_t role, const char *content) {
    agentc_message_t *msg = AGENTC_CALLOC(1, sizeof(agentc_message_t));
    if (!msg) return NULL;

    msg->role = role;
    msg->content = content ? AGENTC_STRDUP(content) : NULL;
    msg->next = NULL;

    return msg;
}

agentc_message_t *agentc_message_create_tool_result(
    const char *tool_call_id,
    const char *content
) {
    if (!tool_call_id) return NULL;

    agentc_message_t *msg = AGENTC_CALLOC(1, sizeof(agentc_message_t));
    if (!msg) return NULL;

    msg->role = AGENTC_ROLE_TOOL;
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

agentc_message_t *agentc_message_create_assistant_tool_calls(
    const char *content,
    agentc_tool_call_t *tool_calls
) {
    agentc_message_t *msg = AGENTC_CALLOC(1, sizeof(agentc_message_t));
    if (!msg) return NULL;

    msg->role = AGENTC_ROLE_ASSISTANT;
    msg->content = content ? AGENTC_STRDUP(content) : NULL;
    msg->tool_calls = tool_calls;  /* Takes ownership */
    msg->next = NULL;

    return msg;
}

void agentc_message_append(agentc_message_t **list, agentc_message_t *message) {
    if (!list || !message) return;

    if (!*list) {
        *list = message;
        return;
    }

    agentc_message_t *tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = message;
}

void agentc_message_free(agentc_message_t *list) {
    while (list) {
        agentc_message_t *next = list->next;
        AGENTC_FREE(list->content);
        AGENTC_FREE(list->name);
        AGENTC_FREE(list->tool_call_id);
        agentc_tool_call_free(list->tool_calls);
        AGENTC_FREE(list);
        list = next;
    }
}

/*============================================================================
 * Response Helpers
 *============================================================================*/

void agentc_chat_response_free(agentc_chat_response_t *response) {
    if (!response) return;

    AGENTC_FREE(response->id);
    AGENTC_FREE(response->model);
    AGENTC_FREE(response->content);
    AGENTC_FREE(response->finish_reason);
    agentc_tool_call_free(response->tool_calls);

    memset(response, 0, sizeof(*response));
}

/*============================================================================
 * Client Create/Destroy
 *============================================================================*/

agentc_err_t agentc_llm_create(
    const agentc_llm_config_t *config,
    agentc_llm_client_t **out
) {
    if (!config || !config->api_key || !out) {
        return AGENTC_ERR_INVALID_ARG;
    }

    agentc_llm_client_t *client = AGENTC_CALLOC(1, sizeof(agentc_llm_client_t));
    if (!client) {
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Copy config */
    client->api_key_copy = AGENTC_STRDUP(config->api_key);
    client->base_url_copy = AGENTC_STRDUP(
        config->base_url ? config->base_url : DEFAULT_BASE_URL
    );
    client->model_copy = AGENTC_STRDUP(
        config->model ? config->model : DEFAULT_MODEL
    );

    if (!client->api_key_copy || !client->base_url_copy || !client->model_copy) {
        AGENTC_FREE(client->api_key_copy);
        AGENTC_FREE(client->base_url_copy);
        AGENTC_FREE(client->model_copy);
        AGENTC_FREE(client);
        return AGENTC_ERR_NO_MEMORY;
    }

    client->config.api_key = client->api_key_copy;
    client->config.base_url = client->base_url_copy;
    client->config.model = client->model_copy;
    client->config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : DEFAULT_TIMEOUT_MS;

    /* Create HTTP client */
    agentc_http_client_config_t http_config = {
        .default_timeout_ms = client->config.timeout_ms,
    };

    agentc_err_t err = agentc_http_client_create(&http_config, &client->http);
    if (err != AGENTC_OK) {
        AGENTC_FREE(client->api_key_copy);
        AGENTC_FREE(client->base_url_copy);
        AGENTC_FREE(client->model_copy);
        AGENTC_FREE(client);
        return err;
    }

    AGENTC_LOG_INFO("LLM client created: %s", client->config.base_url);
    *out = client;
    return AGENTC_OK;
}

void agentc_llm_destroy(agentc_llm_client_t *client) {
    if (!client) return;

    agentc_http_client_destroy(client->http);
    AGENTC_FREE(client->api_key_copy);
    AGENTC_FREE(client->base_url_copy);
    AGENTC_FREE(client->model_copy);
    AGENTC_FREE(client);
}

/*============================================================================
 * Build Message JSON with Tool Calls Support
 *============================================================================*/

static cJSON *build_message_json(const agentc_message_t *msg) {
    cJSON *msg_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_obj, "role", agentc_role_to_string(msg->role));

    /* Handle content */
    if (msg->content) {
        cJSON_AddStringToObject(msg_obj, "content", msg->content);
    } else if (msg->role == AGENTC_ROLE_ASSISTANT && msg->tool_calls) {
        /* Assistant with tool_calls but no content - add null */
        cJSON_AddNullToObject(msg_obj, "content");
    }

    /* Handle tool message */
    if (msg->role == AGENTC_ROLE_TOOL && msg->tool_call_id) {
        cJSON_AddStringToObject(msg_obj, "tool_call_id", msg->tool_call_id);
    }

    /* Handle assistant tool_calls */
    if (msg->role == AGENTC_ROLE_ASSISTANT && msg->tool_calls) {
        cJSON *tool_calls_arr = cJSON_CreateArray();

        for (const agentc_tool_call_t *tc = msg->tool_calls; tc; tc = tc->next) {
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
 * Build Request JSON (using cJSON)
 *============================================================================*/

static char *build_chat_request_json(
    const agentc_llm_client_t *client,
    const agentc_chat_request_t *request,
    int stream
) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* Model */
    const char *model = request->model ? request->model : client->config.model;
    cJSON_AddStringToObject(root, "model", model);

    /* Messages array */
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    for (const agentc_message_t *msg = request->messages; msg; msg = msg->next) {
        cJSON *msg_obj = build_message_json(msg);
        cJSON_AddItemToArray(messages, msg_obj);
    }

    /* Temperature */
    if (request->temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)request->temperature);
    }

    /* Max tokens */
    if (request->max_tokens > 0) {
        cJSON_AddNumberToObject(root, "max_tokens", request->max_tokens);
    }

    /* Stream */
    if (stream) {
        cJSON_AddBoolToObject(root, "stream", 1);
    }

    /* Tools */
    if (request->tools_json && strlen(request->tools_json) > 0) {
        cJSON *tools = cJSON_Parse(request->tools_json);
        if (tools) {
            cJSON_AddItemToObject(root, "tools", tools);

            /* Tool choice */
            if (request->tool_choice) {
                if (strcmp(request->tool_choice, "auto") == 0 ||
                    strcmp(request->tool_choice, "none") == 0 ||
                    strcmp(request->tool_choice, "required") == 0) {
                    cJSON_AddStringToObject(root, "tool_choice", request->tool_choice);
                } else {
                    /* Specific function */
                    cJSON *choice = cJSON_CreateObject();
                    cJSON_AddStringToObject(choice, "type", "function");
                    cJSON *func = cJSON_CreateObject();
                    cJSON_AddStringToObject(func, "name", request->tool_choice);
                    cJSON_AddItemToObject(choice, "function", func);
                    cJSON_AddItemToObject(root, "tool_choice", choice);
                }
            }

            /* Parallel tool calls */
            if (!request->parallel_tool_calls) {
                cJSON_AddBoolToObject(root, "parallel_tool_calls", 0);
            }
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

/*============================================================================
 * Parse Tool Calls from Response
 *============================================================================*/

static agentc_tool_call_t *parse_tool_calls(cJSON *tool_calls_arr) {
    if (!tool_calls_arr || !cJSON_IsArray(tool_calls_arr)) {
        return NULL;
    }

    agentc_tool_call_t *head = NULL;
    agentc_tool_call_t *tail = NULL;

    int size = cJSON_GetArraySize(tool_calls_arr);
    for (int i = 0; i < size; i++) {
        cJSON *tc = cJSON_GetArrayItem(tool_calls_arr, i);
        if (!tc) continue;

        cJSON *id = cJSON_GetObjectItem(tc, "id");
        cJSON *func = cJSON_GetObjectItem(tc, "function");

        if (!func) continue;

        cJSON *name = cJSON_GetObjectItem(func, "name");
        cJSON *args = cJSON_GetObjectItem(func, "arguments");

        agentc_tool_call_t *call = AGENTC_CALLOC(1, sizeof(agentc_tool_call_t));
        if (!call) {
            agentc_tool_call_free(head);
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
 * Parse Response JSON (using cJSON)
 *============================================================================*/

static agentc_err_t parse_chat_response(
    const char *json,
    agentc_chat_response_t *response
) {
    if (!json || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        AGENTC_LOG_ERROR("Failed to parse JSON response");
        return AGENTC_ERR_HTTP;
    }

    /* Check for error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            AGENTC_LOG_ERROR("API error: %s", msg->valuestring);
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

agentc_err_t agentc_llm_chat(
    agentc_llm_client_t *client,
    const agentc_chat_request_t *request,
    agentc_chat_response_t *response
) {
    if (!client || !request || !request->messages || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", client->config.base_url);

    /* Build request body */
    char *body = build_chat_request_json(client, request, 0);
    if (!body) {
        return AGENTC_ERR_NO_MEMORY;
    }

    AGENTC_LOG_DEBUG("Request body: %s", body);

    /* Build headers */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", client->config.api_key);

    agentc_http_header_t *headers = NULL;
    agentc_http_header_append(&headers,
        agentc_http_header_create("Content-Type", "application/json"));
    agentc_http_header_append(&headers,
        agentc_http_header_create("Authorization", auth_header));

    /* Make request */
    agentc_http_request_t req = {
        .url = url,
        .method = AGENTC_HTTP_POST,
        .headers = headers,
        .body = body,
        .body_len = strlen(body),
        .timeout_ms = client->config.timeout_ms,
        .verify_ssl = 1,
    };

    agentc_http_response_t http_resp = {0};
    agentc_err_t err = agentc_http_request(client->http, &req, &http_resp);

    /* Cleanup */
    agentc_http_header_free(headers);
    cJSON_free(body);

    if (err != AGENTC_OK) {
        agentc_http_response_free(&http_resp);
        return err;
    }

    if (http_resp.status_code != 200) {
        AGENTC_LOG_ERROR("HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }

    /* Parse response */
    AGENTC_LOG_DEBUG("Response: %s", http_resp.body);
    err = parse_chat_response(http_resp.body, response);

    agentc_http_response_free(&http_resp);
    return err;
}

/*============================================================================
 * Simple Completion
 *============================================================================*/

agentc_err_t agentc_llm_complete(
    agentc_llm_client_t *client,
    const char *prompt,
    char **response
) {
    if (!client || !prompt || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Build message list */
    agentc_message_t *messages = agentc_message_create(AGENTC_ROLE_USER, prompt);
    if (!messages) {
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Make request */
    agentc_chat_request_t req = {
        .messages = messages,
    };

    agentc_chat_response_t resp = {0};
    agentc_err_t err = agentc_llm_chat(client, &req, &resp);

    /* Cleanup messages */
    agentc_message_free(messages);

    if (err != AGENTC_OK) {
        agentc_chat_response_free(&resp);
        return err;
    }

    /* Return content */
    *response = resp.content;
    resp.content = NULL;  /* Transfer ownership */

    agentc_chat_response_free(&resp);
    return AGENTC_OK;
}

/*============================================================================
 * Streaming (SSE) Support
 *============================================================================*/

static void parse_sse_line(stream_parse_ctx_t *ctx, const char *line, size_t len) {
    /* Skip empty lines */
    if (len == 0) return;

    /* Check for "data: " prefix */
    if (len < 6 || strncmp(line, "data: ", 6) != 0) {
        return;
    }

    const char *data = line + 6;
    size_t data_len = len - 6;

    /* Check for [DONE] */
    if (data_len == 6 && strncmp(data, "[DONE]", 6) == 0) {
        if (ctx->on_done) {
            ctx->on_done(ctx->finish_reason, ctx->total_tokens, ctx->user_data);
        }
        return;
    }

    /* Parse JSON delta using cJSON */
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) return;

    /* Extract content from choices[0].delta.content */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        if (first) {
            cJSON *delta = cJSON_GetObjectItem(first, "delta");
            if (delta) {
                cJSON *content = cJSON_GetObjectItem(delta, "content");
                if (content && cJSON_IsString(content) && ctx->on_chunk) {
                    ctx->on_chunk(content->valuestring, strlen(content->valuestring), ctx->user_data);
                }
            }

            /* Check finish_reason */
            cJSON *finish = cJSON_GetObjectItem(first, "finish_reason");
            if (finish && cJSON_IsString(finish) && strlen(finish->valuestring) > 0) {
                AGENTC_FREE(ctx->finish_reason);
                ctx->finish_reason = AGENTC_STRDUP(finish->valuestring);
            }
        }
    }

    cJSON_Delete(root);
}

static int stream_data_callback(const char *data, size_t len, void *user_data) {
    stream_parse_ctx_t *ctx = (stream_parse_ctx_t *)user_data;

    /* Append to buffer and process complete lines */
    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\n') {
            /* Process line */
            if (ctx->buf_len > 0) {
                parse_sse_line(ctx, ctx->buffer, ctx->buf_len);
            }
            ctx->buf_len = 0;
        } else if (c != '\r') {
            /* Add to buffer */
            if (ctx->buf_len + 1 >= ctx->buf_cap) {
                size_t new_cap = ctx->buf_cap * 2;
                char *new_buf = AGENTC_REALLOC(ctx->buffer, new_cap);
                if (!new_buf) return -1;
                ctx->buffer = new_buf;
                ctx->buf_cap = new_cap;
            }
            ctx->buffer[ctx->buf_len++] = c;
            ctx->buffer[ctx->buf_len] = '\0';
        }
    }

    return 0;
}

agentc_err_t agentc_llm_chat_stream(
    agentc_llm_client_t *client,
    const agentc_chat_request_t *request,
    agentc_llm_stream_callback_t on_chunk,
    agentc_llm_stream_done_callback_t on_done,
    void *user_data
) {
    if (!client || !request || !request->messages) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", client->config.base_url);

    /* Build request body with stream=true */
    char *body = build_chat_request_json(client, request, 1);
    if (!body) {
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Build headers */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", client->config.api_key);

    agentc_http_header_t *headers = NULL;
    agentc_http_header_append(&headers,
        agentc_http_header_create("Content-Type", "application/json"));
    agentc_http_header_append(&headers,
        agentc_http_header_create("Authorization", auth_header));

    /* Stream context */
    stream_parse_ctx_t ctx = {
        .on_chunk = on_chunk,
        .on_done = on_done,
        .user_data = user_data,
        .buffer = AGENTC_MALLOC(1024),
        .buf_len = 0,
        .buf_cap = 1024,
        .total_tokens = 0,
        .finish_reason = NULL,
    };

    if (!ctx.buffer) {
        cJSON_free(body);
        agentc_http_header_free(headers);
        return AGENTC_ERR_NO_MEMORY;
    }
    ctx.buffer[0] = '\0';

    /* Make streaming request */
    agentc_http_stream_request_t req = {
        .base = {
            .url = url,
            .method = AGENTC_HTTP_POST,
            .headers = headers,
            .body = body,
            .body_len = strlen(body),
            .timeout_ms = client->config.timeout_ms,
            .verify_ssl = 1,
        },
        .on_data = stream_data_callback,
        .user_data = &ctx,
    };

    agentc_http_response_t http_resp = {0};
    agentc_err_t err = agentc_http_request_stream(client->http, &req, &http_resp);

    /* Cleanup */
    agentc_http_header_free(headers);
    cJSON_free(body);
    AGENTC_FREE(ctx.buffer);
    AGENTC_FREE(ctx.finish_reason);
    agentc_http_response_free(&http_resp);

    return err;
}
