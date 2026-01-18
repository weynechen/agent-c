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

struct ac_agent {
    /* Configuration */
    const char *name;
    ac_llm_t *llm;
    ac_tools_t *tools;
    ac_memory_t *memory;
    int max_iterations;
    uint32_t timeout_ms;

    /* Runtime state */
    char *tools_json;             /* Cached tools JSON */
    int current_iteration;
};

/*============================================================================
 * Agent Create/Destroy
 *============================================================================*/

ac_agent_t *ac_agent_create(const ac_agent_params_t *params) {
    if (!params || !params->llm) {
        AC_LOG_ERROR("Invalid parameters: llm is required");
        return NULL;
    }

    ac_agent_t *agent = AGENTC_CALLOC(1, sizeof(ac_agent_t));
    if (!agent) return NULL;

    /* Copy configuration */
    agent->name = params->name;
    agent->llm = params->llm;
    agent->tools = params->tools;
    agent->memory = params->memory;
    agent->max_iterations = params->max_iterations > 0 ?
        params->max_iterations : AC_AGENT_DEFAULT_MAX_ITERATIONS;
    agent->timeout_ms = params->timeout_ms;

    /* Generate tools JSON if tools are provided */
    if (agent->tools && ac_tool_count(agent->tools) > 0) {
        agent->tools_json = ac_tools_to_json(agent->tools);
        if (!agent->tools_json) {
            AC_LOG_WARN("Failed to generate tools JSON");
        }
    }

    agent->current_iteration = 0;

    AC_LOG_INFO("Agent created: %s (max_iter=%d)",
        agent->name ? agent->name : "unnamed",
        agent->max_iterations);

    return agent;
}

void ac_agent_destroy(ac_agent_t *agent) {
    if (!agent) return;

    AGENTC_FREE(agent->tools_json);
    AGENTC_FREE(agent);
}

void ac_agent_reset(ac_agent_t *agent) {
    if (!agent) return;
    agent->current_iteration = 0;
}

void ac_agent_result_free(ac_agent_result_t *result) {
    if (!result) return;
    AGENTC_FREE(result->response);
    memset(result, 0, sizeof(*result));
}

/*============================================================================
 * Agent Run (Synchronous)
 *============================================================================*/

agentc_err_t ac_agent_run_sync(
    ac_agent_t *agent,
    const char *input,
    ac_agent_result_t *result
) {
    if (!agent || !input || !result) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->status = AC_RUN_SUCCESS;
    
    /* Build message history */
    ac_message_t *messages = NULL;

    /* Add messages from memory if available */
    if (agent->memory) {
        const ac_message_t *mem_msgs = ac_memory_get_messages(agent->memory);
        for (const ac_message_t *m = mem_msgs; m; m = m->next) {
            ac_message_t *copy = ac_message_create(m->role, m->content);
            if (copy) {
                ac_message_append(&messages, copy);
            }
        }
    }
    
    /* Add user input */
    ac_message_t *user_msg = ac_message_create(AC_ROLE_USER, input);
    if (!user_msg) {
        ac_message_free(messages);
        return AGENTC_ERR_NO_MEMORY;
        }
    ac_message_append(&messages, user_msg);
    
    /* Add to memory */
    if (agent->memory) {
        ac_memory_add(agent->memory, user_msg);
    }

    /* ReACT loop */
    agent->current_iteration = 0;
    agentc_err_t err = AGENTC_OK;

    while (agent->current_iteration < agent->max_iterations) {
        agent->current_iteration++;

        AC_LOG_DEBUG("ReACT iteration %d/%d", 
                         agent->current_iteration, agent->max_iterations);
        
        /* Call LLM */
        ac_chat_response_t resp = {0};
        err = ac_llm_chat(agent->llm, messages, agent->tools_json, &resp);

            if (err != AGENTC_OK) {
            AC_LOG_ERROR("LLM call failed: %d", err);
            result->status = AC_RUN_ERROR;
                result->error_code = err;
            ac_message_free(messages);
                return err;
        }

        result->total_tokens += resp.total_tokens;

        /* Check finish reason */
        if (resp.finish_reason && strcmp(resp.finish_reason, "stop") == 0) {
            /* Normal completion */
            result->response = resp.content ? AGENTC_STRDUP(resp.content) : NULL;
            resp.content = NULL;
            
            /* Add assistant response to memory */
            if (agent->memory && result->response) {
                ac_message_t *assist_msg = ac_message_create(AC_ROLE_ASSISTANT, result->response);
                if (assist_msg) {
                    ac_memory_add(agent->memory, assist_msg);
                    ac_message_free(assist_msg);
                }
            }
            
            ac_chat_response_free(&resp);
            ac_message_free(messages);
            result->iterations = agent->current_iteration;
            return AGENTC_OK;
            }

        /* Check for tool calls */
        if (resp.tool_calls) {
            AC_LOG_DEBUG("LLM requested %d tool call(s)", 
                           resp.tool_calls->next ? 2 : 1);

            /* Add assistant message with tool calls to history */
            ac_message_t *assist_msg = ac_message_create_assistant_tool_calls(
                resp.content, resp.tool_calls
            );
            resp.tool_calls = NULL;  /* Transfer ownership */
            resp.content = NULL;
            
            if (!assist_msg) {
                ac_chat_response_free(&resp);
                ac_message_free(messages);
                result->status = AC_RUN_ERROR;
                result->error_code = AGENTC_ERR_NO_MEMORY;
                return AGENTC_ERR_NO_MEMORY;
            }
            
            ac_message_append(&messages, assist_msg);
            
            /* Add to memory */
            if (agent->memory) {
                ac_memory_add(agent->memory, assist_msg);
            }

            /* Execute tools */
            if (agent->tools) {
                ac_tool_result_t *tool_results = NULL;
                err = ac_tool_execute_all(agent->tools, assist_msg->tool_calls, &tool_results);

            if (err != AGENTC_OK) {
                    AC_LOG_ERROR("Tool execution failed: %d", err);
                    ac_chat_response_free(&resp);
                    ac_message_free(messages);
                    result->status = AC_RUN_ERROR;
                result->error_code = err;
                return err;
            }

                /* Add tool results to messages */
                for (ac_tool_result_t *tr = tool_results; tr; tr = tr->next) {
                    ac_message_t *tool_msg = ac_message_create_tool_result(
                        tr->tool_call_id, tr->output
                );
                if (tool_msg) {
                        ac_message_append(&messages, tool_msg);
                        
                        /* Add to memory */
                        if (agent->memory) {
                            ac_memory_add(agent->memory, tool_msg);
                        }
                    }
                }
                
                ac_tool_result_free(tool_results);
            }
            
            ac_chat_response_free(&resp);
            continue;  /* Next iteration */
        }

        /* No content and no tool calls - unexpected */
        AC_LOG_WARN("LLM returned no content and no tool calls");
        ac_chat_response_free(&resp);
        break;
    }

    /* Hit max iterations */
    ac_message_free(messages);
    result->status = AC_RUN_MAX_ITERATIONS;
    result->iterations = agent->current_iteration;
    result->response = AGENTC_STRDUP("Maximum iterations reached without completion");
    
    AC_LOG_WARN("Agent hit max iterations: %d", agent->max_iterations);
    return AGENTC_OK;
}

/*============================================================================
 * Agent Run (Streaming) - NOT IMPLEMENTED YET
 *============================================================================*/

ac_stream_t *ac_agent_run(ac_agent_t *agent, const char *input) {
    (void)agent;
    (void)input;
    
    AC_LOG_ERROR("Streaming mode not implemented yet");
    return NULL;
}

int ac_stream_is_running(ac_stream_t *stream) {
    (void)stream;
    return 0;
}

ac_stream_result_t *ac_stream_next(ac_stream_t *stream, int timeout_ms) {
    (void)stream;
    (void)timeout_ms;
    return NULL;
        }

void ac_stream_destroy(ac_stream_t *stream) {
    (void)stream;
}
