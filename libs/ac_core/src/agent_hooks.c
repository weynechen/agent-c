/**
 * @file agent_hooks.c
 * @brief Agent hooks implementation
 */

#include "arc/agent_hooks.h"
#include <string.h>

/*============================================================================
 * Static State
 *============================================================================*/

static ac_agent_hooks_t s_hooks = {0};
static int s_hooks_set = 0;

/*============================================================================
 * Public API
 *============================================================================*/

void ac_agent_set_hooks(const ac_agent_hooks_t *hooks) {
    if (hooks) {
        memcpy(&s_hooks, hooks, sizeof(ac_agent_hooks_t));
        s_hooks_set = 1;
    } else {
        memset(&s_hooks, 0, sizeof(ac_agent_hooks_t));
        s_hooks_set = 0;
    }
}

const ac_agent_hooks_t *ac_agent_get_hooks(void) {
    return s_hooks_set ? &s_hooks : NULL;
}

/*============================================================================
 * Internal Hook Invocation (used by agent.c)
 *============================================================================*/

void ac_hook_call_run_start(const ac_hook_run_start_t *info) {
    if (s_hooks_set && s_hooks.on_run_start) {
        s_hooks.on_run_start(s_hooks.ctx, info);
    }
}

void ac_hook_call_run_end(const ac_hook_run_end_t *info) {
    if (s_hooks_set && s_hooks.on_run_end) {
        s_hooks.on_run_end(s_hooks.ctx, info);
    }
}

void ac_hook_call_iter_start(const ac_hook_iter_t *info) {
    if (s_hooks_set && s_hooks.on_iter_start) {
        s_hooks.on_iter_start(s_hooks.ctx, info);
    }
}

void ac_hook_call_iter_end(const ac_hook_iter_t *info) {
    if (s_hooks_set && s_hooks.on_iter_end) {
        s_hooks.on_iter_end(s_hooks.ctx, info);
    }
}

void ac_hook_call_llm_request(const ac_hook_llm_request_t *info) {
    if (s_hooks_set && s_hooks.on_llm_request) {
        s_hooks.on_llm_request(s_hooks.ctx, info);
    }
}

void ac_hook_call_llm_response(const ac_hook_llm_response_t *info) {
    if (s_hooks_set && s_hooks.on_llm_response) {
        s_hooks.on_llm_response(s_hooks.ctx, info);
    }
}

void ac_hook_call_tool_start(const ac_hook_tool_start_t *info) {
    if (s_hooks_set && s_hooks.on_tool_start) {
        s_hooks.on_tool_start(s_hooks.ctx, info);
    }
}

void ac_hook_call_tool_end(const ac_hook_tool_end_t *info) {
    if (s_hooks_set && s_hooks.on_tool_end) {
        s_hooks.on_tool_end(s_hooks.ctx, info);
    }
}
