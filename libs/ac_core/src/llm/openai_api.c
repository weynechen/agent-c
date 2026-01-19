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
#include "llm_provider.h"
#include "llm_internal.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Check if this provider can handle the request
 */
static int openai_can_handle(const ac_llm_params_t* params) {
    if (!params || !params->api_base) {
        return 0;
    }
    
    const char* base = params->api_base;
    
    // OpenAI domains
    if (strstr(base, "api.openai.com") || 
        strstr(base, "openai.com")) {
        return 1;
    }
    
    // DeepSeek
    if (strstr(base, "api.deepseek.com") ||
        strstr(base, "deepseek.com")) {
        return 1;
    }
    
    // 通义千问 (Qwen)
    if (strstr(base, "dashscope.aliyuncs.com")) {
        return 1;
    }
    
    // 智谱AI (GLM)
    if (strstr(base, "bigmodel.cn")) {
        return 1;
    }
    
    // Default: assume OpenAI-compatible if no other provider matches
    // (This provider is the fallback)
    return 1;
}

/**
 * @brief Perform chat completion
 */
static agentc_err_t openai_chat(
    ac_llm_t* llm,
    const ac_message_t* messages,
    const char* tools,
    ac_chat_response_t* response
) {
    const ac_llm_params_t* params = ac_llm_get_params(llm);
    agentc_http_client_t* http = ac_llm_get_http_client(llm);
    
    if (!params || !http) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", params->api_base);
    
    /* Build request body */
    char* body = build_chat_request_json(llm, messages, tools);
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
    err = parse_chat_response(http_resp.body, response);
    
    agentc_http_response_free(&http_resp);
    return err;
}

/**
 * @brief OpenAI provider definition
 */
const ac_llm_provider_t ac_provider_openai = {
    .name = "OpenAI",
    .can_handle = openai_can_handle,
    .chat = openai_chat,
};
