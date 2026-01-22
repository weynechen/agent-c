/**
 * @file openai.c
 * @brief OpenAI-compatible API provider
 * 
 * Supports:
 * - OpenAI (api.openai.com)
 * - DeepSeek (api.deepseek.com)
 * - Any other OpenAI-compatible endpoint
 */

#include "../llm_provider.h"
#include "agentc/message.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include "http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * OpenAI Provider Private Data
 *============================================================================*/

typedef struct {
    agentc_http_client_t *http;
} openai_priv_t;

/*============================================================================
 * Provider Implementation
 *============================================================================*/

static void* openai_create(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }
    
    openai_priv_t* priv = AGENTC_CALLOC(1, sizeof(openai_priv_t));
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
    
    AC_LOG_DEBUG("OpenAI provider initialized");
    return priv;
}

static agentc_err_t openai_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    char* response_buffer,
    size_t buffer_size
) {
    if (!priv_data || !params || !messages || !response_buffer) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    openai_priv_t* priv = (openai_priv_t*)priv_data;
    
    /* Build URL */
    char url[512];
    const char* api_base = params->api_base ? params->api_base : "https://api.openai.com/v1";
    snprintf(url, sizeof(url), "%s/chat/completions", api_base);
    
    /* Build request JSON */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return AGENTC_ERR_NO_MEMORY;
    }
    
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
    
    /* Add message history */
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        cJSON* msg_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_obj, "role", ac_role_to_string(msg->role));
        if (msg->content) {
            cJSON_AddStringToObject(msg_obj, "content", msg->content);
        }
        cJSON_AddItemToArray(msgs_arr, msg_obj);
    }
    
    /* Stream = false */
    cJSON_AddBoolToObject(root, "stream", 0);
    
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!body) {
        return AGENTC_ERR_NO_MEMORY;
    }
    
    AC_LOG_DEBUG("OpenAI request to %s: %s", url, body);
    
    /* Build headers */
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", params->api_key);
    
    agentc_http_header_t* headers = NULL;
    agentc_http_header_append(&headers,
        agentc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    agentc_http_header_append(&headers,
        agentc_http_header_create("Authorization", auth_header));
    
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
        AC_LOG_ERROR("OpenAI HTTP request failed: %d", err);
        agentc_http_response_free(&http_resp);
        return err;
    }
    
    if (http_resp.status_code != 200) {
        AC_LOG_ERROR("OpenAI HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Parse response JSON */
    AC_LOG_DEBUG("OpenAI response: %s", http_resp.body);
    
    cJSON* resp_root = cJSON_Parse(http_resp.body);
    if (!resp_root) {
        AC_LOG_ERROR("Failed to parse OpenAI response JSON");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Extract content from choices[0].message.content */
    cJSON* choices = cJSON_GetObjectItem(resp_root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        AC_LOG_ERROR("No choices in OpenAI response");
        cJSON_Delete(resp_root);
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    cJSON* choice = cJSON_GetArrayItem(choices, 0);
    cJSON* message = cJSON_GetObjectItem(choice, "message");
    cJSON* content = cJSON_GetObjectItem(message, "content");
    
    if (!content || !cJSON_IsString(content)) {
        AC_LOG_ERROR("No content in OpenAI response");
        cJSON_Delete(resp_root);
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Copy to response buffer */
    const char* content_str = cJSON_GetStringValue(content);
    size_t len = strlen(content_str);
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }
    memcpy(response_buffer, content_str, len);
    response_buffer[len] = '\0';
    
    cJSON_Delete(resp_root);
    agentc_http_response_free(&http_resp);
    
    AC_LOG_DEBUG("OpenAI chat completed");
    return AGENTC_OK;
}

static void openai_cleanup(void* priv_data) {
    if (!priv_data) {
        return;
    }
    
    openai_priv_t* priv = (openai_priv_t*)priv_data;
    agentc_http_client_destroy(priv->http);
    AGENTC_FREE(priv);
    
    AC_LOG_DEBUG("OpenAI provider cleaned up");
}

/*============================================================================
 * Provider Registration
 *============================================================================*/

const ac_llm_ops_t openai_ops = {
    .name = "openai",
    .create = openai_create,
    .chat = openai_chat,
    .cleanup = openai_cleanup,
};

AC_PROVIDER_REGISTER(openai, &openai_ops);
