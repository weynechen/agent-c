/**
 * @file anthropic.c
 * @brief Anthropic Claude API provider
 * 
 * Supports Claude models via Anthropic's API.
 * API documentation: https://docs.anthropic.com/
 */

#include "../llm_provider.h"
#include "agentc/message.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include "http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define ANTHROPIC_API_VERSION "2023-06-01"

/*============================================================================
 * Anthropic Provider Private Data
 *============================================================================*/

typedef struct {
    agentc_http_client_t *http;
} anthropic_priv_t;

/*============================================================================
 * Provider Implementation
 *============================================================================*/

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
        .default_timeout_ms = 60000,  // Default 60s timeout
    };
    
    agentc_err_t err = agentc_http_client_create(&config, &priv->http);
    if (err != AGENTC_OK) {
        AGENTC_FREE(priv);
        return NULL;
    }
    
    AC_LOG_DEBUG("Anthropic provider initialized");
    return priv;
}

static agentc_err_t anthropic_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    char* response_buffer,
    size_t buffer_size
) {
    if (!priv_data || !params || !messages || !response_buffer) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;
    
    /* Build URL */
    const char* api_base = params->api_base ? params->api_base : "https://api.anthropic.com";
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", api_base);
    
    /* Build request JSON */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return AGENTC_ERR_NO_MEMORY;
    }
    
    cJSON_AddStringToObject(root, "model", params->model);
    cJSON_AddNumberToObject(root, "max_tokens", 4096);  // Anthropic requires this
    
    /* Anthropic uses separate system field, not in messages */
    if (params->instructions) {
        cJSON_AddStringToObject(root, "system", params->instructions);
    }
    
    /* Messages array (skip system messages) */
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");
    
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        // Skip system messages (handled separately)
        if (msg->role == AC_ROLE_SYSTEM) {
            continue;
        }
        
        cJSON* msg_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_obj, "role", ac_role_to_string(msg->role));
        if (msg->content) {
            cJSON_AddStringToObject(msg_obj, "content", msg->content);
        }
        cJSON_AddItemToArray(msgs_arr, msg_obj);
    }
    
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!body) {
        return AGENTC_ERR_NO_MEMORY;
    }
    
    AC_LOG_DEBUG("Anthropic request to %s: %s", url, body);
    
    /* Build headers */
    agentc_http_header_t* headers = NULL;
    agentc_http_header_append(&headers,
        agentc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    agentc_http_header_append(&headers,
        agentc_http_header_create("x-api-key", params->api_key));
    agentc_http_header_append(&headers,
        agentc_http_header_create("anthropic-version", ANTHROPIC_API_VERSION));
    
    /* Make HTTP request */
    agentc_http_request_t req = {
        .url = url,
        .method = AGENTC_HTTP_POST,
        .headers = headers,
        .body = body,
        .body_len = strlen(body),
        .timeout_ms = 60000,
        .verify_ssl = 1,
    };
    
    agentc_http_response_t http_resp = {0};
    agentc_err_t err = agentc_http_request(priv->http, &req, &http_resp);
    
    /* Cleanup */
    agentc_http_header_free(headers);
    cJSON_free(body);
    
    if (err != AGENTC_OK) {
        AC_LOG_ERROR("Anthropic HTTP request failed: %d", err);
        agentc_http_response_free(&http_resp);
        return err;
    }
    
    if (http_resp.status_code != 200) {
        AC_LOG_ERROR("Anthropic HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Parse response JSON */
    AC_LOG_DEBUG("Anthropic response: %s", http_resp.body);
    
    cJSON* resp_root = cJSON_Parse(http_resp.body);
    if (!resp_root) {
        AC_LOG_ERROR("Failed to parse Anthropic response JSON");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Extract content from content[0].text */
    cJSON* content_arr = cJSON_GetObjectItem(resp_root, "content");
    if (!content_arr || !cJSON_IsArray(content_arr) || cJSON_GetArraySize(content_arr) == 0) {
        AC_LOG_ERROR("No content in Anthropic response");
        cJSON_Delete(resp_root);
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    cJSON* content_item = cJSON_GetArrayItem(content_arr, 0);
    cJSON* text = cJSON_GetObjectItem(content_item, "text");
    
    if (!text || !cJSON_IsString(text)) {
        AC_LOG_ERROR("No text in Anthropic response");
        cJSON_Delete(resp_root);
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Copy to response buffer */
    const char* text_str = cJSON_GetStringValue(text);
    size_t len = strlen(text_str);
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }
    memcpy(response_buffer, text_str, len);
    response_buffer[len] = '\0';
    
    cJSON_Delete(resp_root);
    agentc_http_response_free(&http_resp);
    
    AC_LOG_DEBUG("Anthropic chat completed");
    return AGENTC_OK;
}

static void anthropic_cleanup(void* priv_data) {
    if (!priv_data) {
        return;
    }
    
    anthropic_priv_t* priv = (anthropic_priv_t*)priv_data;
    agentc_http_client_destroy(priv->http);
    AGENTC_FREE(priv);
    
    AC_LOG_DEBUG("Anthropic provider cleaned up");
}

/*============================================================================
 * Provider Registration
 *============================================================================*/

const ac_llm_ops_t anthropic_ops = {
    .name = "anthropic",
    .create = anthropic_create,
    .chat = anthropic_chat,
    .cleanup = anthropic_cleanup,
};

AC_PROVIDER_REGISTER(anthropic, &anthropic_ops);
