/**
 * @file llm.c
 * @brief LLM implementation with arena allocation
 */

#include "agentc/llm.h"
#include "agentc/log.h"
#include "llm_internal.h"
#include "llm_provider.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * LLM Implementation
 *============================================================================*/

ac_llm_t* ac_llm_create(arena_t* arena, const ac_llm_params_t* params) {
    if (!arena || !params) {
        AC_LOG_ERROR("Invalid arguments to ac_llm_create");
        return NULL;
    }
    
    if (!params->model || !params->api_key) {
        AC_LOG_ERROR("model and api_key are required");
        return NULL;
    }
    
    // Allocate LLM structure from arena
    ac_llm_t* llm = (ac_llm_t*)arena_alloc(arena, sizeof(ac_llm_t));
    if (!llm) {
        AC_LOG_ERROR("Failed to allocate LLM from arena");
        return NULL;
    }
    
    llm->arena = arena;
    
    // Copy params strings to arena
    llm->params.provider = params->provider ? arena_strdup(arena, params->provider) : NULL;
    llm->params.compatible = params->compatible ? arena_strdup(arena, params->compatible) : NULL;
    llm->params.model = arena_strdup(arena, params->model);
    llm->params.api_key = arena_strdup(arena, params->api_key);
    llm->params.api_base = params->api_base ? arena_strdup(arena, params->api_base) : NULL;
    llm->params.instructions = params->instructions ? arena_strdup(arena, params->instructions) : NULL;
    
    if (!llm->params.model || !llm->params.api_key) {
        AC_LOG_ERROR("Failed to copy strings to arena");
        return NULL;
    }
    
    // Find provider based on params
    llm->provider = ac_llm_find_provider(&llm->params);
    if (!llm->provider) {
        AC_LOG_ERROR("No provider found");
        return NULL;
    }
    
    // Create provider private data
    llm->priv = NULL;
    if (llm->provider->create) {
        llm->priv = llm->provider->create(&llm->params);
        if (!llm->priv) {
            AC_LOG_ERROR("Provider %s failed to create private data", llm->provider->name);
            return NULL;
        }
    }
    
    AC_LOG_DEBUG("LLM created: model=%s, provider=%s", llm->params.model, llm->provider->name);
    
    return llm;
}

void ac_llm_cleanup(ac_llm_t* llm) {
    if (!llm) {
        return;
    }
    
    // Cleanup provider private data
    if (llm->provider && llm->provider->cleanup && llm->priv) {
        llm->provider->cleanup(llm->priv);
        llm->priv = NULL;
    }
}

char* ac_llm_chat(ac_llm_t* llm, const ac_message_t* messages) {
    if (!llm || !llm->arena || !llm->provider) {
        AC_LOG_ERROR("Invalid LLM state");
        return NULL;
    }
    
    if (!messages) {
        AC_LOG_ERROR("Messages is NULL");
        return NULL;
    }
    
    // Allocate response buffer from arena
    // TODO: Make this dynamic based on actual response size
    char* response = (char*)arena_alloc(llm->arena, 4096);
    if (!response) {
        AC_LOG_ERROR("Failed to allocate response buffer from arena");
        return NULL;
    }
    
    // Call provider
    if (!llm->provider->chat) {
        AC_LOG_ERROR("Provider %s does not implement chat", llm->provider->name);
        return NULL;
    }
    
    agentc_err_t err = llm->provider->chat(
        llm->priv,
        &llm->params,
        messages,
        response,
        4096
    );
    
    if (err != AGENTC_OK) {
        AC_LOG_ERROR("Provider chat failed: %d", err);
        return NULL;
    }
    
    AC_LOG_DEBUG("LLM chat completed");
    
    return response;
}
