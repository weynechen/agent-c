/**
 * @file llm_internal.h
 * @brief Internal LLM structure definition
 */

#ifndef AGENTC_LLM_INTERNAL_H
#define AGENTC_LLM_INTERNAL_H

#include "agentc/llm.h"
#include "llm_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Internal Structure Definition
 *============================================================================*/

/**
 * @brief Internal LLM client structure
 */
struct ac_llm {
    ac_llm_params_t params;
    const ac_llm_ops_t* provider;
    void* priv;              /* Provider private data (malloc'd) */
    arena_t* arena;
};

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_INTERNAL_H */
