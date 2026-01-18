/**
 * @file llm_provider.h
 * @brief Internal LLM provider interface
 * 
 * This header defines the provider interface used internally by llm.c
 * to route requests to different LLM backends.
 */

#ifndef AGENTC_LLM_PROVIDER_H
#define AGENTC_LLM_PROVIDER_H

#include "agentc/llm.h"
#include "agentc/http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provider interface
 * 
 * Each provider implements these functions to handle
 * LLM-specific request building and response parsing.
 */
typedef struct ac_llm_provider {
    const char* name;  /**< Provider name (for logging) */
    
    /**
     * @brief Check if this provider can handle the given parameters
     * 
     * @param params LLM parameters
     * @return 1 if can handle, 0 otherwise
     */
    int (*can_handle)(const ac_llm_params_t* params);
    
    /**
     * @brief Perform chat completion
     * 
     * @param llm LLM client
     * @param messages Message history
     * @param tools Tools JSON
     * @param response Output response
     * @return AGENTC_OK on success
     */
    agentc_err_t (*chat)(
        ac_llm_t* llm,
        const ac_message_t* messages,
        const char* tools,
        ac_chat_response_t* response
    );
} ac_llm_provider_t;

/**
 * @brief Built-in providers
 */
extern const ac_llm_provider_t ac_provider_openai;
extern const ac_llm_provider_t ac_provider_anthropic;

/**
 * @brief Find the appropriate provider for given parameters
 * 
 * @param params LLM parameters
 * @return Provider, or NULL if none found
 */
const ac_llm_provider_t* ac_llm_find_provider(const ac_llm_params_t* params);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_PROVIDER_H */
