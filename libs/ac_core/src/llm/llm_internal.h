/**
 * @file llm_internal.h
 * @brief Internal helper functions shared between llm.c and providers
 */

#ifndef AGENTC_LLM_INTERNAL_H
#define AGENTC_LLM_INTERNAL_H

#include "agentc/llm.h"
#include "agentc/tool.h"
#include "agentc/http_client.h"
#include "llm_provider.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Internal Structure Definition
 *============================================================================*/

/**
 * @brief Internal LLM client structure
 * 
 * This structure is defined here so providers and internal functions
 * can access the members.
 */
struct ac_llm {
    ac_llm_params_t params;
    agentc_http_client_t *http;
    const ac_llm_provider_t *provider;  /* Provider implementation */
    /* Owned copies */
    char *model_copy;
    char *api_key_copy;
    char *api_base_copy;
    char *instructions_copy;
    char *organization_copy;
};

/**
 * @brief Build message JSON object
 * 
 * @param msg Message to convert
 * @return cJSON object (caller must delete)
 */
cJSON* build_message_json(const ac_message_t* msg);

/**
 * @brief Build chat request JSON
 * 
 * @param llm LLM client
 * @param messages Message list
 * @param tools Tools JSON string
 * @return JSON string (caller must free with cJSON_free)
 */
char* build_chat_request_json(
    const ac_llm_t* llm,
    const ac_message_t* messages,
    const char* tools
);

/**
 * @brief Parse tool calls from JSON array
 * 
 * @param tool_calls_arr cJSON array of tool calls
 * @return Tool call list (caller must free)
 */
ac_tool_call_t* parse_tool_calls(cJSON* tool_calls_arr);

/**
 * @brief Parse chat response JSON
 * 
 * @param json Response JSON string
 * @param response Output response structure
 * @return AGENTC_OK on success
 */
agentc_err_t parse_chat_response(
    const char* json,
    ac_chat_response_t* response
);

/**
 * @brief Get HTTP client from LLM
 * 
 * Providers need access to the HTTP client to make requests.
 * 
 * @param llm LLM client
 * @return HTTP client handle
 */
static inline agentc_http_client_t* ac_llm_get_http_client(const ac_llm_t* llm) {
    return llm ? llm->http : NULL;
}

/**
 * @brief Get parameters from LLM
 * 
 * @param llm LLM client
 * @return Parameters structure
 */
static inline const ac_llm_params_t* ac_llm_get_params(const ac_llm_t* llm) {
    return llm ? &llm->params : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_INTERNAL_H */
