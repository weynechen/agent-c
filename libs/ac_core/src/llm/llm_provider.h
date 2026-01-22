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
#include "agentc/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provider operations
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
     * @param messages Message history (linked list)
     * @param response_buffer Buffer for response (allocated by caller)
     * @param buffer_size Size of response buffer
     * @return AGENTC_OK on success
     */
    agentc_err_t (*chat)(
        void* priv,
        const ac_llm_params_t* params,
        const ac_message_t* messages,
        char* response_buffer,
        size_t buffer_size
    );
    
    /**
     * @brief Cleanup provider private data
     * 
     * Called during arena destruction to free provider-specific resources.
     * 
     * @param priv Provider private data (returned by create)
     */
    void (*cleanup)(void* priv);
} ac_llm_ops_t;

/**
 * @brief Register a provider (called by provider modules)
 * 
 * Providers use AC_PROVIDER_REGISTER macro to auto-register at startup.
 * 
 * @param name Provider name (e.g., "openai", "anthropic")
 * @param ops Provider operations
 */
void ac_llm_register_provider(const char *name, const ac_llm_ops_t *ops);

/**
 * @brief Find provider by name
 * 
 * @param name Provider name
 * @return Provider operations, or NULL if not found
 */
const ac_llm_ops_t* ac_llm_find_provider_by_name(const char *name);

/**
 * @brief Find the appropriate provider for given parameters
 * 
 * Selection logic:
 * 1. If params->provider is set, use it directly
 * 2. If params->compatible is set, use that provider
 * 3. Otherwise, auto-detect based on model/api_base
 * 
 * @param params LLM parameters
 * @return Provider operations, or NULL if none found
 */
const ac_llm_ops_t* ac_llm_find_provider(const ac_llm_params_t* params);

/**
 * @brief Auto-registration macro (constructor pattern)
 * 
 * Usage in provider file:
 * @code
 * static const ac_llm_ops_t openai_ops = { ... };
 * AC_PROVIDER_REGISTER(openai, &openai_ops);
 * @endcode
 */
#if defined(_MSC_VER)
    #define PROVIDER_CONSTRUCTOR __pragma(section(".CRT$XCT")) \
        __declspec(allocate(".CRT$XCT"))
    #define PROVIDER_CALL_CONSTRUCTOR(f) \
        PROVIDER_CONSTRUCTOR static void (__cdecl *f##_)(void) = f;
#else
    #define PROVIDER_CALL_CONSTRUCTOR(f) \
        __attribute__((constructor)) static void f(void)
#endif

#define AC_PROVIDER_REGISTER(name, ops) \
    PROVIDER_CALL_CONSTRUCTOR(ac_register_provider_##name) { \
        ac_llm_register_provider(#name, ops); \
    } \
    extern int ac_provider_##name##_dummy

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_PROVIDER_H */
