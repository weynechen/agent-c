/**
 * @file provider.c
 * @brief LLM provider registration and lookup
 */

#include "llm_provider.h"
#include "agentc/log.h"
#include <string.h>

/*============================================================================
 * Provider Registry
 *============================================================================*/

#define MAX_PROVIDERS 32

typedef struct {
    const char* name;
    const ac_llm_ops_t* ops;
} provider_entry_t;

static provider_entry_t s_providers[MAX_PROVIDERS];
static int s_provider_count = 0;
static int s_providers_initialized = 0;

/*============================================================================
 * Built-in Provider Forward Declarations
 *============================================================================*/

/* These are defined in provider implementation files */
extern const ac_llm_ops_t openai_ops;
extern const ac_llm_ops_t anthropic_ops;

/*============================================================================
 * Provider Initialization
 *============================================================================*/

/**
 * @brief Initialize built-in providers
 * 
 * This is called lazily on first use to ensure built-in providers
 * are registered even in static library builds where constructors
 * may not execute automatically.
 */
static void ac_llm_init_builtin_providers(void) {
    if (s_providers_initialized) {
        return;
    }
    
    /* Register built-in providers manually */
    /* Note: AC_PROVIDER_REGISTER macro uses constructor attribute which may not
     * work reliably in static library builds, so we manually register here */
    ac_llm_register_provider("openai", &openai_ops);
    ac_llm_register_provider("anthropic", &anthropic_ops);
    
    s_providers_initialized = 1;
    AC_LOG_DEBUG("Built-in providers initialized (openai, anthropic)");
}

/*============================================================================
 * Provider Registration
 *============================================================================*/

void ac_llm_register_provider(const char *name, const ac_llm_ops_t *ops) {
    if (!name || !ops) {
        AC_LOG_ERROR("Invalid provider registration: name or ops is NULL");
        return;
    }
    
    if (s_provider_count >= MAX_PROVIDERS) {
        AC_LOG_ERROR("Provider registry full, cannot register: %s", name);
        return;
    }
    
    // Check for duplicates
    for (int i = 0; i < s_provider_count; i++) {
        if (strcmp(s_providers[i].name, name) == 0) {
            AC_LOG_WARN("Provider '%s' already registered, skipping", name);
            return;
        }
    }
    
    s_providers[s_provider_count].name = name;
    s_providers[s_provider_count].ops = ops;
    s_provider_count++;
    
    AC_LOG_DEBUG("Provider registered: %s", name);
}

/*============================================================================
 * Provider Lookup
 *============================================================================*/

const ac_llm_ops_t* ac_llm_find_provider_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < s_provider_count; i++) {
        if (strcmp(s_providers[i].name, name) == 0) {
            return s_providers[i].ops;
        }
    }
    
    return NULL;
}

const ac_llm_ops_t* ac_llm_find_provider(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }
    
    /* Ensure built-in providers are registered */
    ac_llm_init_builtin_providers();

    if (params->provider == NULL) {
        AC_LOG_ERROR("Please set llm provider");
    }
     
    /* Strategy 1: Use compatible mode (e.g., "openai" for OpenAI-compatible APIs) */
    if (params->compatible && params->compatible[0] != '\0') {
        const ac_llm_ops_t* ops = ac_llm_find_provider_by_name(params->compatible);
        if (ops) {
            AC_LOG_DEBUG("Using provider: %s (compatible mode)", params->compatible);
            return ops;
        }
    }

    /* Strategy 2: Use explicitly specified provider */
    if (params->provider && params->provider[0] != '\0') {
        const ac_llm_ops_t* ops = ac_llm_find_provider_by_name(params->provider);
        if (ops) {
            AC_LOG_DEBUG("Using provider: %s (explicit)", params->provider);
            return ops;
        }
        AC_LOG_WARN("Provider '%s' not found", params->provider);
    }
       
    /* No provider found */
    AC_LOG_ERROR("No suitable provider found for provider=%s", params->provider);
    return NULL;
}
