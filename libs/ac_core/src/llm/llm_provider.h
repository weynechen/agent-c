/**
 * @file llm_provider.h
 * @brief Internal LLM provider interface
 * 
 * This header defines the provider interface used internally by llm.c
 * to route requests to different LLM backends.
 * 
 * Design: Each provider manages its own private data (similar to Linux driver model).
 */

#ifndef AGENTC_LLM_PROVIDER_H
#define AGENTC_LLM_PROVIDER_H

#include "agentc/llm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provider operations (similar to net_device_ops in Linux)
 * 
 * Each provider implements these functions to manage its own resources
 * and handle LLM-specific request/response processing.
 */
typedef struct ac_llm_ops {
    const char* name;  /**< Provider name (for logging) */
    
    /**
     * @brief Create provider private data
     * 
     * Called during ac_llm_create() to allocate and initialize
     * provider-specific resources (e.g., HTTP client).
     * 
     * @param params LLM parameters
     * @return Provider private data, or NULL on error
     */
    void* (*create)(const ac_llm_params_t* params);
    
    /**
     * @brief Perform chat completion
     * 
     * @param priv Provider private data (returned by create)
     * @param params LLM parameters
     * @param messages Message history
     * @param tools Tools JSON
     * @param response Output response
     * @return AGENTC_OK on success
     */
    agentc_err_t (*chat)(
        void* priv,
        const ac_llm_params_t* params,
        const ac_message_t* messages,
        const char* tools,
        ac_chat_response_t* response
    );
    
    /**
     * @brief Cleanup provider private data
     * 
     * Called during ac_llm_destroy() to free provider-specific resources.
     * 
     * @param priv Provider private data (returned by create)
     */
    void (*cleanup)(void* priv);
} ac_llm_ops_t;

/**
 * @brief Built-in providers
 */
extern const ac_llm_ops_t ac_provider_openai;
extern const ac_llm_ops_t ac_provider_anthropic;

/**
 * @brief Find the appropriate provider for given parameters
 * 
 * Checks model name and api_base to determine which provider to use.
 * 
 * @param params LLM parameters
 * @return Provider operations, or NULL if none found
 */
const ac_llm_ops_t* ac_llm_find_provider(const ac_llm_params_t* params);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_PROVIDER_H */
