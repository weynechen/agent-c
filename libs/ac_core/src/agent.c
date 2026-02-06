/**
 * @file agent.c
 * @brief Agent implementation with arena memory management
 */

#include "arc/agent.h"
#include "arc/arena.h"
#include "arc/llm.h"
#include "arc/tool.h"
#include "arc/message.h"
#include "arc/log.h"
#include "arc/platform.h"
#include "agent_hooks_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Use platform-specific default from platform.h */
#define DEFAULT_ARENA_SIZE ARC_AGENT_ARENA_SIZE

/*============================================================================
 * Forward Declarations
 *============================================================================*/

/* Session internal API */
arc_err_t ac_session_add_agent(struct ac_session *session, ac_agent_t *agent);

/*============================================================================
 * Agent Private Data
 *============================================================================*/

typedef struct {
    arena_t *arena;
    ac_llm_t *llm;
    ac_tool_registry_t *tools;
    struct ac_session *session;

    /* Message history (stored in arena) */
    ac_message_t *messages;
    ac_message_t *messages_tail;  /* Tail pointer for O(1) append */
    size_t message_count;

    const char *name;
    const char *instructions;
    int max_iterations;

    /* Cached tools schema (built once at creation) */
    char *cached_tools_schema;

    /* Streaming callbacks */
    ac_stream_callback_t stream_callback;
    void *callback_user_data;

    /* Statistics for hooks */
    uint64_t run_start_time_ms;
    int total_prompt_tokens;
    int total_completion_tokens;
} agent_priv_t;

/*============================================================================
 * Agent Structure
 *============================================================================*/

struct ac_agent {
    agent_priv_t *priv;
};

/*============================================================================
 * Message Append Helper (O(1) with tail pointer)
 *============================================================================*/

static void agent_append_message(agent_priv_t *priv, ac_message_t *message) {
    if (!priv || !message) {
        return;
    }

    message->next = NULL;

    if (!priv->messages) {
        priv->messages = message;
        priv->messages_tail = message;
    } else {
        priv->messages_tail->next = message;
        priv->messages_tail = message;
    }
    priv->message_count++;
}

/*============================================================================
 * Tool Schema Builder
 *============================================================================*/

static char *build_tools_schema(agent_priv_t *priv) {
    if (!priv->tools) {
        return NULL;
    }
    return ac_tool_registry_schema(priv->tools);
}

/*============================================================================
 * Tool Execution
 *============================================================================*/

static char *execute_tool_call(agent_priv_t *priv, const ac_tool_call_t *call) {
    if (!call || !call->name) {
        return ARC_STRDUP("{\"error\":\"Invalid tool call\"}");
    }

    if (!priv->tools) {
        AC_LOG_WARN("No tool registry configured");
        return ARC_STRDUP("{\"error\":\"No tools available\"}");
    }

    ac_tool_ctx_t ctx = {
        .session_id = NULL,
        .working_dir = NULL,
        .user_data = NULL
    };

    AC_LOG_INFO("Executing tool: %s(%s)", call->name,
                call->arguments ? call->arguments : "{}");

    /* Hook: tool start */
    uint64_t tool_start_ms = ac_platform_timestamp_ms();
    {
        ac_hook_tool_start_t hook_info = {
            .agent_name = priv->name,
            .id = call->id,
            .name = call->name,
            .arguments = call->arguments
        };
        AC_HOOK_CALL(ac_hook_call_tool_start, &hook_info);
    }

    /* Execute */
    char *result = ac_tool_registry_call(
        priv->tools,
        call->name,
        call->arguments ? call->arguments : "{}",
        &ctx
    );

    AC_LOG_DEBUG("Tool %s returned: %s", call->name, result ? result : "NULL");

    /* Hook: tool end */
    uint64_t tool_end_ms = ac_platform_timestamp_ms();
    {
        ac_hook_tool_end_t hook_info = {
            .agent_name = priv->name,
            .id = call->id,
            .name = call->name,
            .result = result,
            .duration_ms = tool_end_ms - tool_start_ms,
            .success = (result != NULL && strstr(result, "\"error\"") == NULL) ? 1 : 0
        };
        AC_HOOK_CALL(ac_hook_call_tool_end, &hook_info);
    }

    return result ? result : ARC_STRDUP("{\"error\":\"Tool returned NULL\"}");
}

/*============================================================================
 * Copy Tool Calls to Arena
 *============================================================================*/

static ac_tool_call_t *copy_tool_calls_to_arena(arena_t *arena, ac_tool_call_t *calls) {
    if (!arena || !calls) {
        return NULL;
    }

    ac_tool_call_t *first = NULL;
    ac_tool_call_t *last = NULL;

    for (ac_tool_call_t *call = calls; call; call = call->next) {
        ac_tool_call_t *copy = ac_tool_call_create(
            arena, call->id, call->name, call->arguments
        );
        if (copy) {
            if (!first) first = copy;
            if (last) last->next = copy;
            last = copy;
        }
    }

    return first;
}

/*============================================================================
 * Agent Run Implementation
 *============================================================================*/

static ac_agent_result_t *agent_run_impl(agent_priv_t *priv, const char *message) {
    if (!priv || !priv->arena || !priv->llm) {
        return NULL;
    }

    /* Initialize run statistics */
    priv->run_start_time_ms = ac_platform_timestamp_ms();
    priv->total_prompt_tokens = 0;
    priv->total_completion_tokens = 0;

    size_t tool_count = priv->tools ? ac_tool_registry_count(priv->tools) : 0;

    /* Hook: run start */
    {
        ac_hook_run_start_t hook_info = {
            .agent_name = priv->name,
            .message = message,
            .instructions = priv->instructions,
            .max_iterations = priv->max_iterations,
            .tool_count = tool_count
        };
        AC_HOOK_CALL(ac_hook_call_run_start, &hook_info);
    }

    /* Add system message if this is the first message */
    if (!priv->messages && priv->instructions) {
        ac_message_t *sys_msg = ac_message_create(
            priv->arena, AC_ROLE_SYSTEM, priv->instructions
        );
        if (sys_msg) {
            agent_append_message(priv, sys_msg);
        }
    }

    /* Add user message to history */
    ac_message_t *user_msg = ac_message_create(priv->arena, AC_ROLE_USER, message);
    if (!user_msg) {
        AC_LOG_ERROR("Failed to create user message");
        return NULL;
    }
    agent_append_message(priv, user_msg);

    AC_LOG_DEBUG("Added user message, total messages: %zu", priv->message_count);

    /* Use cached tools schema */
    char *tools_schema = priv->cached_tools_schema;

    /* ReACT loop */
    char *final_content = NULL;
    int iteration = 0;

    while (iteration < priv->max_iterations) {
        iteration++;
        AC_LOG_DEBUG("ReACT iteration %d/%d", iteration, priv->max_iterations);

        /* Hook: iteration start */
        {
            ac_hook_iter_t hook_info = {
                .agent_name = priv->name,
                .iteration = iteration,
                .max_iterations = priv->max_iterations
            };
            AC_HOOK_CALL(ac_hook_call_iter_start, &hook_info);
        }

        uint64_t llm_start_ms = ac_platform_timestamp_ms();

        /* Hook: LLM request - pass raw pointers, no JSON serialization here */
        {
            ac_hook_llm_request_t hook_info = {
                .agent_name = priv->name,
                .model = NULL,
                .messages = priv->messages,
                .tools_schema = tools_schema,
                .message_count = priv->message_count
            };
            AC_HOOK_CALL(ac_hook_call_llm_request, &hook_info);
        }

        /* Call LLM */
        ac_chat_response_t response;
        ac_chat_response_init(&response);

        arc_err_t err = ac_llm_chat_with_tools(
            priv->llm,
            priv->messages,
            tools_schema,
            &response
        );

        uint64_t llm_end_ms = ac_platform_timestamp_ms();

        /* Hook: LLM response - pass raw pointer, no JSON serialization here */
        {
            ac_hook_llm_response_t hook_info = {
                .agent_name = priv->name,
                .content = response.content,
                .tool_calls = response.tool_calls,
                .tool_call_count = response.tool_call_count,
                .prompt_tokens = response.prompt_tokens,
                .completion_tokens = response.completion_tokens,
                .total_tokens = response.total_tokens,
                .finish_reason = response.finish_reason,
                .duration_ms = llm_end_ms - llm_start_ms
            };
            AC_HOOK_CALL(ac_hook_call_llm_response, &hook_info);
        }

        /* Accumulate token usage */
        priv->total_prompt_tokens += response.prompt_tokens;
        priv->total_completion_tokens += response.completion_tokens;

        if (err != ARC_OK) {
            AC_LOG_ERROR("LLM chat failed: %d", err);
            ac_chat_response_free(&response);
            return NULL;
        }

        /* Check if there are tool calls */
        if (ac_chat_response_has_tool_calls(&response)) {
            AC_LOG_INFO("LLM requested %d tool call(s)", response.tool_call_count);

            /* Copy tool calls to arena and add assistant message */
            ac_tool_call_t *arena_calls = copy_tool_calls_to_arena(
                priv->arena, response.tool_calls
            );

            ac_message_t *asst_msg = ac_message_create_with_tool_calls(
                priv->arena, response.content, arena_calls
            );

            if (asst_msg) {
                agent_append_message(priv, asst_msg);
            }

            /* Execute each tool call and add results */
            for (ac_tool_call_t *call = response.tool_calls; call; call = call->next) {
                char *result = execute_tool_call(priv, call);

                ac_message_t *tool_msg = ac_message_create_tool_result(
                    priv->arena,
                    call->id,
                    result ? result : "{\"error\":\"Tool execution failed\"}"
                );

                if (tool_msg) {
                    agent_append_message(priv, tool_msg);
                }

                if (result) ARC_FREE(result);
            }

            /* Hook: iteration end */
            {
                ac_hook_iter_t hook_info = {
                    .agent_name = priv->name,
                    .iteration = iteration,
                    .max_iterations = priv->max_iterations
                };
                AC_HOOK_CALL(ac_hook_call_iter_end, &hook_info);
            }

            ac_chat_response_free(&response);
            continue;
        }

        /* No tool calls - we have the final response */
        if (response.content) {
            final_content = arena_strdup(priv->arena, response.content);

            ac_message_t *asst_msg = ac_message_create(
                priv->arena, AC_ROLE_ASSISTANT, response.content
            );
            if (asst_msg) {
                agent_append_message(priv, asst_msg);
            }
        }

        /* Hook: iteration end */
        {
            ac_hook_iter_t hook_info = {
                .agent_name = priv->name,
                .iteration = iteration,
                .max_iterations = priv->max_iterations
            };
            AC_HOOK_CALL(ac_hook_call_iter_end, &hook_info);
        }

        ac_chat_response_free(&response);
        break;
    }

    if (iteration >= priv->max_iterations && !final_content) {
        AC_LOG_WARN("ReACT loop reached max iterations (%d)", priv->max_iterations);
    }

    /* Hook: run end */
    uint64_t run_end_ms = ac_platform_timestamp_ms();
    {
        ac_hook_run_end_t hook_info = {
            .agent_name = priv->name,
            .content = final_content,
            .iterations = iteration,
            .total_prompt_tokens = priv->total_prompt_tokens,
            .total_completion_tokens = priv->total_completion_tokens,
            .duration_ms = run_end_ms - priv->run_start_time_ms
        };
        AC_HOOK_CALL(ac_hook_call_run_end, &hook_info);
    }

    /* Allocate result from agent's arena */
    ac_agent_result_t *result = (ac_agent_result_t *)arena_alloc(
        priv->arena, sizeof(ac_agent_result_t)
    );

    if (!result) {
        AC_LOG_ERROR("Failed to allocate result from arena");
        return NULL;
    }

    result->content = final_content;

    AC_LOG_DEBUG("Agent run completed after %d iterations, total messages: %zu",
                 iteration, priv->message_count);
    return result;
}

/*============================================================================
 * Agent Run Implementation (Streaming Mode)
 *============================================================================*/

/**
 * @brief Check if response has tool use blocks
 */
static int response_has_tool_use(const ac_chat_response_t* response) {
    if (!response) return 0;
    for (ac_content_block_t* b = response->blocks; b; b = b->next) {
        if (b->type == AC_BLOCK_TOOL_USE) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Execute tool from content block and return result
 */
static char *execute_tool_block(agent_priv_t *priv, const ac_content_block_t *block) {
    if (!block || !block->name) {
        return ARC_STRDUP("{\"error\":\"Invalid tool block\"}");
    }

    if (!priv->tools) {
        AC_LOG_WARN("No tool registry configured");
        return ARC_STRDUP("{\"error\":\"No tools available\"}");
    }

    ac_tool_ctx_t ctx = {
        .session_id = NULL,
        .working_dir = NULL,
        .user_data = NULL
    };

    AC_LOG_INFO("Executing tool: %s(%s)", block->name,
                block->input ? block->input : "{}");

    /* Hook: tool start */
    uint64_t tool_start_ms = ac_platform_timestamp_ms();
    {
        ac_hook_tool_start_t hook_info = {
            .agent_name = priv->name,
            .id = block->id,
            .name = block->name,
            .arguments = block->input
        };
        AC_HOOK_CALL(ac_hook_call_tool_start, &hook_info);
    }

    /* Execute */
    char *result = ac_tool_registry_call(
        priv->tools,
        block->name,
        block->input ? block->input : "{}",
        &ctx
    );

    AC_LOG_DEBUG("Tool %s returned: %s", block->name, result ? result : "NULL");

    /* Hook: tool end */
    uint64_t tool_end_ms = ac_platform_timestamp_ms();
    {
        ac_hook_tool_end_t hook_info = {
            .agent_name = priv->name,
            .id = block->id,
            .name = block->name,
            .result = result,
            .duration_ms = tool_end_ms - tool_start_ms,
            .success = (result != NULL && strstr(result, "\"error\"") == NULL) ? 1 : 0
        };
        AC_HOOK_CALL(ac_hook_call_tool_end, &hook_info);
    }

    return result ? result : ARC_STRDUP("{\"error\":\"Tool returned NULL\"}");
}

/**
 * @brief Create tool result message from response blocks
 */
static ac_message_t* create_tool_results_message(agent_priv_t *priv, const ac_chat_response_t* response) {
    if (!response || !response->blocks) return NULL;

    /* Create tool result message (user role for Anthropic API) */
    ac_message_t* result_msg = (ac_message_t*)arena_alloc(priv->arena, sizeof(ac_message_t));
    if (!result_msg) return NULL;
    memset(result_msg, 0, sizeof(ac_message_t));
    result_msg->role = AC_ROLE_USER;

    ac_content_block_t* last_block = NULL;

    for (ac_content_block_t* b = response->blocks; b; b = b->next) {
        if (b->type != AC_BLOCK_TOOL_USE) continue;
        if (!b->id || !b->name) continue;

        /* Execute the tool */
        char* tool_result = execute_tool_block(priv, b);
        int is_error = (tool_result && strstr(tool_result, "\"error\"") != NULL);

        /* Create tool_result content block */
        ac_content_block_t* result_block = (ac_content_block_t*)arena_alloc(priv->arena, sizeof(ac_content_block_t));
        if (!result_block) {
            if (tool_result) ARC_FREE(tool_result);
            continue;
        }
        memset(result_block, 0, sizeof(ac_content_block_t));
        result_block->type = AC_BLOCK_TOOL_RESULT;
        result_block->id = arena_strdup(priv->arena, b->id);
        result_block->text = arena_strdup(priv->arena, tool_result ? tool_result : "{}");
        result_block->is_error = is_error;

        if (tool_result) ARC_FREE(tool_result);

        /* Append to result message */
        if (!result_msg->blocks) {
            result_msg->blocks = result_block;
        } else {
            last_block->next = result_block;
        }
        last_block = result_block;
    }

    return result_msg->blocks ? result_msg : NULL;
}

static ac_agent_result_t *agent_run_stream_impl(agent_priv_t *priv, const char *message) {
    if (!priv || !priv->arena || !priv->llm) {
        return NULL;
    }

    /* Initialize run statistics */
    priv->run_start_time_ms = ac_platform_timestamp_ms();
    priv->total_prompt_tokens = 0;
    priv->total_completion_tokens = 0;

    size_t tool_count = priv->tools ? ac_tool_registry_count(priv->tools) : 0;

    /* Hook: run start */
    {
        ac_hook_run_start_t hook_info = {
            .agent_name = priv->name,
            .message = message,
            .instructions = priv->instructions,
            .max_iterations = priv->max_iterations,
            .tool_count = tool_count
        };
        AC_HOOK_CALL(ac_hook_call_run_start, &hook_info);
    }

    /* Add system message if this is the first message */
    if (!priv->messages && priv->instructions) {
        ac_message_t *sys_msg = ac_message_create(
            priv->arena, AC_ROLE_SYSTEM, priv->instructions
        );
        if (sys_msg) {
            agent_append_message(priv, sys_msg);
        }
    }

    /* Add user message to history */
    ac_message_t *user_msg = ac_message_create(priv->arena, AC_ROLE_USER, message);
    if (!user_msg) {
        AC_LOG_ERROR("Failed to create user message");
        return NULL;
    }
    agent_append_message(priv, user_msg);

    AC_LOG_DEBUG("Added user message, total messages: %zu", priv->message_count);

    /* Use cached tools schema */
    char *tools_schema = priv->cached_tools_schema;

    /* ReACT loop with streaming */
    char *final_content = NULL;
    int iteration = 0;

    while (iteration < priv->max_iterations) {
        iteration++;
        AC_LOG_DEBUG("ReACT streaming iteration %d/%d", iteration, priv->max_iterations);

        /* Hook: iteration start */
        {
            ac_hook_iter_t hook_info = {
                .agent_name = priv->name,
                .iteration = iteration,
                .max_iterations = priv->max_iterations
            };
            AC_HOOK_CALL(ac_hook_call_iter_start, &hook_info);
        }

        uint64_t llm_start_ms = ac_platform_timestamp_ms();

        /* Hook: LLM request */
        {
            ac_hook_llm_request_t hook_info = {
                .agent_name = priv->name,
                .model = NULL,
                .messages = priv->messages,
                .tools_schema = tools_schema,
                .message_count = priv->message_count
            };
            AC_HOOK_CALL(ac_hook_call_llm_request, &hook_info);
        }

        /* Call LLM with streaming */
        ac_chat_response_t response = {0};
        arc_err_t err = ac_llm_chat_stream(
            priv->llm,
            priv->messages,
            tools_schema,
            priv->stream_callback,
            priv->callback_user_data,
            &response
        );

        uint64_t llm_end_ms = ac_platform_timestamp_ms();

        /* Hook: LLM response */
        {
            ac_hook_llm_response_t hook_info = {
                .agent_name = priv->name,
                .content = response.content,
                .tool_calls = NULL,
                .tool_call_count = 0,
                .prompt_tokens = response.input_tokens,
                .completion_tokens = response.output_tokens,
                .total_tokens = response.input_tokens + response.output_tokens,
                .finish_reason = response.stop_reason,
                .duration_ms = llm_end_ms - llm_start_ms
            };
            AC_HOOK_CALL(ac_hook_call_llm_response, &hook_info);
        }

        /* Accumulate token usage */
        priv->total_prompt_tokens += response.input_tokens;
        priv->total_completion_tokens += response.output_tokens;

        if (err != ARC_OK) {
            AC_LOG_ERROR("LLM streaming chat failed: %d", err);
            ac_chat_response_free(&response);
            return NULL;
        }

        /* Check if there are tool use blocks */
        if (response_has_tool_use(&response)) {
            AC_LOG_INFO("LLM requested tool calls (streaming mode)");

            /* Add assistant response to history */
            ac_message_t *asst_msg = ac_message_from_response(priv->arena, &response);
            if (asst_msg) {
                agent_append_message(priv, asst_msg);
            }

            /* Execute tools and create result message */
            ac_message_t *tool_result_msg = create_tool_results_message(priv, &response);
            if (tool_result_msg) {
                agent_append_message(priv, tool_result_msg);
            }

            /* Hook: iteration end */
            {
                ac_hook_iter_t hook_info = {
                    .agent_name = priv->name,
                    .iteration = iteration,
                    .max_iterations = priv->max_iterations
                };
                AC_HOOK_CALL(ac_hook_call_iter_end, &hook_info);
            }

            ac_chat_response_free(&response);
            continue;
        }

        /* No tool calls - we have the final response */
        if (response.content) {
            final_content = arena_strdup(priv->arena, response.content);

            ac_message_t *asst_msg = ac_message_from_response(priv->arena, &response);
            if (asst_msg) {
                agent_append_message(priv, asst_msg);
            }
        }

        /* Hook: iteration end */
        {
            ac_hook_iter_t hook_info = {
                .agent_name = priv->name,
                .iteration = iteration,
                .max_iterations = priv->max_iterations
            };
            AC_HOOK_CALL(ac_hook_call_iter_end, &hook_info);
        }

        ac_chat_response_free(&response);
        break;
    }

    if (iteration >= priv->max_iterations && !final_content) {
        AC_LOG_WARN("ReACT loop reached max iterations (%d)", priv->max_iterations);
    }

    /* Hook: run end */
    uint64_t run_end_ms = ac_platform_timestamp_ms();
    {
        ac_hook_run_end_t hook_info = {
            .agent_name = priv->name,
            .content = final_content,
            .iterations = iteration,
            .total_prompt_tokens = priv->total_prompt_tokens,
            .total_completion_tokens = priv->total_completion_tokens,
            .duration_ms = run_end_ms - priv->run_start_time_ms
        };
        AC_HOOK_CALL(ac_hook_call_run_end, &hook_info);
    }

    /* Allocate result from agent's arena */
    ac_agent_result_t *result = (ac_agent_result_t *)arena_alloc(
        priv->arena, sizeof(ac_agent_result_t)
    );

    if (!result) {
        AC_LOG_ERROR("Failed to allocate result from arena");
        return NULL;
    }

    result->content = final_content;

    AC_LOG_DEBUG("Agent streaming run completed after %d iterations, total messages: %zu",
                 iteration, priv->message_count);
    return result;
}

/*============================================================================
 * Public API
 *============================================================================*/

ac_agent_t *ac_agent_create(ac_session_t *session, const ac_agent_params_t *params) {
    if (!session || !params) {
        AC_LOG_ERROR("Invalid arguments to ac_agent_create");
        return NULL;
    }

    ac_agent_t *agent = (ac_agent_t *)ARC_CALLOC(1, sizeof(ac_agent_t));
    if (!agent) {
        AC_LOG_ERROR("Failed to allocate agent");
        return NULL;
    }

    agent_priv_t *priv = (agent_priv_t *)ARC_CALLOC(1, sizeof(agent_priv_t));
    if (!priv) {
        AC_LOG_ERROR("Failed to allocate agent private data");
        ARC_FREE(agent);
        return NULL;
    }

    priv->arena = arena_create(DEFAULT_ARENA_SIZE);
    if (!priv->arena) {
        AC_LOG_ERROR("Failed to create arena");
        ARC_FREE(priv);
        ARC_FREE(agent);
        return NULL;
    }

    priv->session = session;
    priv->messages = NULL;
    priv->messages_tail = NULL;
    priv->message_count = 0;
    priv->cached_tools_schema = NULL;

    if (params->name) {
        priv->name = arena_strdup(priv->arena, params->name);
    }

    if (params->instructions) {
        priv->instructions = arena_strdup(priv->arena, params->instructions);
    }

    priv->max_iterations = params->max_iterations > 0 ?
        params->max_iterations : AC_AGENT_DEFAULT_MAX_ITERATIONS;

    priv->llm = ac_llm_create(priv->arena, &params->llm);
    if (!priv->llm) {
        AC_LOG_ERROR("Failed to create LLM");
        arena_destroy(priv->arena);
        ARC_FREE(priv);
        ARC_FREE(agent);
        return NULL;
    }

    priv->tools = params->tools;

    if (priv->tools) {
        size_t tool_count = ac_tool_registry_count(priv->tools);
        AC_LOG_DEBUG("Agent configured with %zu tools", tool_count);

        /* Build and cache tools schema once at creation */
        priv->cached_tools_schema = build_tools_schema(priv);
        if (priv->cached_tools_schema) {
            AC_LOG_DEBUG("Cached tools schema (%zu bytes)",
                         strlen(priv->cached_tools_schema));
        }
    }

    /* Store streaming callbacks */
    priv->stream_callback = params->callbacks.on_stream;
    priv->callback_user_data = params->callbacks.user_data;

    agent->priv = priv;

    if (ac_session_add_agent(session, agent) != ARC_OK) {
        AC_LOG_ERROR("Failed to add agent to session");
        arena_destroy(priv->arena);
        ARC_FREE(priv);
        ARC_FREE(agent);
        return NULL;
    }

    AC_LOG_INFO("Agent created: %s (arena=%zuKB, max_iter=%d, stream=%s)",
                priv->name ? priv->name : "unnamed",
                DEFAULT_ARENA_SIZE / 1024,
                priv->max_iterations,
                priv->stream_callback ? "yes" : "no");

    return agent;
}

ac_agent_result_t *ac_agent_run(ac_agent_t *agent, const char *message) {
    if (!agent || !agent->priv || !message) {
        AC_LOG_ERROR("Invalid arguments to ac_agent_run");
        return NULL;
    }

    /* Use streaming mode if callback is configured */
    if (agent->priv->stream_callback) {
        return agent_run_stream_impl(agent->priv, message);
    }

    return agent_run_impl(agent->priv, message);
}

void ac_agent_destroy(ac_agent_t *agent) {
    if (!agent) {
        return;
    }

    agent_priv_t *priv = agent->priv;
    if (priv) {
        if (priv->llm) {
            ac_llm_cleanup(priv->llm);
        }

        /* Free cached tools schema */
        if (priv->cached_tools_schema) {
            ARC_FREE(priv->cached_tools_schema);
            priv->cached_tools_schema = NULL;
        }

        if (priv->arena) {
            AC_LOG_DEBUG("Destroying agent arena");
            arena_destroy(priv->arena);
        }
        ARC_FREE(priv);
    }

    ARC_FREE(agent);
}
