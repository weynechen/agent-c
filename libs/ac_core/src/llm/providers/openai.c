/**
 * @file openai_api.c
 * @brief OpenAI-compatible API provider
 * 
 * Supports:
 * - OpenAI (api.openai.com)
 * - DeepSeek (api.deepseek.com)
 * - 通义千问 (dashscope.aliyuncs.com)
 * - 智谱AI (open.bigmodel.cn)
 * - Any other OpenAI-compatible endpoint
 */

#include "agentc/log.h"
#include "agentc/platform.h"
#include "http_client.h"
#include "../llm_provider.h"
#include "../llm_internal.h"
#include "../message/message_json.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief OpenAI provider private data
 */
typedef struct {
    agentc_http_client_t *http;
} openai_priv_t;

/**
 * @brief Create OpenAI provider private data
 */
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
        .default_timeout_ms = params->timeout_ms,
    };
    
    agentc_err_t err = agentc_http_client_create(&config, &priv->http);
    if (err != AGENTC_OK) {
        AGENTC_FREE(priv);
        return NULL;
    }
    
    AC_LOG_DEBUG("OpenAI provider initialized");
    return priv;
}

/**
 * @brief Perform chat completion
 */
static agentc_err_t openai_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,
    const char* tools,
    ac_chat_response_t* response
) {
    if (!priv_data || !params) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    openai_priv_t* priv = (openai_priv_t*)priv_data;
    agentc_http_client_t* http = priv->http;
    
    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", params->api_base);
    
    /* Build request body (need to pass params for building JSON) */
    /* Note: build_chat_request_json expects ac_llm_t*, but we only have params */
    /* We'll need to refactor build_chat_request_json to accept params directly */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return AGENTC_ERR_NO_MEMORY;
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
        return AGENTC_ERR_NO_MEMORY;
    }
    
    AC_LOG_DEBUG("OpenAI request: %s", body);
    
    /* Build headers */
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", params->api_key);
    
    agentc_http_header_t* headers = NULL;
    agentc_http_header_append(&headers,
        agentc_http_header_create("Content-Type", "application/json; charset=utf-8"));
    agentc_http_header_append(&headers,
        agentc_http_header_create("Authorization", auth_header));
    
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
        AC_LOG_ERROR("OpenAI HTTP %d: %s", http_resp.status_code,
            http_resp.body ? http_resp.body : "");
        agentc_http_response_free(&http_resp);
        return AGENTC_ERR_HTTP;
    }
    
    /* Parse response */
    AC_LOG_DEBUG("OpenAI response: %s", http_resp.body);
    err = ac_chat_response_parse(http_resp.body, response);
    
    agentc_http_response_free(&http_resp);
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
    agentc_http_client_destroy(priv->http);
    AGENTC_FREE(priv);
    
    AC_LOG_DEBUG("OpenAI provider cleaned up");
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
    .create = openai_create,
    .chat = openai_chat,
    .cleanup = openai_cleanup,
};

/* Auto-register provider at program startup (for dynamic/shared library builds) */
AC_PROVIDER_REGISTER(openai, &openai_ops)
