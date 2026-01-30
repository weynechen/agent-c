/**
 * @file llm.h
 * @brief ArC LLM API - Internal Interface
 *
 * Simple LLM abstraction using arena allocation.
 * This is an internal interface used by agents.
 */

#ifndef ARC_LLM_H
#define ARC_LLM_H

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

    /* Generation parameters */
    float temperature;              /* Sampling temperature (0.0-2.0, default: 0.7) */
    float top_p;                    /* Nucleus sampling (0.0-1.0) */
    int max_tokens;                 /* Max tokens to generate (0 = no limit) */
    int timeout_ms;                 /* Request timeout in ms (default: 60000) */
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
 * @brief Chat with LLM (simple, text-only)
 *
 * Sends message history to the LLM and returns the text response.
 * The response is allocated from the LLM's arena.
 *
 * @param llm       LLM handle
 * @param messages  Message history (linked list)
 * @return Response text (allocated from arena), NULL on error
 */
char* ac_llm_chat(ac_llm_t* llm, const ac_message_t* messages);

/**
 * @brief Chat with LLM with tool support
 *
 * Sends message history with optional tools to the LLM.
 * Returns structured response that may include tool calls.
 *
 * @param llm       LLM handle
 * @param messages  Message history (linked list)
 * @param tools     JSON array of tool definitions (NULL for no tools)
 * @param response  Output response structure (caller must call ac_chat_response_free)
 * @return ARC_OK on success
 */
arc_err_t ac_llm_chat_with_tools(
    ac_llm_t* llm,
    const ac_message_t* messages,
    const char* tools,
    ac_chat_response_t* response
);

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

#endif /* ARC_LLM_H */
