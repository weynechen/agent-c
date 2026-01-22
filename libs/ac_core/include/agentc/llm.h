/**
 * @file llm.h
 * @brief AgentC LLM API - Internal Interface
 *
 * Simple LLM abstraction using arena allocation.
 * This is an internal interface used by agents.
 */

#ifndef AGENTC_LLM_H
#define AGENTC_LLM_H

#include "arena.h"
#include "error.h"
#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * LLM Handle (opaque)
 *============================================================================*/

typedef struct ac_llm ac_llm_t;

/*============================================================================
 * LLM Parameters
 *============================================================================*/

typedef struct {
    /* Provider selection */
    const char* provider;           /* Provider name: "openai", "anthropic", etc. (optional) */
    const char* compatible;         /* Compatibility mode: "openai" for OpenAI-compatible APIs (optional) */
    
    /* LLM configuration */
    const char* model;              /* Model name (required) */
    const char* api_key;            /* API key (required) */
    const char* api_base;           /* API base URL (optional) */
    const char* instructions;       /* System instructions (optional) */
} ac_llm_params_t;

/*============================================================================
 * LLM API
 *============================================================================*/

/**
 * @brief Create LLM with arena
 *
 * Creates an LLM client using the provided arena for all memory allocations.
 * All memory is freed when the arena is destroyed.
 *
 * @param arena   Arena for memory allocation
 * @param params  LLM parameters
 * @return LLM handle, NULL on error
 */
ac_llm_t* ac_llm_create(arena_t* arena, const ac_llm_params_t* params);

/**
 * @brief Chat with LLM
 *
 * Sends message history to the LLM and returns the response.
 * The response is allocated from the LLM's arena.
 *
 * @param llm       LLM handle
 * @param messages  Message history (linked list)
 * @return Response (allocated from arena), NULL on error
 */
char* ac_llm_chat(ac_llm_t* llm, const ac_message_t* messages);

/**
 * @brief Cleanup LLM resources
 *
 * Cleanup provider private data (HTTP client, etc).
 * The LLM structure itself is in arena and will be freed with arena_destroy.
 * This should be called before destroying the arena.
 *
 * @param llm  LLM handle
 */
void ac_llm_cleanup(ac_llm_t* llm);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_H */
