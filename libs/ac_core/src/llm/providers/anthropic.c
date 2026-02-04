/**
 * @file anthropic.c
 * @brief Anthropic Claude API provider
 *
 * Supports Claude models via Anthropic's API.
 * Features:
 * - Extended thinking (thinking blocks with signature)
 * - Tool calling
 * - Content block parsing
 * - Streaming responses
 *
 * API documentation: https://docs.anthropic.com/
 */

#include "../llm_provider.h"
#include "../message/message_json.h"
#include "../sse_parser.h"
#include "arc/message.h"
#include "arc/platform.h"
#include "arc/log.h"
#include "http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define ANTHROPIC_API_VERSION "2023-06-01"
#define ANTHROPIC_THINKING_MIN_BUDGET 1024

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

/*============================================================================
 * Anthropic Provider Private Data
 *============================================================================*/

typedef struct {
    arc_http_client_t *http;  /**< Owned HTTP client (NULL if using pool) */
    int owns_http;               /**< 1 if we created the client, 0 if from pool */
} anthropic_priv_t;

/*============================================================================
 * Provider Implementation
 *============================================================================*/

/**
 * @brief Convert OpenAI tool format to Anthropic format
 *
 * OpenAI: [{"type": "function", "function": {"name": ..., "parameters": ...}}]
 * Anthropic: [{"name": ..., "description": ..., "input_schema": ...}]
 */
static cJSON* convert_tools_to_anthropic(const char* tools_json) {
    if (!tools_json || strlen(tools_json) == 0) {
        return NULL;
    }

    cJSON* input = cJSON_Parse(tools_json);
    if (!input || !cJSON_IsArray(input)) {
        if (input) cJSON_Delete(input);
        return NULL;
    }

    cJSON* output = cJSON_CreateArray();
    if (!output) {
        cJSON_Delete(input);
        return NULL;
    }

    cJSON* tool = NULL;
    cJSON_ArrayForEach(tool, input) {
        cJSON* anthropic_tool = cJSON_CreateObject();
        if (!anthropic_tool) continue;

        /* Check if it's OpenAI format (has "function" wrapper) */
        cJSON* func = cJSON_GetObjectItem(tool, "function");
        if (func) {
            /* OpenAI format - extract from function object */
            cJSON* name = cJSON_GetObjectItem(func, "name");
            cJSON* desc = cJSON_GetObjectItem(func, "description");
            cJSON* params = cJSON_GetObjectItem(func, "parameters");

            if (name && cJSON_IsString(name)) {
                cJSON_AddStringToObject(anthropic_tool, "name", cJSON_GetStringValue(name));
            }
            if (desc && cJSON_IsString(desc)) {
                cJSON_AddStringToObject(anthropic_tool, "description", cJSON_GetStringValue(desc));
            }
            if (params) {
                cJSON_AddItemToObject(anthropic_tool, "input_schema", cJSON_Duplicate(params, 1));
            }
        } else {
            /* Already Anthropic format or simple format - just copy relevant fields */
            cJSON* name = cJSON_GetObjectItem(tool, "name");
            cJSON* desc = cJSON_GetObjectItem(tool, "description");
            cJSON* input_schema = cJSON_GetObjectItem(tool, "input_schema");
            cJSON* params = cJSON_GetObjectItem(tool, "parameters");

            if (name && cJSON_IsString(name)) {
                cJSON_AddStringToObject(anthropic_tool, "name", cJSON_GetStringValue(name));
            }
            if (desc && cJSON_IsString(desc)) {
                cJSON_AddStringToObject(anthropic_tool, "description", cJSON_GetStringValue(desc));
            }
            if (input_schema) {
                cJSON_AddItemToObject(anthropic_tool, "input_schema", cJSON_Duplicate(input_schema, 1));
            } else if (params) {
                cJSON_AddItemToObject(anthropic_tool, "input_schema", cJSON_Duplicate(params, 1));
            }
        }

        cJSON_AddItemToArray(output, anthropic_tool);
    }

    cJSON_Delete(input);
    return output;
}

static void* anthropic_create(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }

    anthropic_priv_t* priv = ARC_CALLOC(1, sizeof(anthropic_priv_t));
    if (!priv) {
        return NULL;
    }

    /* Check if HTTP pool is available */
    if (http_pool_available()) {
        /* Will acquire from pool on each request */
        priv->http = NULL;
        priv->owns_http = 0;
        AC_LOG_DEBUG("Anthropic provider initialized (using HTTP pool)");
    } else {
        /* Create own HTTP client */
        arc_http_client_config_t config = {
            .default_timeout_ms = 60000,  /* Default 60s timeout */
        };

        arc_err_t err = arc_http_client_create(&config, &priv->http);
        if (err != ARC_OK) {
            ARC_FREE(priv);
            return NULL;
        }
        priv->owns_http = 1;
        AC_LOG_DEBUG("Anthropic provider initialized (using own HTTP client)");
    }

    return priv;
}

static arc_err_t anthropic_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    const char* tools,
    ac_chat_response_t* response
) {
    if (!priv_data || !params || !response) {
        return ARC_ERR_INVALID_ARG;
    }

    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;
    arc_http_client_t* http = NULL;
    int from_pool = 0;

    /* Get HTTP client: from pool or owned */
    if (priv->owns_http) {
        http = priv->http;
    } else if (http_pool_available()) {
        http = ac_http_pool_acquire(params->timeout_ms > 0 ? params->timeout_ms : 60000);
        if (!http) {
            AC_LOG_ERROR("Anthropic: failed to acquire HTTP client from pool");
            return ARC_ERR_TIMEOUT;
        }
        from_pool = 1;
    } else {
        AC_LOG_ERROR("Anthropic: no HTTP client available");
        return ARC_ERR_NOT_INITIALIZED;
    }

    /* Build URL */
    const char* api_base = params->api_base ? params->api_base : "https://api.anthropic.com";
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", api_base);

    /* Build request JSON */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(root, "model", params->model);
    cJSON_AddNumberToObject(root, "max_tokens", params->max_tokens > 0 ? params->max_tokens : 4096);

    /* Anthropic uses separate system field, not in messages */
    if (params->instructions) {
        cJSON_AddStringToObject(root, "system", params->instructions);
    }

    /* Thinking configuration */
    if (params->thinking.enabled) {
        cJSON* thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "enabled");
        int budget = params->thinking.budget_tokens;
        if (budget < ANTHROPIC_THINKING_MIN_BUDGET) {
            budget = ANTHROPIC_THINKING_MIN_BUDGET;
        }
        cJSON_AddNumberToObject(thinking, "budget_tokens", budget);
        cJSON_AddItemToObject(root, "thinking", thinking);
    }

    /* Messages array (skip system messages) */
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");

    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        /* Skip system messages (handled separately) */
        if (msg->role == AC_ROLE_SYSTEM) {
            continue;
        }

        /* Use Anthropic-specific format for messages with content blocks */
        cJSON* msg_obj = ac_message_to_json_anthropic(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }

    /* Tools - convert from OpenAI format to Anthropic format */
    if (tools && strlen(tools) > 0) {
        cJSON* tools_arr = convert_tools_to_anthropic(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
        }
    }

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    AC_LOG_DEBUG("Anthropic request to %s: %s", url, body);

    /* Build headers */
    arc_http_header_t* headers = NULL;
    arc_http_header_append(&headers,
        arc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    arc_http_header_append(&headers,
        arc_http_header_create("x-api-key", params->api_key));
    arc_http_header_append(&headers,
        arc_http_header_create("anthropic-version", ANTHROPIC_API_VERSION));

    /* Make HTTP request */
    arc_http_request_t req = {
        .url = url,
        .method = ARC_HTTP_POST,
        .headers = headers,
        .body = body,
        .body_len = strlen(body),
        .timeout_ms = params->timeout_ms > 0 ? params->timeout_ms : 60000,
        .verify_ssl = 1,
    };

    arc_http_response_t http_resp = {0};
    arc_err_t err = arc_http_request(http, &req, &http_resp);

    /* Cleanup */
    arc_http_header_free(headers);
    cJSON_free(body);

    if (err != ARC_OK) {
        AC_LOG_ERROR("Anthropic HTTP request failed: %d", err);
        arc_http_response_free(&http_resp);
        if (from_pool) ac_http_pool_release(http);
        return err;
    }

    if (http_resp.status_code != 200) {
        AC_LOG_ERROR("Anthropic HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        arc_http_response_free(&http_resp);
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_HTTP;
    }

    /* Parse response using Anthropic-specific parser */
    AC_LOG_DEBUG("Anthropic response: %s", http_resp.body);

    err = ac_chat_response_parse_anthropic(http_resp.body, response);

    arc_http_response_free(&http_resp);

    /* Release HTTP client back to pool */
    if (from_pool) ac_http_pool_release(http);

    if (err != ARC_OK) {
        AC_LOG_ERROR("Failed to parse Anthropic response");
        return err;
    }

    AC_LOG_DEBUG("Anthropic chat completed: blocks=%d, content=%s",
                 response->block_count,
                 response->content ? "yes" : "no");
    return ARC_OK;
}

static void anthropic_cleanup(void* priv_data) {
    if (!priv_data) {
        return;
    }

    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;

    /* Only destroy HTTP client if we own it (not from pool) */
    if (priv->owns_http && priv->http) {
        arc_http_client_destroy(priv->http);
    }

    ARC_FREE(priv);

    AC_LOG_DEBUG("Anthropic provider cleaned up");
}

/*============================================================================
 * Streaming Implementation
 *============================================================================*/

typedef struct {
    ac_stream_callback_t user_callback;
    void* user_data;
    ac_chat_response_t* response;
    sse_parser_t sse;
    
    /* Current block state */
    int current_block_index;
    ac_block_type_t current_block_type;
    char* current_tool_id;
    char* current_tool_name;
    
    /* Accumulated content for response */
    char* accumulated_text;
    char* accumulated_thinking;
    char* accumulated_signature;
    char* accumulated_tool_input;
    
    int aborted;
} stream_context_t;

static void stream_ctx_free(stream_context_t* ctx) {
    if (ctx->current_tool_id) ARC_FREE(ctx->current_tool_id);
    if (ctx->current_tool_name) ARC_FREE(ctx->current_tool_name);
    if (ctx->accumulated_text) ARC_FREE(ctx->accumulated_text);
    if (ctx->accumulated_thinking) ARC_FREE(ctx->accumulated_thinking);
    if (ctx->accumulated_signature) ARC_FREE(ctx->accumulated_signature);
    if (ctx->accumulated_tool_input) ARC_FREE(ctx->accumulated_tool_input);
    sse_parser_free(&ctx->sse);
}

static void append_string(char** dst, const char* src, size_t len) {
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

static int handle_sse_event(const sse_event_t* event, void* ctx_ptr) {
    stream_context_t* ctx = (stream_context_t*)ctx_ptr;
    
    if (!event->data || ctx->aborted) {
        return ctx->aborted ? -1 : 0;
    }
    
    /* Parse JSON data */
    cJSON* data = cJSON_Parse(event->data);
    if (!data) {
        AC_LOG_ERROR("Failed to parse SSE data: %s", event->data);
        return 0;
    }
    
    cJSON* type = cJSON_GetObjectItem(data, "type");
    const char* type_str = type && cJSON_IsString(type) ? cJSON_GetStringValue(type) : "";
    
    ac_stream_event_t stream_event = {0};
    
    if (strcmp(type_str, "message_start") == 0) {
        stream_event.type = AC_STREAM_MESSAGE_START;
        
        /* Extract message ID */
        cJSON* message = cJSON_GetObjectItem(data, "message");
        if (message && ctx->response) {
            cJSON* id = cJSON_GetObjectItem(message, "id");
            if (id && cJSON_IsString(id)) {
                ctx->response->id = ARC_STRDUP(cJSON_GetStringValue(id));
            }
        }
        
        if (ctx->user_callback) {
            if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                ctx->aborted = 1;
            }
        }
    }
    else if (strcmp(type_str, "content_block_start") == 0) {
        cJSON* index = cJSON_GetObjectItem(data, "index");
        cJSON* content_block = cJSON_GetObjectItem(data, "content_block");
        
        ctx->current_block_index = index ? index->valueint : 0;
        
        if (content_block) {
            cJSON* block_type = cJSON_GetObjectItem(content_block, "type");
            const char* bt = block_type && cJSON_IsString(block_type) ? 
                             cJSON_GetStringValue(block_type) : "";
            
            if (strcmp(bt, "thinking") == 0) {
                ctx->current_block_type = AC_BLOCK_THINKING;
            } else if (strcmp(bt, "text") == 0) {
                ctx->current_block_type = AC_BLOCK_TEXT;
            } else if (strcmp(bt, "tool_use") == 0) {
                ctx->current_block_type = AC_BLOCK_TOOL_USE;
                
                cJSON* id = cJSON_GetObjectItem(content_block, "id");
                cJSON* name = cJSON_GetObjectItem(content_block, "name");
                
                if (ctx->current_tool_id) ARC_FREE(ctx->current_tool_id);
                if (ctx->current_tool_name) ARC_FREE(ctx->current_tool_name);
                
                ctx->current_tool_id = id && cJSON_IsString(id) ? 
                    ARC_STRDUP(cJSON_GetStringValue(id)) : NULL;
                ctx->current_tool_name = name && cJSON_IsString(name) ? 
                    ARC_STRDUP(cJSON_GetStringValue(name)) : NULL;
            }
        }
        
        stream_event.type = AC_STREAM_CONTENT_BLOCK_START;
        stream_event.block_index = ctx->current_block_index;
        stream_event.block_type = ctx->current_block_type;
        stream_event.tool_id = ctx->current_tool_id;
        stream_event.tool_name = ctx->current_tool_name;
        
        if (ctx->user_callback) {
            if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                ctx->aborted = 1;
            }
        }
    }
    else if (strcmp(type_str, "content_block_delta") == 0) {
        cJSON* delta = cJSON_GetObjectItem(data, "delta");
        if (delta) {
            cJSON* delta_type = cJSON_GetObjectItem(delta, "type");
            const char* dt = delta_type && cJSON_IsString(delta_type) ? 
                             cJSON_GetStringValue(delta_type) : "";
            
            stream_event.type = AC_STREAM_DELTA;
            stream_event.block_index = ctx->current_block_index;
            stream_event.block_type = ctx->current_block_type;
            
            if (strcmp(dt, "thinking_delta") == 0) {
                cJSON* thinking = cJSON_GetObjectItem(delta, "thinking");
                if (thinking && cJSON_IsString(thinking)) {
                    const char* text = cJSON_GetStringValue(thinking);
                    stream_event.delta_type = AC_DELTA_THINKING;
                    stream_event.delta = text;
                    stream_event.delta_len = strlen(text);
                    
                    append_string(&ctx->accumulated_thinking, text, stream_event.delta_len);
                }
            }
            else if (strcmp(dt, "text_delta") == 0) {
                cJSON* text_obj = cJSON_GetObjectItem(delta, "text");
                if (text_obj && cJSON_IsString(text_obj)) {
                    const char* text = cJSON_GetStringValue(text_obj);
                    stream_event.delta_type = AC_DELTA_TEXT;
                    stream_event.delta = text;
                    stream_event.delta_len = strlen(text);
                    
                    append_string(&ctx->accumulated_text, text, stream_event.delta_len);
                }
            }
            else if (strcmp(dt, "input_json_delta") == 0) {
                cJSON* partial_json = cJSON_GetObjectItem(delta, "partial_json");
                if (partial_json && cJSON_IsString(partial_json)) {
                    const char* text = cJSON_GetStringValue(partial_json);
                    stream_event.delta_type = AC_DELTA_INPUT_JSON;
                    stream_event.delta = text;
                    stream_event.delta_len = strlen(text);
                    
                    append_string(&ctx->accumulated_tool_input, text, stream_event.delta_len);
                }
            }
            else if (strcmp(dt, "signature_delta") == 0) {
                cJSON* sig = cJSON_GetObjectItem(delta, "signature");
                if (sig && cJSON_IsString(sig)) {
                    const char* text = cJSON_GetStringValue(sig);
                    stream_event.delta_type = AC_DELTA_SIGNATURE;
                    stream_event.delta = text;
                    stream_event.delta_len = strlen(text);
                    
                    append_string(&ctx->accumulated_signature, text, stream_event.delta_len);
                }
            }
            
            if (ctx->user_callback && stream_event.delta) {
                if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                    ctx->aborted = 1;
                }
            }
        }
    }
    else if (strcmp(type_str, "content_block_stop") == 0) {
        stream_event.type = AC_STREAM_CONTENT_BLOCK_STOP;
        stream_event.block_index = ctx->current_block_index;
        stream_event.block_type = ctx->current_block_type;
        
        /* Build content block for response */
        if (ctx->response) {
            ac_content_block_t* block = ARC_CALLOC(1, sizeof(ac_content_block_t));
            if (block) {
                block->type = ctx->current_block_type;
                
                if (ctx->current_block_type == AC_BLOCK_THINKING) {
                    block->text = ctx->accumulated_thinking;
                    block->signature = ctx->accumulated_signature;
                    ctx->accumulated_thinking = NULL;
                    ctx->accumulated_signature = NULL;
                }
                else if (ctx->current_block_type == AC_BLOCK_TEXT) {
                    block->text = ctx->accumulated_text;
                    ctx->accumulated_text = NULL;
                }
                else if (ctx->current_block_type == AC_BLOCK_TOOL_USE) {
                    block->id = ctx->current_tool_id;
                    block->name = ctx->current_tool_name;
                    block->input = ctx->accumulated_tool_input;
                    ctx->current_tool_id = NULL;
                    ctx->current_tool_name = NULL;
                    ctx->accumulated_tool_input = NULL;
                }
                
                /* Append to response blocks */
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
        
        if (ctx->user_callback) {
            if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                ctx->aborted = 1;
            }
        }
    }
    else if (strcmp(type_str, "message_delta") == 0) {
        cJSON* delta = cJSON_GetObjectItem(data, "delta");
        if (delta) {
            cJSON* stop_reason = cJSON_GetObjectItem(delta, "stop_reason");
            if (stop_reason && cJSON_IsString(stop_reason) && ctx->response) {
                ctx->response->stop_reason = ARC_STRDUP(cJSON_GetStringValue(stop_reason));
                ctx->response->finish_reason = ARC_STRDUP(cJSON_GetStringValue(stop_reason));
            }
        }
        
        cJSON* usage = cJSON_GetObjectItem(data, "usage");
        if (usage && ctx->response) {
            cJSON* ot = cJSON_GetObjectItem(usage, "output_tokens");
            if (ot && cJSON_IsNumber(ot)) {
                ctx->response->output_tokens = ot->valueint;
                ctx->response->completion_tokens = ot->valueint;
            }
        }
        
        stream_event.type = AC_STREAM_MESSAGE_DELTA;
        stream_event.stop_reason = ctx->response ? ctx->response->stop_reason : NULL;
        stream_event.output_tokens = ctx->response ? ctx->response->output_tokens : 0;
        
        if (ctx->user_callback) {
            if (ctx->user_callback(&stream_event, ctx->user_data) != 0) {
                ctx->aborted = 1;
            }
        }
    }
    else if (strcmp(type_str, "message_stop") == 0) {
        stream_event.type = AC_STREAM_MESSAGE_STOP;
        
        if (ctx->user_callback) {
            ctx->user_callback(&stream_event, ctx->user_data);
        }
    }
    else if (strcmp(type_str, "error") == 0) {
        cJSON* error = cJSON_GetObjectItem(data, "error");
        if (error) {
            cJSON* msg = cJSON_GetObjectItem(error, "message");
            stream_event.type = AC_STREAM_ERROR;
            stream_event.error_msg = msg && cJSON_IsString(msg) ? 
                cJSON_GetStringValue(msg) : "Unknown error";
            
            if (ctx->user_callback) {
                ctx->user_callback(&stream_event, ctx->user_data);
            }
        }
        ctx->aborted = 1;
    }
    
    cJSON_Delete(data);
    return ctx->aborted ? -1 : 0;
}

static int http_stream_callback(const char* data, size_t len, void* user_data) {
    stream_context_t* ctx = (stream_context_t*)user_data;
    return sse_parser_feed(&ctx->sse, data, len);
}

static arc_err_t anthropic_chat_stream(
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

    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;
    arc_http_client_t* http = NULL;
    int from_pool = 0;

    /* Get HTTP client */
    if (priv->owns_http) {
        http = priv->http;
    } else if (http_pool_available()) {
        http = ac_http_pool_acquire(params->timeout_ms > 0 ? params->timeout_ms : 120000);
        if (!http) {
            AC_LOG_ERROR("Anthropic: failed to acquire HTTP client from pool");
            return ARC_ERR_TIMEOUT;
        }
        from_pool = 1;
    } else {
        AC_LOG_ERROR("Anthropic: no HTTP client available");
        return ARC_ERR_NOT_INITIALIZED;
    }

    /* Build URL */
    const char* api_base = params->api_base ? params->api_base : "https://api.anthropic.com";
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", api_base);

    /* Build request JSON */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(root, "model", params->model);
    cJSON_AddNumberToObject(root, "max_tokens", params->max_tokens > 0 ? params->max_tokens : 4096);
    cJSON_AddBoolToObject(root, "stream", 1);  /* Enable streaming */

    if (params->instructions) {
        cJSON_AddStringToObject(root, "system", params->instructions);
    }

    /* Thinking configuration */
    if (params->thinking.enabled) {
        cJSON* thinking = cJSON_CreateObject();
        cJSON_AddStringToObject(thinking, "type", "enabled");
        int budget = params->thinking.budget_tokens;
        if (budget < ANTHROPIC_THINKING_MIN_BUDGET) {
            budget = ANTHROPIC_THINKING_MIN_BUDGET;
        }
        cJSON_AddNumberToObject(thinking, "budget_tokens", budget);
        cJSON_AddItemToObject(root, "thinking", thinking);
    }

    /* Messages array */
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        if (msg->role == AC_ROLE_SYSTEM) continue;
        cJSON* msg_obj = ac_message_to_json_anthropic(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }

    /* Tools - convert from OpenAI format to Anthropic format */
    if (tools && strlen(tools) > 0) {
        cJSON* tools_arr = convert_tools_to_anthropic(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
        }
    }

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        if (from_pool) ac_http_pool_release(http);
        return ARC_ERR_NO_MEMORY;
    }

    AC_LOG_DEBUG("Anthropic stream request to %s", url);
    AC_LOG_DEBUG("Anthropic stream body: %s", body);

    /* Build headers */
    arc_http_header_t* headers = NULL;
    arc_http_header_append(&headers,
        arc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    arc_http_header_append(&headers,
        arc_http_header_create("x-api-key", params->api_key));
    arc_http_header_append(&headers,
        arc_http_header_create("anthropic-version", ANTHROPIC_API_VERSION));

    /* Initialize stream context */
    stream_context_t ctx = {0};
    ctx.user_callback = callback;
    ctx.user_data = user_data;
    ctx.response = response;
    ctx.current_block_index = -1;
    sse_parser_init(&ctx.sse, handle_sse_event, &ctx);

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
        .on_data = http_stream_callback,
        .user_data = &ctx,
    };

    arc_http_response_t http_resp = {0};
    arc_err_t err = arc_http_request_stream(http, &req, &http_resp);

    /* Cleanup */
    arc_http_header_free(headers);
    cJSON_free(body);
    stream_ctx_free(&ctx);

    if (from_pool) ac_http_pool_release(http);

    if (err != ARC_OK && !ctx.aborted) {
        AC_LOG_ERROR("Anthropic stream request failed: %d", err);
        return err;
    }

    if (http_resp.status_code != 200 && http_resp.status_code != 0) {
        AC_LOG_ERROR("Anthropic HTTP %d", http_resp.status_code);
        return ARC_ERR_HTTP;
    }

    /* Set legacy content field from accumulated text */
    if (response && response->blocks) {
        for (ac_content_block_t* b = response->blocks; b; b = b->next) {
            if (b->type == AC_BLOCK_TEXT && b->text && !response->content) {
                response->content = ARC_STRDUP(b->text);
                break;
            }
        }
    }

    AC_LOG_DEBUG("Anthropic stream completed: blocks=%d", 
                 response ? response->block_count : 0);
    return ARC_OK;
}

/*============================================================================
 * Provider Registration
 *============================================================================*/

const ac_llm_ops_t anthropic_ops = {
    .name = "anthropic",
    .capabilities = AC_LLM_CAP_THINKING | AC_LLM_CAP_TOOLS | AC_LLM_CAP_STREAMING,
    .create = anthropic_create,
    .chat = anthropic_chat,
    .chat_stream = anthropic_chat_stream,
    .cleanup = anthropic_cleanup,
};

AC_PROVIDER_REGISTER(anthropic, &anthropic_ops);
