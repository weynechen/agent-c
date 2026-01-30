/**
 * @file trace.c
 * @brief AgentC Trace implementation
 * 
 * Platform-independent trace functionality.
 * Time functions are provided by the port layer (ac_platform_timestamp_ms).
 */

#include "agentc/trace.h"
#include "agentc/platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Static State
 *============================================================================*/

static ac_trace_handler_t s_trace_handler = NULL;
static void *s_trace_user_data = NULL;
static ac_trace_level_t s_trace_level = AC_TRACE_LEVEL_OFF;

/*============================================================================
 * Event Type Names
 *============================================================================*/

static const char *s_event_names[] = {
    "agent_start",
    "agent_end",
    "react_iter_start",
    "react_iter_end",
    "llm_request",
    "llm_response",
    "tool_call",
    "tool_result"
};

/*============================================================================
 * Trace API Implementation
 *============================================================================*/

void ac_trace_set_handler(ac_trace_handler_t handler, void *user_data) {
    s_trace_handler = handler;
    s_trace_user_data = user_data;
}

void ac_trace_set_level(ac_trace_level_t level) {
    s_trace_level = level;
}

ac_trace_level_t ac_trace_get_level(void) {
    return s_trace_level;
}

int ac_trace_is_enabled(void) {
    return s_trace_level > AC_TRACE_LEVEL_OFF && s_trace_handler != NULL;
}

const char *ac_trace_event_name(ac_trace_event_type_t type) {
    if (type >= 0 && type < (int)(sizeof(s_event_names) / sizeof(s_event_names[0]))) {
        return s_event_names[type];
    }
    return "unknown";
}

void ac_trace_emit(const ac_trace_event_t *event) {
    if (!ac_trace_is_enabled() || !event) {
        return;
    }
    
    /* Check trace level */
    switch (event->type) {
        case AC_TRACE_AGENT_START:
        case AC_TRACE_AGENT_END:
            /* Basic events always emitted if level >= BASIC */
            if (s_trace_level < AC_TRACE_LEVEL_BASIC) {
                return;
            }
            break;
        default:
            /* Detailed events only if level >= DETAILED */
            if (s_trace_level < AC_TRACE_LEVEL_DETAILED) {
                return;
            }
            break;
    }
    
    /* Call handler */
    s_trace_handler(event, s_trace_user_data);
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

uint64_t ac_trace_timestamp_ms(void) {
    /* Delegate to platform-specific implementation */
    return ac_platform_timestamp_ms();
}

char *ac_trace_generate_id(char *buffer, size_t size) {
    if (!buffer || size < 32) {
        return NULL;
    }
    
    /* Generate a simple unique ID based on timestamp and random value */
    uint64_t ts = ac_trace_timestamp_ms();
    unsigned int rand_val = (unsigned int)rand();
    
    snprintf(buffer, size, "tr_%llx_%08x", 
             (unsigned long long)ts, rand_val);
    
    return buffer;
}

/*============================================================================
 * Trace Context Implementation
 *============================================================================*/

void ac_trace_ctx_init(ac_trace_ctx_t *ctx, const char *agent_name) {
    if (!ctx) {
        return;
    }
    
    memset(ctx, 0, sizeof(ac_trace_ctx_t));
    ac_trace_generate_id(ctx->trace_id, sizeof(ctx->trace_id));
    ctx->agent_name = agent_name;
    ctx->sequence = 0;
    ctx->start_time_ms = ac_trace_timestamp_ms();
    ctx->total_prompt_tokens = 0;
    ctx->total_completion_tokens = 0;
}

int ac_trace_ctx_next_seq(ac_trace_ctx_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return ++ctx->sequence;
}
