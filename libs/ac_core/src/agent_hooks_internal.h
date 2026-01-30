/**
 * @file agent_hooks_internal.h
 * @brief Internal hook invocation functions and macros
 *
 * This header provides:
 * - Hook invocation functions (used by agent.c)
 * - AC_HOOK_CALL macro for compile-time and runtime control
 *
 * Compile-time control:
 * - Define AC_DISABLE_HOOKS to completely disable hooks (zero overhead)
 */

#ifndef ARC_AGENT_HOOKS_INTERNAL_H
#define ARC_AGENT_HOOKS_INTERNAL_H

#include "arc/agent_hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Compile-time Hook Control
 *============================================================================*/

#ifdef AC_DISABLE_HOOKS

/* Completely disable hooks - zero overhead */
#define AC_HOOK_CALL(func, info_ptr) ((void)0)

#else

/**
 * @brief Call a hook function if hooks are registered
 *
 * This macro provides runtime check - only calls the hook if
 * ac_agent_get_hooks() returns non-NULL.
 *
 * @param func The hook call function (e.g., ac_hook_call_run_start)
 * @param info_ptr Pointer to the hook info structure
 */
#define AC_HOOK_CALL(func, info_ptr) \
    do { \
        if (ac_agent_get_hooks()) { \
            func(info_ptr); \
        } \
    } while(0)

#endif /* AC_DISABLE_HOOKS */

/*============================================================================
 * Internal Hook Invocation Functions
 *
 * These functions check if the specific hook callback is set and call it.
 * They are used by AC_HOOK_CALL macro.
 *============================================================================*/

#ifndef AC_DISABLE_HOOKS

void ac_hook_call_run_start(const ac_hook_run_start_t *info);
void ac_hook_call_run_end(const ac_hook_run_end_t *info);
void ac_hook_call_iter_start(const ac_hook_iter_t *info);
void ac_hook_call_iter_end(const ac_hook_iter_t *info);
void ac_hook_call_llm_request(const ac_hook_llm_request_t *info);
void ac_hook_call_llm_response(const ac_hook_llm_response_t *info);
void ac_hook_call_tool_start(const ac_hook_tool_start_t *info);
void ac_hook_call_tool_end(const ac_hook_tool_end_t *info);

#endif /* AC_DISABLE_HOOKS */

#ifdef __cplusplus
}
#endif

#endif /* ARC_AGENT_HOOKS_INTERNAL_H */
