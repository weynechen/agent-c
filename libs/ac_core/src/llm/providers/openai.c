/**
 * @file openai_api.c
 * @brief OpenAI-compatible API provider
 *
 * Supports:
 * - OpenAI (api.openai.com)
 * - DeepSeek (api.deepseek.com)
 * - Kimi/Moonshot (api.moonshot.cn) - with reasoning_content support
 * - 通义千问 (dashscope.aliyuncs.com)
 * - 智谱AI (open.bigmodel.cn)
 * - Any other OpenAI-compatible endpoint
 */

#include "arc/log.h"
#include "arc/platform.h"
#include "arc/sse_parser.h"
#include "http_client.h"
#include "../llm_provider.h"
#include "../llm_internal.h"
#include "../message/message_json.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * HTTP Pool Integration (weak symbols for optional linking)
 *============================================================================*/

/* Weak declarations - resolved at link time if ac_hosted is linked */
__attribute__((weak)) int ac_http_pool_is_initialized(void);
__attribute__((weak)) arc_http_client_t *ac_http_pool_acquire(uint32_t timeout_ms);
__attribute__((weak)) void ac_http_pool_release(arc_http_client_t *client);

/**
 * @brief Check if HTTP pool is available and initialized
 */
static int http_pool_available(void) {
    return ac_http_pool_is_initialized && ac_http_pool_is_initialized();
}

/**
 * @brief OpenAI provider private data
 */
typedef struct {
    arc_http_client_t *http;  /**< Owned HTTP client (NULL if using pool) */
    int owns_http;               /**< 1 if we created the client, 0 if from pool */
} openai_priv_t;

/**
 * @brief Create OpenAI provider private data
 */
static void* openai_create(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }

    openai_priv_t* priv = ARC_CALLOC(1, sizeof(openai_priv_t));
    if (!priv) {
        return NULL;
    }

    /* Check if HTTP pool is available */
    if (http_pool_available()) {
        /* Will acquire from pool on each request */
        priv->http = NULL;
        priv->owns_http = 0;
        AC_LOG_DEBUG("OpenAI provider initialized (using HTTP pool)");
    } else {
        /* Create own HTTP client */
        arc_http_client_config_t config = {
            .default_timeout_ms = params->timeout_ms,
        };

        arc_err_t err = arc_http_client_create(&config, &priv->http);
        if (err != ARC_OK) {
            ARC_FREE(priv);
            return NULL;
        }
        priv->owns_http = 1;
        AC_LOG_DEBUG("OpenAI provider initialized (using own HTTP client)");
    }

    return priv;
}

/**
 * @brief Perform chat completion
 */
static arc_err_t openai_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    const char* tools,
    ac_chat_response_t* response
) {
    if (!priv_data || !params) {
        return ARC_ERR_INVALID_ARG;
    }

    openai_priv_t* priv = (openai_priv_t*)priv_data;
    arc_http_client_t* http = NULL;
    int from_pool = 0;

    /* Get HTTP client: from pool or owned */
    if (priv->owns_http) {
        http = priv->http;
    } else if (http_pool_available()) {
        http = ac_http_pool_acquire(params->timeout_ms > 0 ? params->timeout_ms : 30000);
        if (!http) {
            AC_LOG_ERROR("OpenAI: failed to acquire HTTP client from pool");
            return ARC_ERR_TIMEOUT;
        }
        from_pool = 1;
    } else {
        AC_LOG_ERROR("OpenAI: no HTTP client available");
        return ARC_ERR_NOT_INITIALIZED;
    }

    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", params->api_base);

    /* Build request body (need to pass params for building JSON) */
    /* Note: build_chat_request_json expects ac_llm_t*, but we only have params */
    /* We'll need to refactor build_chat_request_json to accept params directly */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return ARC_ERR_NO_MEMORY;
    }

    /* Model */
    cJSON_AddStringToObject(root, "model", params->model);

    /* Messages array */
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");

    /* Add system message if instructions provided */
    if (params->instructions) {
        cJSON* sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", params->instructions);
        cJSON_AddItemToArray(msgs_arr, sys_msg);
    }

    /* Add user messages */
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        cJSON* msg_obj = ac_message_to_json(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }

    /* Temperature */
    if (params->temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)params->temperature);
    }

    /* Max tokens */
    if (params->max_tokens > 0) {
        cJSON_AddNumberToObject(root, "max_tokens", params->max_tokens);
    }

    /* Top-p */
    if (params->top_p > 0.0f) {
        cJSON_AddNumberToObject(root, "top_p", (double)params->top_p);
    }

    /* Stream */
    cJSON_AddBoolToObject(root, "stream", 0);

    /* Tools */
    if (tools && strlen(tools) > 0) {
        cJSON* tools_arr = cJSON_Parse(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
            cJSON_AddStringToObject(root, "tool_choice", "auto");
        }
    }

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    AC_LOG_DEBUG("OpenAI request: %s", body);

    /* Build headers */
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", params->api_key);

    arc_http_header_t* headers = NULL;
    arc_http_header_append(&headers,
        arc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    arc_http_header_append(&headers,
        arc_http_header_create("Authorization", auth_header));

    /* Make request */
    arc_http_request_t req = {
        .url = url,
        .method = ARC_HTTP_POST,
        .headers = headers,
        .body = body,
        .body_len = strlen(body),
        .timeout_ms = params->timeout_ms,
        .verify_ssl = 1,
    };

    arc_http_response_t http_resp = {0};
    arc_err_t err = arc_http_request(http, &req, &http_resp);

    /* Cleanup */
    arc_http_header_free(headers);
    cJSON_free(body);

    if (err != ARC_OK) {
        arc_http_response_free(&http_resp);
        if (from_pool) ac_http_pool_release(http);
        return err;
    }

    if (http_resp.status_code != 200) {
        AC_LOG_ERROR("OpenAI HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        arc_http_response_free(&http_resp);
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_HTTP;
    }

    /* Parse response */
    AC_LOG_DEBUG("OpenAI response: %s", http_resp.body);
    err = ac_chat_response_parse(http_resp.body, response);

    arc_http_response_free(&http_resp);

    /* Release HTTP client back to pool */
    if (from_pool) ac_http_pool_release(http);

    return err;
}

/**
 * @brief Cleanup OpenAI provider private data
 */
static void openai_cleanup(void* priv_data) {
    if (!priv_data) {
        return;
    }

    openai_priv_t* priv = (openai_priv_t*)priv_data;

    /* Only destroy HTTP client if we own it (not from pool) */
    if (priv->owns_http && priv->http) {
        arc_http_client_destroy(priv->http);
    }

    ARC_FREE(priv);

    AC_LOG_DEBUG("OpenAI provider cleaned up");
}

/*============================================================================
 * Streaming Implementation
 *============================================================================*/

/**
 * @brief Stream context for OpenAI-compatible APIs
 */
typedef struct {
    ac_stream_callback_t user_callback;
    void* user_data;
    ac_chat_response_t* response;
    sse_parser_t sse;
    
    /* Current state */
    int message_started;
    int in_reasoning;            /**< Currently receiving reasoning_content */
    int in_content;              /**< Currently receiving content */
    int in_tool_call;            /**< Currently receiving tool call */
    
    /* Current tool call state */
    int current_tool_index;
    char* current_tool_id;
    char* current_tool_name;
    char* accumulated_tool_args;
    
    /* Accumulated content */
    char* accumulated_text;
    char* accumulated_reasoning;
    
    int aborted;
} openai_stream_ctx_t;

static void openai_stream_ctx_free(openai_stream_ctx_t* ctx) {
    if (ctx->current_tool_id) ARC_FREE(ctx->current_tool_id);
    if (ctx->current_tool_name) ARC_FREE(ctx->current_tool_name);
    if (ctx->accumulated_tool_args) ARC_FREE(ctx->accumulated_tool_args);
    if (ctx->accumulated_text) ARC_FREE(ctx->accumulated_text);
    if (ctx->accumulated_reasoning) ARC_FREE(ctx->accumulated_reasoning);
    sse_parser_free(&ctx->sse);
}

static void openai_append_string(char** dst, const char* src, size_t len) {
    if (!src || len == 0) return;
    
    if (*dst) {
        size_t old_len = strlen(*dst);
        char* new_str = ARC_REALLOC(*dst, old_len + len + 1);
        if (new_str) {
            memcpy(new_str + old_len, src, len);
            new_str[old_len + len] = '\0';
            *dst = new_str;
        }
    } else {
        *dst = ARC_STRNDUP(src, len);
    }
}

/**
 * @brief Handle OpenAI SSE event
 *
 * OpenAI stream format:
 *   data: {"id":"...", "choices":[{"index":0, "delta":{"content":"..."}}]}
 *   data: [DONE]
 *
 * Kimi K2.5 extension:
 *   data: {"choices":[{"delta":{"reasoning_content":"..."}}]}
 *   (reasoning_content appears before content)
 */
static int openai_handle_sse_event(const sse_event_t* event, void* ctx_ptr) {
    openai_stream_ctx_t* ctx = (openai_stream_ctx_t*)ctx_ptr;
    
    if (!event->data || ctx->aborted) {
        return ctx->aborted ? -1 : 0;
    }
    
    /* Check for stream end */
    if (strcmp(event->data, "[DONE]") == 0) {
        /* Build final blocks from accumulated content */
        if (ctx->response) {
            /* Add reasoning block if present */
            if (ctx->accumulated_reasoning && strlen(ctx->accumulated_reasoning) > 0) {
                ac_content_block_t* block = ARC_CALLOC(1, sizeof(ac_content_block_t));
                if (block) {
                    block->type = AC_BLOCK_REASONING;
                    block->text = ctx->accumulated_reasoning;
                    ctx->accumulated_reasoning = NULL;
                    
                    if (!ctx->response->blocks) {
                        ctx->response->blocks = block;
                    } else {
                        ac_content_block_t* last = ctx->response->blocks;
                        while (last->next) last = last->next;
                        last->next = block;
                    }
                    ctx->response->block_count++;
                }
            }
            
            /* Add text block if present */
            if (ctx->accumulated_text && strlen(ctx->accumulated_text) > 0) {
                ac_content_block_t* block = ARC_CALLOC(1, sizeof(ac_content_block_t));
                if (block) {
                    block->type = AC_BLOCK_TEXT;
                    block->text = ctx->accumulated_text;
                    ctx->accumulated_text = NULL;
                    
                    if (!ctx->response->blocks) {
                        ctx->response->blocks = block;
                    } else {
                        ac_content_block_t* last = ctx->response->blocks;
                        while (last->next) last = last->next;
                        last->next = block;
                    }
                    ctx->response->block_count++;
                    
                    /* Also set legacy content field */
                    ctx->response->content = ARC_STRDUP(block->text);
                }
            }
        }
        
        /* Emit message stop event */
        ac_stream_event_t stream_event = {0};
        stream_event.type = AC_STREAM_MESSAGE_STOP;
        if (ctx->user_callback) {
            ctx->user_callback(&stream_event, ctx->user_data);
        }
        return 0;
    }
    
    /* Parse JSON data */
    cJSON* data = cJSON_Parse(event->data);
    if (!data) {
        AC_LOG_ERROR("Failed to parse OpenAI SSE data: %s", event->data);
        return 0;
    }
    
    ac_stream_event_t stream_event = {0};
    
    /* Emit message start on first chunk */
    if (!ctx->message_started) {
        ctx->message_started = 1;
        stream_event.type = AC_STREAM_MESSAGE_START;
        
        /* Extract ID */
        cJSON* id = cJSON_GetObjectItem(data, "id");
        if (id && cJSON_IsString(id) && ctx->response) {
            ctx->response->id = ARC_STRDUP(cJSON_GetStringValue(id));
        }
        
        if (ctx->user_callback) {
            if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                ctx->aborted = 1;
                cJSON_Delete(data);
                return -1;
            }
        }
    }
    
    /* Process choices array */
    cJSON* choices = cJSON_GetObjectItem(data, "choices");
    if (choices && cJSON_IsArray(choices)) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        if (choice) {
            cJSON* delta = cJSON_GetObjectItem(choice, "delta");
            cJSON* finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
            
            if (delta) {
                /* Check for reasoning_content (Kimi K2.5 thinking) */
                cJSON* reasoning_content = cJSON_GetObjectItem(delta, "reasoning_content");
                if (reasoning_content && cJSON_IsString(reasoning_content)) {
                    const char* text = cJSON_GetStringValue(reasoning_content);
                    size_t text_len = strlen(text);
                    
                    /* Emit block start if first reasoning chunk */
                    if (!ctx->in_reasoning) {
                        ctx->in_reasoning = 1;
                        stream_event.type = AC_STREAM_CONTENT_BLOCK_START;
                        stream_event.block_type = AC_BLOCK_REASONING;
                        stream_event.block_index = 0;
                        if (ctx->user_callback) {
                            ctx->user_callback(&stream_event, ctx->user_data);
                        }
                    }
                    
                    /* Emit delta */
                    stream_event.type = AC_STREAM_DELTA;
                    stream_event.delta_type = AC_DELTA_REASONING;
                    stream_event.block_type = AC_BLOCK_REASONING;
                    stream_event.delta = text;
                    stream_event.delta_len = text_len;
                    
                    openai_append_string(&ctx->accumulated_reasoning, text, text_len);
                    
                    if (ctx->user_callback) {
                        if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                            ctx->aborted = 1;
                            cJSON_Delete(data);
                            return -1;
                        }
                    }
                }
                
                /* Check for content */
                cJSON* content = cJSON_GetObjectItem(delta, "content");
                if (content && cJSON_IsString(content)) {
                    const char* text = cJSON_GetStringValue(content);
                    size_t text_len = strlen(text);
                    
                    /* Close reasoning block if transitioning to content */
                    if (ctx->in_reasoning && !ctx->in_content) {
                        stream_event.type = AC_STREAM_CONTENT_BLOCK_STOP;
                        stream_event.block_type = AC_BLOCK_REASONING;
                        stream_event.block_index = 0;
                        if (ctx->user_callback) {
                            ctx->user_callback(&stream_event, ctx->user_data);
                        }
                    }
                    
                    /* Emit block start if first content chunk */
                    if (!ctx->in_content) {
                        ctx->in_content = 1;
                        stream_event.type = AC_STREAM_CONTENT_BLOCK_START;
                        stream_event.block_type = AC_BLOCK_TEXT;
                        stream_event.block_index = ctx->in_reasoning ? 1 : 0;
                        if (ctx->user_callback) {
                            ctx->user_callback(&stream_event, ctx->user_data);
                        }
                    }
                    
                    /* Emit delta */
                    stream_event.type = AC_STREAM_DELTA;
                    stream_event.delta_type = AC_DELTA_TEXT;
                    stream_event.block_type = AC_BLOCK_TEXT;
                    stream_event.delta = text;
                    stream_event.delta_len = text_len;
                    
                    openai_append_string(&ctx->accumulated_text, text, text_len);
                    
                    if (ctx->user_callback) {
                        if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                            ctx->aborted = 1;
                            cJSON_Delete(data);
                            return -1;
                        }
                    }
                }
                
                /* Check for tool_calls */
                cJSON* tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    cJSON* tc = cJSON_GetArrayItem(tool_calls, 0);
                    if (tc) {
                        cJSON* index = cJSON_GetObjectItem(tc, "index");
                        int tc_index = index ? index->valueint : 0;
                        
                        /* Check if this is a new tool call */
                        cJSON* id = cJSON_GetObjectItem(tc, "id");
                        if (id && cJSON_IsString(id)) {
                            /* New tool call starting */
                            ctx->in_tool_call = 1;
                            ctx->current_tool_index = tc_index;
                            
                            if (ctx->current_tool_id) ARC_FREE(ctx->current_tool_id);
                            ctx->current_tool_id = ARC_STRDUP(cJSON_GetStringValue(id));
                            
                            cJSON* func = cJSON_GetObjectItem(tc, "function");
                            if (func) {
                                cJSON* name = cJSON_GetObjectItem(func, "name");
                                if (name && cJSON_IsString(name)) {
                                    if (ctx->current_tool_name) ARC_FREE(ctx->current_tool_name);
                                    ctx->current_tool_name = ARC_STRDUP(cJSON_GetStringValue(name));
                                }
                            }
                            
                            /* Emit tool block start */
                            stream_event.type = AC_STREAM_CONTENT_BLOCK_START;
                            stream_event.block_type = AC_BLOCK_TOOL_USE;
                            stream_event.block_index = tc_index;
                            stream_event.tool_id = ctx->current_tool_id;
                            stream_event.tool_name = ctx->current_tool_name;
                            if (ctx->user_callback) {
                                ctx->user_callback(&stream_event, ctx->user_data);
                            }
                        }
                        
                        /* Handle function arguments delta */
                        cJSON* func = cJSON_GetObjectItem(tc, "function");
                        if (func) {
                            cJSON* args = cJSON_GetObjectItem(func, "arguments");
                            if (args && cJSON_IsString(args)) {
                                const char* arg_text = cJSON_GetStringValue(args);
                                size_t arg_len = strlen(arg_text);
                                
                                stream_event.type = AC_STREAM_DELTA;
                                stream_event.delta_type = AC_DELTA_INPUT_JSON;
                                stream_event.block_type = AC_BLOCK_TOOL_USE;
                                stream_event.delta = arg_text;
                                stream_event.delta_len = arg_len;
                                
                                openai_append_string(&ctx->accumulated_tool_args, arg_text, arg_len);
                                
                                if (ctx->user_callback) {
                                    ctx->user_callback(&stream_event, ctx->user_data);
                                }
                            }
                        }
                    }
                }
            }
            
            /* Handle finish_reason */
            if (finish_reason && !cJSON_IsNull(finish_reason) && cJSON_IsString(finish_reason)) {
                const char* reason = cJSON_GetStringValue(finish_reason);
                
                /* Close any open blocks */
                if (ctx->in_content) {
                    stream_event.type = AC_STREAM_CONTENT_BLOCK_STOP;
                    stream_event.block_type = AC_BLOCK_TEXT;
                    if (ctx->user_callback) {
                        ctx->user_callback(&stream_event, ctx->user_data);
                    }
                }
                
                if (ctx->in_tool_call) {
                    stream_event.type = AC_STREAM_CONTENT_BLOCK_STOP;
                    stream_event.block_type = AC_BLOCK_TOOL_USE;
                    if (ctx->user_callback) {
                        ctx->user_callback(&stream_event, ctx->user_data);
                    }
                    
                    /* Add tool call to response */
                    if (ctx->response && ctx->current_tool_id && ctx->current_tool_name) {
                        ac_content_block_t* block = ARC_CALLOC(1, sizeof(ac_content_block_t));
                        if (block) {
                            block->type = AC_BLOCK_TOOL_USE;
                            block->id = ctx->current_tool_id;
                            block->name = ctx->current_tool_name;
                            block->input = ctx->accumulated_tool_args;
                            ctx->current_tool_id = NULL;
                            ctx->current_tool_name = NULL;
                            ctx->accumulated_tool_args = NULL;
                            
                            if (!ctx->response->blocks) {
                                ctx->response->blocks = block;
                            } else {
                                ac_content_block_t* last = ctx->response->blocks;
                                while (last->next) last = last->next;
                                last->next = block;
                            }
                            ctx->response->block_count++;
                        }
                    }
                }
                
                /* Store finish reason */
                if (ctx->response) {
                    ctx->response->finish_reason = ARC_STRDUP(reason);
                    ctx->response->stop_reason = ARC_STRDUP(reason);
                }
                
                /* Emit message delta */
                stream_event.type = AC_STREAM_MESSAGE_DELTA;
                stream_event.stop_reason = reason;
                if (ctx->user_callback) {
                    ctx->user_callback(&stream_event, ctx->user_data);
                }
            }
        }
    }
    
    /* Handle usage info */
    cJSON* usage = cJSON_GetObjectItem(data, "usage");
    if (usage && ctx->response) {
        cJSON* pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON* ct = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON* tt = cJSON_GetObjectItem(usage, "total_tokens");
        
        if (pt && cJSON_IsNumber(pt)) {
            ctx->response->input_tokens = pt->valueint;
            ctx->response->prompt_tokens = pt->valueint;
        }
        if (ct && cJSON_IsNumber(ct)) {
            ctx->response->output_tokens = ct->valueint;
            ctx->response->completion_tokens = ct->valueint;
        }
        if (tt && cJSON_IsNumber(tt)) {
            ctx->response->total_tokens = tt->valueint;
        }
    }
    
    cJSON_Delete(data);
    return ctx->aborted ? -1 : 0;
}

static int openai_http_stream_callback(const char* data, size_t len, void* user_data) {
    openai_stream_ctx_t* ctx = (openai_stream_ctx_t*)user_data;
    return sse_parser_feed(&ctx->sse, data, len);
}

/**
 * @brief Perform streaming chat completion
 */
static arc_err_t openai_chat_stream(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    const char* tools,
    ac_stream_callback_t callback,
    void* user_data,
    ac_chat_response_t* response
) {
    if (!priv_data || !params || !callback) {
        return ARC_ERR_INVALID_ARG;
    }

    openai_priv_t* priv = (openai_priv_t*)priv_data;
    arc_http_client_t* http = NULL;
    int from_pool = 0;

    /* Get HTTP client */
    if (priv->owns_http) {
        http = priv->http;
    } else if (http_pool_available()) {
        http = ac_http_pool_acquire(params->timeout_ms > 0 ? params->timeout_ms : 120000);
        if (!http) {
            AC_LOG_ERROR("OpenAI: failed to acquire HTTP client from pool");
            return ARC_ERR_TIMEOUT;
        }
        from_pool = 1;
    } else {
        AC_LOG_ERROR("OpenAI: no HTTP client available");
        return ARC_ERR_NOT_INITIALIZED;
    }

    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", params->api_base);

    /* Build request JSON */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(root, "model", params->model);
    cJSON_AddBoolToObject(root, "stream", 1);  /* Enable streaming */
    
    /* Add stream_options for usage stats (OpenAI compatible) */
    cJSON* stream_opts = cJSON_CreateObject();
    cJSON_AddBoolToObject(stream_opts, "include_usage", 1);
    cJSON_AddItemToObject(root, "stream_options", stream_opts);

    /* Messages array */
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");

    /* Add system message if instructions provided */
    if (params->instructions) {
        cJSON* sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", params->instructions);
        cJSON_AddItemToArray(msgs_arr, sys_msg);
    }

    /* Add user messages */
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        cJSON* msg_obj = ac_message_to_json(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }

    /* Temperature */
    if (params->temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)params->temperature);
    }

    /* Max tokens */
    if (params->max_tokens > 0) {
        cJSON_AddNumberToObject(root, "max_tokens", params->max_tokens);
    }

    /* Top-p */
    if (params->top_p > 0.0f) {
        cJSON_AddNumberToObject(root, "top_p", (double)params->top_p);
    }

    /* Tools */
    if (tools && strlen(tools) > 0) {
        cJSON* tools_arr = cJSON_Parse(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
            cJSON_AddStringToObject(root, "tool_choice", "auto");
        }
    }

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    AC_LOG_DEBUG("OpenAI stream request to %s", url);
    AC_LOG_DEBUG("OpenAI stream body: %s", body);

    /* Build headers */
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", params->api_key);

    arc_http_header_t* headers = NULL;
    arc_http_header_append(&headers,
        arc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    arc_http_header_append(&headers,
        arc_http_header_create("Authorization", auth_header));

    /* Initialize stream context */
    openai_stream_ctx_t ctx = {0};
    ctx.user_callback = callback;
    ctx.user_data = user_data;
    ctx.response = response;
    ctx.current_tool_index = -1;
    sse_parser_init(&ctx.sse, openai_handle_sse_event, &ctx);

    if (response) {
        ac_chat_response_init(response);
    }

    /* Make streaming HTTP request */
    arc_http_stream_request_t req = {
        .base = {
            .url = url,
            .method = ARC_HTTP_POST,
            .headers = headers,
            .body = body,
            .body_len = strlen(body),
            .timeout_ms = params->timeout_ms > 0 ? params->timeout_ms : 120000,
            .verify_ssl = 1,
        },
        .on_data = openai_http_stream_callback,
        .user_data = &ctx,
    };

    arc_http_response_t http_resp = {0};
    arc_err_t err = arc_http_request_stream(http, &req, &http_resp);

    /* Cleanup */
    arc_http_header_free(headers);
    cJSON_free(body);
    openai_stream_ctx_free(&ctx);

    if (from_pool) ac_http_pool_release(http);

    if (err != ARC_OK && !ctx.aborted) {
        AC_LOG_ERROR("OpenAI stream request failed: %d", err);
        return err;
    }

    if (http_resp.status_code != 200 && http_resp.status_code != 0) {
        AC_LOG_ERROR("OpenAI HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        arc_http_response_free(&http_resp);
        return ARC_ERR_HTTP;
    }

    arc_http_response_free(&http_resp);

    AC_LOG_DEBUG("OpenAI stream completed: blocks=%d", 
                 response ? response->block_count : 0);
    return ARC_OK;
}

/**
 * @brief OpenAI provider definition
 *
 * Exported (non-static) so llm.c can register it during lazy initialization.
 * The AC_PROVIDER_REGISTER macro provides automatic registration for custom
 * providers loaded dynamically or in shared libraries.
 */
const ac_llm_ops_t openai_ops = {
    .name = "openai",
    .capabilities = AC_LLM_CAP_TOOLS | AC_LLM_CAP_STREAMING | AC_LLM_CAP_REASONING,
    .create = openai_create,
    .chat = openai_chat,
    .chat_stream = openai_chat_stream,
    .cleanup = openai_cleanup,
};

/* Auto-register provider at program startup (for dynamic/shared library builds) */
AC_PROVIDER_REGISTER(openai, &openai_ops);
