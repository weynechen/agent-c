/**
 * @file trace.c
 * @brief ArC Trace implementation
 *
 * Implements tracing by registering as an agent hook observer.
 * The trace module is completely decoupled from agent.c.
 */

#include "arc/trace.h"
#include "arc/agent_hooks.h"
#include "arc/platform.h"
#include "llm/message/message_json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Event Type Names
 *============================================================================*/

static const char *s_event_names[] = {
    "agent_start",
    "agent_end",
    "iter_start",
    "iter_end",
    "llm_request",
    "llm_response",
    "tool_start",
    "tool_end"
};

/*============================================================================
 * Trace Context
 *============================================================================*/

typedef struct {
    ac_trace_handler_t handler;
    void *user_data;
    char trace_id[32];
    int sequence;
    int enabled;
} trace_ctx_t;

static trace_ctx_t s_ctx = {0};

/*============================================================================
 * Utility Functions
 *============================================================================*/

uint64_t ac_trace_timestamp_ms(void) {
    return ac_platform_timestamp_ms();
}

char *ac_trace_generate_id(char *buffer, size_t size) {
    if (!buffer || size < 32) {
        return NULL;
    }

    uint64_t ts = ac_trace_timestamp_ms();
    unsigned int rand_val = (unsigned int)rand();

    snprintf(buffer, size, "tr_%llx_%08x",
             (unsigned long long)ts, rand_val);

    return buffer;
}

const char *ac_trace_event_name(ac_trace_event_type_t type) {
    if (type >= 0 && type < (int)(sizeof(s_event_names) / sizeof(s_event_names[0]))) {
        return s_event_names[type];
    }
    return "unknown";
}

int ac_trace_is_enabled(void) {
    return s_ctx.enabled && s_ctx.handler != NULL;
}

/*============================================================================
 * Internal: Emit trace event
 *============================================================================*/

static void emit_event(ac_trace_event_type_t type, const char *agent_name, ac_trace_event_t *event) {
    if (!s_ctx.enabled || !s_ctx.handler) {
        return;
    }

    event->type = type;
    event->timestamp_ms = ac_trace_timestamp_ms();
    event->trace_id = s_ctx.trace_id;
    event->agent_name = agent_name;
    event->sequence = ++s_ctx.sequence;

    s_ctx.handler(event, s_ctx.user_data);
}

/*============================================================================
 * Hook Callbacks
 *============================================================================*/

static void on_run_start(void *ctx, const ac_hook_run_start_t *info) {
    (void)ctx;

    /* Initialize new trace */
    ac_trace_generate_id(s_ctx.trace_id, sizeof(s_ctx.trace_id));
    s_ctx.sequence = 0;

    ac_trace_event_t event = {0};
    event.data.agent_start.message = info->message;
    event.data.agent_start.instructions = info->instructions;
    event.data.agent_start.max_iterations = info->max_iterations;
    event.data.agent_start.tool_count = info->tool_count;

    emit_event(AC_TRACE_AGENT_START, info->agent_name, &event);
}

static void on_run_end(void *ctx, const ac_hook_run_end_t *info) {
    (void)ctx;

    ac_trace_event_t event = {0};
    event.data.agent_end.content = info->content;
    event.data.agent_end.iterations = info->iterations;
    event.data.agent_end.total_prompt_tokens = info->total_prompt_tokens;
    event.data.agent_end.total_completion_tokens = info->total_completion_tokens;
    event.data.agent_end.duration_ms = info->duration_ms;

    emit_event(AC_TRACE_AGENT_END, info->agent_name, &event);
}

static void on_iter_start(void *ctx, const ac_hook_iter_t *info) {
    (void)ctx;

    ac_trace_event_t event = {0};
    event.data.iter.iteration = info->iteration;
    event.data.iter.max_iterations = info->max_iterations;

    emit_event(AC_TRACE_ITER_START, info->agent_name, &event);
}

static void on_iter_end(void *ctx, const ac_hook_iter_t *info) {
    (void)ctx;

    ac_trace_event_t event = {0};
    event.data.iter.iteration = info->iteration;
    event.data.iter.max_iterations = info->max_iterations;

    emit_event(AC_TRACE_ITER_END, info->agent_name, &event);
}

static void on_llm_request(void *ctx, const ac_hook_llm_request_t *info) {
    (void)ctx;

    /* Serialize messages on demand - only when trace is active */
    char *messages_json = ac_messages_to_json_string(info->messages);

    ac_trace_event_t event = {0};
    event.data.llm_request.model = info->model;
    event.data.llm_request.messages_json = messages_json;
    event.data.llm_request.tools_json = info->tools_schema;
    event.data.llm_request.message_count = info->message_count;

    emit_event(AC_TRACE_LLM_REQUEST, info->agent_name, &event);

    /* Cleanup */
    if (messages_json) ARC_FREE(messages_json);
}

static void on_llm_response(void *ctx, const ac_hook_llm_response_t *info) {
    (void)ctx;

    /* Serialize tool calls on demand - only when trace is active */
    char *tool_calls_json = ac_tool_calls_to_json_string(info->tool_calls);

    ac_trace_event_t event = {0};
    event.data.llm_response.content = info->content;
    event.data.llm_response.tool_calls_json = tool_calls_json;
    event.data.llm_response.tool_call_count = info->tool_call_count;
    event.data.llm_response.prompt_tokens = info->prompt_tokens;
    event.data.llm_response.completion_tokens = info->completion_tokens;
    event.data.llm_response.total_tokens = info->total_tokens;
    event.data.llm_response.finish_reason = info->finish_reason;
    event.data.llm_response.duration_ms = info->duration_ms;

    emit_event(AC_TRACE_LLM_RESPONSE, info->agent_name, &event);

    /* Cleanup */
    if (tool_calls_json) ARC_FREE(tool_calls_json);
}

static void on_tool_start(void *ctx, const ac_hook_tool_start_t *info) {
    (void)ctx;

    ac_trace_event_t event = {0};
    event.data.tool_start.id = info->id;
    event.data.tool_start.name = info->name;
    event.data.tool_start.arguments = info->arguments;

    emit_event(AC_TRACE_TOOL_START, info->agent_name, &event);
}

static void on_tool_end(void *ctx, const ac_hook_tool_end_t *info) {
    (void)ctx;

    ac_trace_event_t event = {0};
    event.data.tool_end.id = info->id;
    event.data.tool_end.name = info->name;
    event.data.tool_end.result = info->result;
    event.data.tool_end.duration_ms = info->duration_ms;
    event.data.tool_end.success = info->success;

    emit_event(AC_TRACE_TOOL_END, info->agent_name, &event);
}

/*============================================================================
 * Public API
 *============================================================================*/

void ac_trace_enable(ac_trace_handler_t handler, void *user_data) {
    if (!handler) {
        return;
    }

    /* Store handler */
    s_ctx.handler = handler;
    s_ctx.user_data = user_data;
    s_ctx.enabled = 1;
    s_ctx.sequence = 0;
    memset(s_ctx.trace_id, 0, sizeof(s_ctx.trace_id));

    /* Register agent hooks */
    static ac_agent_hooks_t trace_hooks = {
        .ctx = NULL,
        .on_run_start = on_run_start,
        .on_run_end = on_run_end,
        .on_iter_start = on_iter_start,
        .on_iter_end = on_iter_end,
        .on_llm_request = on_llm_request,
        .on_llm_response = on_llm_response,
        .on_tool_start = on_tool_start,
        .on_tool_end = on_tool_end
    };

    ac_agent_set_hooks(&trace_hooks);
}

void ac_trace_disable(void) {
    s_ctx.enabled = 0;
    s_ctx.handler = NULL;
    s_ctx.user_data = NULL;

    /* Unregister hooks */
    ac_agent_set_hooks(NULL);
}
