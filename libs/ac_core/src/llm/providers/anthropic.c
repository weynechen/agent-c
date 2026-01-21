/**
 * @file anthropic_api.c
 * @brief Anthropic Claude API provider
 * 
 * Supports Claude models via Anthropic's API.
 * API documentation: https://docs.anthropic.com/
 */

#include "agentc/log.h"
#include "agentc/platform.h"
#include "agentc/tool.h"
#include "http_client.h"
#include "../llm_provider.h"
#include "../llm_internal.h"
#include "../message/message.h"
#include "../message/message_json.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define ANTHROPIC_API_VERSION "2023-06-01"

/**
 * @brief Anthropic provider private data
 */
typedef struct {
    agentc_http_client_t *http;
} anthropic_priv_t;

/**
 * @brief Create Anthropic provider private data
 */
static void* anthropic_create(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }
    
    anthropic_priv_t* priv = AGENTC_CALLOC(1, sizeof(anthropic_priv_t));
    if (!priv) {
        return NULL;
    }
    
    /* Create HTTP client */
    agentc_http_client_config_t config = {
        .default_timeout_ms = params->timeout_ms,
    };
    
    agentc_err_t err = agentc_http_client_create(&config, &priv->http);
    if (err != AGENTC_OK) {
        AGENTC_FREE(priv);
        return NULL;
    }
    
    AC_LOG_DEBUG("Anthropic provider initialized");
    return priv;
}

/**
 * @brief Build Anthropic-specific request JSON
 * 
 * Anthropic API has a different format from OpenAI:
 * - system message is a top-level field, not in messages
 * - max_tokens is required (not optional)
 * - uses "anthropic-version" header
 */
static char* build_anthropic_request_json(
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    const char* tools
) {
    
    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;
    
    /* Model */
    cJSON_AddStringToObject(root, "model", params->model);
    
    /* Max tokens (required for Anthropic) */
    int max_tokens = params->max_tokens > 0 ? params->max_tokens : 4096;
    cJSON_AddNumberToObject(root, "max_tokens", max_tokens);
    
    /* System message (top-level, not in messages array) */
    if (params->instructions) {
        cJSON_AddStringToObject(root, "system", params->instructions);
    }
    
    /* Messages array (no system messages) */
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        if (msg->role == AC_ROLE_SYSTEM) {
            continue;  // Skip system messages (handled separately)
        }
        
        cJSON* msg_obj = ac_message_to_json(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }
    
    /* Temperature */
    if (params->temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)params->temperature);
    }
    
    /* Top-p */
    if (params->top_p > 0.0f) {
        cJSON_AddNumberToObject(root, "top_p", (double)params->top_p);
    }
    
    /* Top-k (Anthropic-specific) */
    if (params->top_k > 0) {
        cJSON_AddNumberToObject(root, "top_k", params->top_k);
    }
    
    /* Tools */
    if (tools && strlen(tools) > 0) {
        cJSON* tools_arr = cJSON_Parse(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
        }
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_str;
}

/**
 * @brief Perform chat completion
 */
static agentc_err_t anthropic_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    const char* tools,
    ac_chat_response_t* response
) {
    if (!priv_data || !params) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;
    agentc_http_client_t* http = priv->http;
    
    /* Build URL */
    const char* base = params->api_base ? params->api_base : "https://api.anthropic.com";
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", base);
    
    /* Build request body */
    char* body = build_anthropic_request_json(params, messages, tools);
    if (!body) {
        return AGENTC_ERR_NO_MEMORY;
    }
    
    AC_LOG_DEBUG("Anthropic request: %s", body);
    
    /* Build headers */
    agentc_http_header_t* headers = NULL;
    agentc_http_header_append(&headers,
        agentc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    agentc_http_header_append(&headers,
        agentc_http_header_create("x-api-key", params->api_key));
    agentc_http_header_append(&headers,
        agentc_http_header_create("anthropic-version", ANTHROPIC_API_VERSION));
    
    /* Make request */
    agentc_http_request_t req = {
        .url = url,
        .method = AGENTC_HTTP_POST,
        .headers = headers,
        .body = body,
        .body_len = strlen(body),
        .timeout_ms = params->timeout_ms,
        .verify_ssl = 1,
    };
    
    agentc_http_response_t http_resp = {0};
    agentc_err_t err = agentc_http_request(http, &req, &http_resp);
    
    /* Cleanup */
    agentc_http_header_free(headers);
    cJSON_free(body);
    
    if (err != AGENTC_OK) {
        agentc_http_response_free(&http_resp);
        return err;
    }
    
    if (http_resp.status_code != 200) {
        AC_LOG_ERROR("Anthropic HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Parse response (Anthropic format is similar enough to OpenAI) */
    AC_LOG_DEBUG("Anthropic response: %s", http_resp.body);
    
    // TODO: Implement Anthropic-specific response parsing if format differs significantly
    // For now, try OpenAI parser (may need adjustment)
    err = ac_chat_response_parse(http_resp.body, response);
    
    agentc_http_response_free(&http_resp);
    return err;
}

/**
 * @brief Cleanup Anthropic provider private data
 */
static void anthropic_cleanup(void* priv_data) {
    if (!priv_data) {
        return;
    }
    
    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;
    agentc_http_client_destroy(priv->http);
    AGENTC_FREE(priv);
    
    AC_LOG_DEBUG("Anthropic provider cleaned up");
}

/**
 * @brief Anthropic provider definition
 * 
 * Exported (non-static) so llm.c can register it during lazy initialization.
 * The AC_PROVIDER_REGISTER macro provides automatic registration for custom
 * providers loaded dynamically or in shared libraries.
 */
const ac_llm_ops_t anthropic_ops = {
    .name = "anthropic",
    .create = anthropic_create,
    .chat = anthropic_chat,
    .cleanup = anthropic_cleanup,
};

/* Auto-register provider at program startup (for dynamic/shared library builds) */
AC_PROVIDER_REGISTER(anthropic, &anthropic_ops)
