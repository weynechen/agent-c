/**
 * @file llm_internal.h
 * @brief Internal LLM structure definition
 */

#ifndef ARC_LLM_INTERNAL_H
#define ARC_LLM_INTERNAL_H

#include "arc/llm.h"
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

#endif /* ARC_LLM_INTERNAL_H */
