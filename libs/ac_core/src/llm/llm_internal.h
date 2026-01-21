/**
 * @file llm_internal.h
 * @brief Internal helper functions shared between llm.c and providers
 */

#ifndef AGENTC_LLM_INTERNAL_H
#define AGENTC_LLM_INTERNAL_H

#include "agentc/llm.h"
#include "agentc/tool.h"
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
    const ac_llm_ops_t *ops;  /* Provider operations (bound at creation) */
    void *priv;                /* Provider private data (allocated by provider) */
    
    /* Owned copies of string parameters */
    char *provider_copy;
    char *compatible_copy;
    char *model_copy;
    char *api_key_copy;
    char *api_base_copy;
    char *instructions_copy;
    char *organization_copy;
};

/*============================================================================
 * Request Building (used by providers)
 *============================================================================*/

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
 * @brief Parse chat response JSON
 * 
 * @param json Response JSON string
 * @param response Output response structure (must be zero-initialized)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_chat_response_parse(
    const char* json,
    ac_chat_response_t* response
);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_INTERNAL_H */
