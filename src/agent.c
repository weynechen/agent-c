/**
 * @file agent.c
 * @brief ReACT Agent implementation
 *
 * Implements the Reasoning + Acting loop for LLM agents.
 */

#include "agentc/agent.h"
#include "agentc/platform.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal Structures
 *============================================================================*/

struct agentc_agent {
    /* Configuration (copied) */
    agentc_llm_client_t *llm;
    agentc_tool_registry_t *tools;
    char *name;
    char *instructions;
    int max_iterations;
    int max_tokens;
    float temperature;
    char *tool_choice;
    int parallel_tool_calls;
    int stream;
    agentc_agent_hooks_t hooks;

    /* Runtime state */
    char *tools_json;             /* Cached tools JSON */
    int current_iteration;
};

/*============================================================================
 * Agent Create/Destroy
 *============================================================================*/

agentc_err_t agentc_agent_create(
    const agentc_agent_config_t *config,
    agentc_agent_t **out
) {
    if (!config || !config->llm || !out) {
        return AGENTC_ERR_INVALID_ARG;
    }

    agentc_agent_t *agent = AGENTC_CALLOC(1, sizeof(agentc_agent_t));
    if (!agent) return AGENTC_ERR_NO_MEMORY;

    /* Copy configuration */
    agent->llm = config->llm;
    agent->tools = config->tools;
    agent->name = config->name ? AGENTC_STRDUP(config->name) : NULL;
    agent->instructions = config->instructions ? AGENTC_STRDUP(config->instructions) : NULL;
    agent->max_iterations = config->max_iterations > 0 ?
        config->max_iterations : AGENTC_AGENT_DEFAULT_MAX_ITERATIONS;
    agent->max_tokens = config->max_tokens;
    agent->temperature = config->temperature > 0.0f ?
        config->temperature : AGENTC_AGENT_DEFAULT_TEMPERATURE;
    agent->tool_choice = AGENTC_STRDUP(
        config->tool_choice ? config->tool_choice : AGENTC_AGENT_DEFAULT_TOOL_CHOICE
    );
    agent->parallel_tool_calls = config->parallel_tool_calls;
    agent->stream = config->stream;
    agent->hooks = config->hooks;

    /* Generate tools JSON if tools are provided */
    if (agent->tools && agentc_tool_count(agent->tools) > 0) {
        agent->tools_json = agentc_tools_to_json(agent->tools);
        if (!agent->tools_json) {
            AGENTC_LOG_WARN("Failed to generate tools JSON");
        }
    }

    agent->current_iteration = 0;

    AGENTC_LOG_INFO("Agent created: %s (max_iter=%d, temp=%.2f)",
        agent->name ? agent->name : "unnamed",
        agent->max_iterations,
        agent->temperature);

    *out = agent;
    return AGENTC_OK;
}

void agentc_agent_destroy(agentc_agent_t *agent) {
    if (!agent) return;

    AGENTC_FREE(agent->name);
    AGENTC_FREE(agent->instructions);
    AGENTC_FREE(agent->tool_choice);
    AGENTC_FREE(agent->tools_json);
    AGENTC_FREE(agent);
}

void agentc_agent_reset(agentc_agent_t *agent) {
    if (!agent) return;
    agent->current_iteration = 0;
}

/*============================================================================
 * Result Helpers
 *============================================================================*/

void agentc_run_result_free(agentc_run_result_t *result) {
    if (!result) return;
    AGENTC_FREE(result->final_output);
    memset(result, 0, sizeof(*result));
}

/*============================================================================
 * Stream Context for Agent
 *============================================================================*/

typedef struct {
    agentc_agent_t *agent;
    char *content_buffer;
    size_t content_len;
    size_t content_cap;
    int aborted;
} agent_stream_ctx_t;

static int agent_stream_callback(const char *delta, size_t len, void *user_data) {
    agent_stream_ctx_t *ctx = (agent_stream_ctx_t *)user_data;

    /* Append to buffer */
    if (ctx->content_len + len + 1 > ctx->content_cap) {
        size_t new_cap = (ctx->content_cap + len + 1) * 2;
        char *new_buf = AGENTC_REALLOC(ctx->content_buffer, new_cap);
        if (!new_buf) return -1;
        ctx->content_buffer = new_buf;
        ctx->content_cap = new_cap;
    }

    memcpy(ctx->content_buffer + ctx->content_len, delta, len);
    ctx->content_len += len;
    ctx->content_buffer[ctx->content_len] = '\0';

    /* Call user hook */
    if (ctx->agent->hooks.on_content) {
        int ret = ctx->agent->hooks.on_content(
            delta, len, 0, ctx->agent->hooks.user_data
        );
        if (ret != 0) {
            ctx->aborted = 1;
            return ret;
        }
    }

    return 0;
}

/*============================================================================
 * Core ReACT Loop
 *============================================================================*/

static agentc_err_t run_react_loop(
    agentc_agent_t *agent,
    agentc_message_t **messages,
    agentc_run_result_t *result
) {
    agentc_err_t err = AGENTC_OK;

    result->status = AGENTC_RUN_SUCCESS;
    result->final_output = NULL;
    result->iterations = 0;
    result->total_tokens = 0;
    result->error_code = AGENTC_OK;

    for (int iter = 0; iter < agent->max_iterations; iter++) {
        agent->current_iteration = iter + 1;
        result->iterations = iter + 1;

        AGENTC_LOG_DEBUG("ReACT iteration %d/%d", iter + 1, agent->max_iterations);

        /* Build chat request */
        agentc_chat_request_t req = {
            .messages = *messages,
            .model = NULL,  /* Use client default */
            .temperature = agent->temperature,
            .max_tokens = agent->max_tokens,
            .stream = agent->stream,
            .tools_json = agent->tools_json,
            .tool_choice = agent->tools_json ? agent->tool_choice : NULL,
            .parallel_tool_calls = agent->parallel_tool_calls,
        };

        agentc_chat_response_t resp = {0};

        if (agent->stream) {
            /* Streaming mode */
            agent_stream_ctx_t stream_ctx = {
                .agent = agent,
                .content_buffer = AGENTC_MALLOC(1024),
                .content_len = 0,
                .content_cap = 1024,
                .aborted = 0,
            };

            if (!stream_ctx.content_buffer) {
                result->status = AGENTC_RUN_ERROR;
                result->error_code = AGENTC_ERR_NO_MEMORY;
                return AGENTC_ERR_NO_MEMORY;
            }
            stream_ctx.content_buffer[0] = '\0';

            err = agentc_llm_chat_stream(
                agent->llm, &req,
                agent_stream_callback, NULL,
                &stream_ctx
            );

            if (stream_ctx.aborted) {
                AGENTC_FREE(stream_ctx.content_buffer);
                result->status = AGENTC_RUN_ABORTED;
                return AGENTC_OK;
            }

            if (err != AGENTC_OK) {
                AGENTC_FREE(stream_ctx.content_buffer);
                result->status = AGENTC_RUN_ERROR;
                result->error_code = err;
                if (agent->hooks.on_error) {
                    agent->hooks.on_error(err, "LLM chat stream failed", agent->hooks.user_data);
                }
                return err;
            }

            /* For streaming, we need to get the response separately */
            /* Note: streaming with tool calls requires additional handling */
            resp.content = stream_ctx.content_buffer;
            resp.finish_reason = AGENTC_STRDUP("stop");  /* Assume stop for now */

            /* Notify content complete */
            if (agent->hooks.on_content && stream_ctx.content_len > 0) {
                agent->hooks.on_content("", 0, 1, agent->hooks.user_data);
            }
        } else {
            /* Blocking mode */
            err = agentc_llm_chat(agent->llm, &req, &resp);

            if (err != AGENTC_OK) {
                result->status = AGENTC_RUN_ERROR;
                result->error_code = err;
                if (agent->hooks.on_error) {
                    agent->hooks.on_error(err, "LLM chat failed", agent->hooks.user_data);
                }
                return err;
            }

            /* Notify content */
            if (agent->hooks.on_content && resp.content) {
                int ret = agent->hooks.on_content(
                    resp.content, strlen(resp.content), 1, agent->hooks.user_data
                );
                if (ret != 0) {
                    agentc_chat_response_free(&resp);
                    result->status = AGENTC_RUN_ABORTED;
                    return AGENTC_OK;
                }
            }
        }

        result->total_tokens += resp.total_tokens;

        /* Check finish reason */
        int has_tool_calls = resp.tool_calls != NULL;
        int is_tool_calls_finish = resp.finish_reason &&
            strcmp(resp.finish_reason, "tool_calls") == 0;

        if (has_tool_calls || is_tool_calls_finish) {
            /* Handle tool calls */
            AGENTC_LOG_DEBUG("LLM requested tool calls");

            if (!agent->tools) {
                AGENTC_LOG_ERROR("Tool calls requested but no tools registered");
                agentc_chat_response_free(&resp);
                result->status = AGENTC_RUN_ERROR;
                result->error_code = AGENTC_ERR_INVALID_ARG;
                return AGENTC_ERR_INVALID_ARG;
            }

            /* Notify tool call hook */
            if (agent->hooks.on_tool_call) {
                int ret = agent->hooks.on_tool_call(resp.tool_calls, agent->hooks.user_data);
                if (ret != 0) {
                    agentc_chat_response_free(&resp);
                    result->status = AGENTC_RUN_ABORTED;
                    return AGENTC_OK;
                }
            }

            /* Add assistant message with tool_calls to history */
            agentc_message_t *assistant_msg = agentc_message_create_assistant_tool_calls(
                resp.content, agentc_tool_call_clone(resp.tool_calls)
            );
            if (assistant_msg) {
                agentc_message_append(messages, assistant_msg);
            }

            /* Execute all tool calls */
            agentc_tool_result_t *results = NULL;
            err = agentc_tool_execute_all(agent->tools, resp.tool_calls, &results);

            if (err != AGENTC_OK) {
                agentc_chat_response_free(&resp);
                result->status = AGENTC_RUN_ERROR;
                result->error_code = err;
                return err;
            }

            /* Notify tool result hook */
            if (agent->hooks.on_tool_result) {
                int ret = agent->hooks.on_tool_result(results, agent->hooks.user_data);
                if (ret != 0) {
                    agentc_tool_result_free(results);
                    agentc_chat_response_free(&resp);
                    result->status = AGENTC_RUN_ABORTED;
                    return AGENTC_OK;
                }
            }

            /* Add tool result messages to history */
            for (agentc_tool_result_t *r = results; r; r = r->next) {
                agentc_message_t *tool_msg = agentc_message_create_tool_result(
                    r->tool_call_id, r->output
                );
                if (tool_msg) {
                    agentc_message_append(messages, tool_msg);
                }
            }

            agentc_tool_result_free(results);
            agentc_chat_response_free(&resp);

            /* Continue loop */
            continue;
        }

        /* No tool calls - we have a final response */
        if (resp.content) {
            result->final_output = AGENTC_STRDUP(resp.content);
        }

        agentc_chat_response_free(&resp);

        /* Success! */
        result->status = AGENTC_RUN_SUCCESS;

        /* Notify complete */
        if (agent->hooks.on_complete) {
            agent->hooks.on_complete(result, agent->hooks.user_data);
        }

        return AGENTC_OK;
    }

    /* Hit max iterations */
    AGENTC_LOG_WARN("Agent hit max iterations (%d)", agent->max_iterations);
    result->status = AGENTC_RUN_MAX_ITERATIONS;

    if (agent->hooks.on_complete) {
        agent->hooks.on_complete(result, agent->hooks.user_data);
    }

    return AGENTC_OK;
}

/*============================================================================
 * Public API
 *============================================================================*/

agentc_err_t agentc_agent_run(
    agentc_agent_t *agent,
    const char *input,
    agentc_run_result_t *result
) {
    if (!agent || !input || !result) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));

    /* Notify start */
    if (agent->hooks.on_start) {
        int ret = agent->hooks.on_start(input, agent->hooks.user_data);
        if (ret != 0) {
            result->status = AGENTC_RUN_ABORTED;
            return AGENTC_OK;
        }
    }

    /* Build message list */
    agentc_message_t *messages = NULL;

    /* Add system message if instructions provided */
    if (agent->instructions) {
        agentc_message_t *sys_msg = agentc_message_create(
            AGENTC_ROLE_SYSTEM, agent->instructions
        );
        if (sys_msg) {
            agentc_message_append(&messages, sys_msg);
        }
    }

    /* Add user message */
    agentc_message_t *user_msg = agentc_message_create(AGENTC_ROLE_USER, input);
    if (!user_msg) {
        agentc_message_free(messages);
        result->status = AGENTC_RUN_ERROR;
        result->error_code = AGENTC_ERR_NO_MEMORY;
        return AGENTC_ERR_NO_MEMORY;
    }
    agentc_message_append(&messages, user_msg);

    /* Run ReACT loop */
    agentc_err_t err = run_react_loop(agent, &messages, result);

    /* Cleanup messages */
    agentc_message_free(messages);

    return err;
}

agentc_err_t agentc_agent_run_with_history(
    agentc_agent_t *agent,
    agentc_message_t **messages,
    agentc_run_result_t *result
) {
    if (!agent || !messages || !result) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));

    /* Add system message at the beginning if not present and instructions provided */
    if (agent->instructions) {
        agentc_message_t *first = *messages;
        if (!first || first->role != AGENTC_ROLE_SYSTEM) {
            agentc_message_t *sys_msg = agentc_message_create(
                AGENTC_ROLE_SYSTEM, agent->instructions
            );
            if (sys_msg) {
                sys_msg->next = *messages;
                *messages = sys_msg;
            }
        }
    }

    /* Notify start with last user message */
    if (agent->hooks.on_start) {
        /* Find last user message */
        const char *last_input = "";
        for (agentc_message_t *m = *messages; m; m = m->next) {
            if (m->role == AGENTC_ROLE_USER && m->content) {
                last_input = m->content;
            }
        }

        int ret = agent->hooks.on_start(last_input, agent->hooks.user_data);
        if (ret != 0) {
            result->status = AGENTC_RUN_ABORTED;
            return AGENTC_OK;
        }
    }

    /* Run ReACT loop */
    return run_react_loop(agent, messages, result);
}

/*============================================================================
 * Convenience Function
 *============================================================================*/

agentc_err_t agentc_agent_quick_run(
    agentc_llm_client_t *llm,
    agentc_tool_registry_t *tools,
    const char *system,
    const char *user_input,
    char **output
) {
    if (!llm || !user_input || !output) {
        return AGENTC_ERR_INVALID_ARG;
    }

    agentc_agent_config_t config = {
        .llm = llm,
        .tools = tools,
        .instructions = system,
        .max_iterations = AGENTC_AGENT_DEFAULT_MAX_ITERATIONS,
        .temperature = AGENTC_AGENT_DEFAULT_TEMPERATURE,
    };

    agentc_agent_t *agent = NULL;
    agentc_err_t err = agentc_agent_create(&config, &agent);
    if (err != AGENTC_OK) {
        return err;
    }

    agentc_run_result_t result = {0};
    err = agentc_agent_run(agent, user_input, &result);

    if (err == AGENTC_OK && result.status == AGENTC_RUN_SUCCESS) {
        *output = result.final_output;
        result.final_output = NULL;  /* Transfer ownership */
    } else {
        *output = NULL;
        if (err == AGENTC_OK) {
            /* Run completed but with non-success status */
            err = result.error_code != AGENTC_OK ? result.error_code : AGENTC_ERR_HTTP;
        }
    }

    agentc_run_result_free(&result);
    agentc_agent_destroy(agent);

    return err;
}
