/**
 * @file agent.c
 * @brief Agent implementation with arena memory management
 */

#include "agentc/agent.h"
#include "agentc/arena.h"
#include "agentc/llm.h"
#include "agentc/tool.h"
#include "agentc/message.h"
#include "agentc/log.h"
#include "agentc/trace.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_ARENA_SIZE (1024 * 1024)  /* 1MB per agent */

/*============================================================================
 * Forward Declarations
 *============================================================================*/

/* Session internal API */
agentc_err_t ac_session_add_agent(struct ac_session *session, ac_agent_t *agent);

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
    size_t message_count;
    
    const char *name;
    const char *instructions;
    int max_iterations;
    
    /* Trace context */
    ac_trace_ctx_t trace_ctx;
} agent_priv_t;

/*============================================================================
 * Agent Structure
 *============================================================================*/

struct ac_agent {
    agent_priv_t *priv;
};

/*============================================================================
 * Trace Helper Functions
 *============================================================================*/

/* Forward declaration for message JSON conversion */
cJSON* ac_message_to_json(const ac_message_t* msg);
cJSON* ac_tool_call_to_json(const ac_tool_call_t* call);

/**
 * @brief Serialize message list to JSON array string
 */
static char *serialize_messages_json(const ac_message_t *messages) {
    if (!messages) {
        return NULL;
    }
    
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }
    
    for (const ac_message_t *msg = messages; msg; msg = msg->next) {
        cJSON *msg_json = ac_message_to_json(msg);
        if (msg_json) {
            cJSON_AddItemToArray(arr, msg_json);
        }
    }
    
    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    
    return json_str;
}

/**
 * @brief Serialize tool calls to JSON array string
 */
static char *serialize_tool_calls_json(const ac_tool_call_t *calls) {
    if (!calls) {
        return NULL;
    }
    
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }
    
    for (const ac_tool_call_t *call = calls; call; call = call->next) {
        cJSON *call_json = ac_tool_call_to_json(call);
        if (call_json) {
            cJSON_AddItemToArray(arr, call_json);
        }
    }
    
    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    
    return json_str;
}

/**
 * @brief Emit trace event with common fields populated
 */
static void emit_trace_event(
    agent_priv_t *priv, 
    ac_trace_event_type_t type,
    ac_trace_event_t *event
) {
    if (!ac_trace_is_enabled()) {
        return;
    }
    
    event->type = type;
    event->timestamp_ms = ac_trace_timestamp_ms();
    event->trace_id = priv->trace_ctx.trace_id;
    event->agent_name = priv->name;
    event->sequence = ac_trace_ctx_next_seq(&priv->trace_ctx);
    
    ac_trace_emit(event);
}

/*============================================================================
 * Tool Schema Builder
 *============================================================================*/

/**
 * @brief Build tools JSON schema for LLM
 *
 * Uses the tool registry to generate OpenAI-compatible tools array.
 *
 * @param priv Agent private data
 * @return JSON string (caller must free), NULL if no tools
 */
static char *build_tools_schema(agent_priv_t *priv) {
    if (!priv->tools) {
        return NULL;
    }
    
    return ac_tool_registry_schema(priv->tools);
}

/**
 * @brief Execute a single tool call
 *
 * @param priv Agent private data
 * @param call Tool call to execute
 * @return Result string (caller must free), NULL on error
 */
static char *execute_tool_call(agent_priv_t *priv, const ac_tool_call_t *call) {
    if (!call || !call->name) {
        return strdup("{\"error\":\"Invalid tool call\"}");
    }
    
    if (!priv->tools) {
        AC_LOG_WARN("No tool registry configured");
        return strdup("{\"error\":\"No tools available\"}");
    }
    
    /* Create execution context */
    ac_tool_ctx_t ctx = {
        .session_id = NULL,  /* TODO: Get from session */
        .working_dir = NULL,
        .user_data = NULL
    };
    
    AC_LOG_INFO("Executing tool: %s(%s)", call->name, 
                call->arguments ? call->arguments : "{}");
    
    /* Emit tool_call trace event */
    uint64_t tool_start_ms = ac_trace_timestamp_ms();
    if (ac_trace_is_enabled()) {
        ac_trace_event_t event = {0};
        event.data.tool_call.id = call->id;
        event.data.tool_call.name = call->name;
        event.data.tool_call.arguments = call->arguments;
        emit_trace_event(priv, AC_TRACE_TOOL_CALL, &event);
    }
    
    char *result = ac_tool_registry_call(
        priv->tools,
        call->name,
        call->arguments ? call->arguments : "{}",
        &ctx
    );
    
    AC_LOG_DEBUG("Tool %s returned: %s", call->name, result ? result : "NULL");
    
    /* Emit tool_result trace event */
    if (ac_trace_is_enabled()) {
        uint64_t tool_end_ms = ac_trace_timestamp_ms();
        ac_trace_event_t event = {0};
        event.data.tool_result.id = call->id;
        event.data.tool_result.name = call->name;
        event.data.tool_result.result = result;
        event.data.tool_result.duration_ms = tool_end_ms - tool_start_ms;
        event.data.tool_result.success = (result != NULL && strstr(result, "\"error\"") == NULL) ? 1 : 0;
        emit_trace_event(priv, AC_TRACE_TOOL_RESULT, &event);
    }
    
    return result ? result : strdup("{\"error\":\"Tool returned NULL\"}");
}

/*============================================================================
 * Agent Implementation
 *============================================================================*/

/**
 * @brief Copy tool calls from response to arena
 */
static ac_tool_call_t *copy_tool_calls_to_arena(
    arena_t *arena,
    ac_tool_call_t *calls
) {
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
            if (!first) {
                first = copy;
            }
            if (last) {
                last->next = copy;
            }
            last = copy;
        }
    }
    
    return first;
}

static ac_agent_result_t *agent_run_impl(agent_priv_t *priv, const char *message) {
    if (!priv || !priv->arena || !priv->llm) {
        return NULL;
    }
    
    /* Initialize trace context */
    ac_trace_ctx_init(&priv->trace_ctx, priv->name);
    
    /* Emit agent_start trace event */
    if (ac_trace_is_enabled()) {
        ac_trace_event_t event = {0};
        event.data.agent_start.message = message;
        event.data.agent_start.instructions = priv->instructions;
        event.data.agent_start.max_iterations = priv->max_iterations;
        event.data.agent_start.tool_count = priv->tools ? ac_tool_registry_count(priv->tools) : 0;
        emit_trace_event(priv, AC_TRACE_AGENT_START, &event);
    }
    
    /* Add system message if this is the first message */
    if (!priv->messages && priv->instructions) {
        ac_message_t *sys_msg = ac_message_create(
            priv->arena, 
            AC_ROLE_SYSTEM, 
            priv->instructions
        );
        if (sys_msg) {
            ac_message_append(&priv->messages, sys_msg);
            priv->message_count++;
        }
    }
    
    /* Add user message to history */
    ac_message_t *user_msg = ac_message_create(priv->arena, AC_ROLE_USER, message);
    if (!user_msg) {
        AC_LOG_ERROR("Failed to create user message");
        return NULL;
    }
    ac_message_append(&priv->messages, user_msg);
    priv->message_count++;
    
    AC_LOG_DEBUG("Added user message, total messages: %zu", priv->message_count);
    
    /* Build tools schema if tools are configured */
    char *tools_schema = build_tools_schema(priv);
    
    /* ReACT loop */
    char *final_content = NULL;
    int iteration = 0;
    
    while (iteration < priv->max_iterations) {
        iteration++;
        AC_LOG_DEBUG("ReACT iteration %d/%d", iteration, priv->max_iterations);
        
        /* Emit react_iter_start trace event */
        if (ac_trace_is_enabled()) {
            ac_trace_event_t event = {0};
            event.data.react_iter.iteration = iteration;
            event.data.react_iter.max_iterations = priv->max_iterations;
            emit_trace_event(priv, AC_TRACE_REACT_ITER_START, &event);
        }
        
        /* Emit llm_request trace event */
        char *messages_json = NULL;
        uint64_t llm_start_ms = ac_trace_timestamp_ms();
        if (ac_trace_is_enabled()) {
            messages_json = serialize_messages_json(priv->messages);
            ac_trace_event_t event = {0};
            event.data.llm_request.model = NULL;  /* Will be filled in llm.c */
            event.data.llm_request.messages_json = messages_json;
            event.data.llm_request.tools_json = tools_schema;
            event.data.llm_request.message_count = priv->message_count;
            emit_trace_event(priv, AC_TRACE_LLM_REQUEST, &event);
        }
        
        /* Call LLM with tools */
        ac_chat_response_t response;
        ac_chat_response_init(&response);
        
        agentc_err_t err = ac_llm_chat_with_tools(
            priv->llm,
            priv->messages,
            tools_schema,
            &response
        );
        
        /* Emit llm_response trace event */
        if (ac_trace_is_enabled()) {
            uint64_t llm_end_ms = ac_trace_timestamp_ms();
            char *tool_calls_json = serialize_tool_calls_json(response.tool_calls);
            
            ac_trace_event_t event = {0};
            event.data.llm_response.content = response.content;
            event.data.llm_response.tool_calls_json = tool_calls_json;
            event.data.llm_response.tool_call_count = response.tool_call_count;
            event.data.llm_response.prompt_tokens = response.prompt_tokens;
            event.data.llm_response.completion_tokens = response.completion_tokens;
            event.data.llm_response.total_tokens = response.total_tokens;
            event.data.llm_response.finish_reason = response.finish_reason;
            event.data.llm_response.duration_ms = llm_end_ms - llm_start_ms;
            emit_trace_event(priv, AC_TRACE_LLM_RESPONSE, &event);
            
            /* Accumulate token usage */
            priv->trace_ctx.total_prompt_tokens += response.prompt_tokens;
            priv->trace_ctx.total_completion_tokens += response.completion_tokens;
            
            if (tool_calls_json) free(tool_calls_json);
        }
        
        if (messages_json) {
            free(messages_json);
            messages_json = NULL;
        }
        
        if (err != AGENTC_OK) {
            AC_LOG_ERROR("LLM chat failed: %d", err);
            if (tools_schema) free(tools_schema);
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
                priv->arena,
                response.content,  /* May be NULL */
                arena_calls
            );
            
            if (asst_msg) {
                ac_message_append(&priv->messages, asst_msg);
                priv->message_count++;
            }
            
            /* Execute each tool call and add results */
            for (ac_tool_call_t *call = response.tool_calls; call; call = call->next) {
                char *result = execute_tool_call(priv, call);
                
                /* Add tool result message */
                ac_message_t *tool_msg = ac_message_create_tool_result(
                    priv->arena,
                    call->id,
                    result ? result : "{\"error\":\"Tool execution failed\"}"
                );
                
                if (tool_msg) {
                    ac_message_append(&priv->messages, tool_msg);
                    priv->message_count++;
                }
                
                if (result) {
                    free(result);
                }
            }
            
            /* Emit react_iter_end trace event */
            if (ac_trace_is_enabled()) {
                ac_trace_event_t event = {0};
                event.data.react_iter.iteration = iteration;
                event.data.react_iter.max_iterations = priv->max_iterations;
                emit_trace_event(priv, AC_TRACE_REACT_ITER_END, &event);
            }
            
            /* Free response and continue loop */
            ac_chat_response_free(&response);
            continue;
        }
        
        /* No tool calls - we have the final response */
        if (response.content) {
            final_content = arena_strdup(priv->arena, response.content);
            
            /* Add assistant message to history */
            ac_message_t *asst_msg = ac_message_create(
                priv->arena, AC_ROLE_ASSISTANT, response.content
            );
            if (asst_msg) {
                ac_message_append(&priv->messages, asst_msg);
                priv->message_count++;
            }
        }
        
        /* Emit react_iter_end trace event */
        if (ac_trace_is_enabled()) {
            ac_trace_event_t event = {0};
            event.data.react_iter.iteration = iteration;
            event.data.react_iter.max_iterations = priv->max_iterations;
            emit_trace_event(priv, AC_TRACE_REACT_ITER_END, &event);
        }
        
        ac_chat_response_free(&response);
        break;  /* Exit loop - we have the final response */
    }
    
    /* Cleanup */
    if (tools_schema) {
        free(tools_schema);
    }
    
    if (iteration >= priv->max_iterations && !final_content) {
        AC_LOG_WARN("ReACT loop reached max iterations (%d)", priv->max_iterations);
    }
    
    /* Emit agent_end trace event */
    if (ac_trace_is_enabled()) {
        uint64_t end_time_ms = ac_trace_timestamp_ms();
        ac_trace_event_t event = {0};
        event.data.agent_end.content = final_content;
        event.data.agent_end.iterations = iteration;
        event.data.agent_end.total_prompt_tokens = priv->trace_ctx.total_prompt_tokens;
        event.data.agent_end.total_completion_tokens = priv->trace_ctx.total_completion_tokens;
        event.data.agent_end.duration_ms = end_time_ms - priv->trace_ctx.start_time_ms;
        emit_trace_event(priv, AC_TRACE_AGENT_END, &event);
    }
    
    /* Allocate result from agent's arena */
    ac_agent_result_t *result = (ac_agent_result_t *)arena_alloc(
        priv->arena, 
        sizeof(ac_agent_result_t)
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

ac_agent_t *ac_agent_create(ac_session_t *session, const ac_agent_params_t *params) {
    if (!session || !params) {
        AC_LOG_ERROR("Invalid arguments to ac_agent_create");
        return NULL;
    }
    
    /* Allocate agent structure */
    ac_agent_t *agent = (ac_agent_t *)calloc(1, sizeof(ac_agent_t));
    if (!agent) {
        AC_LOG_ERROR("Failed to allocate agent");
        return NULL;
    }
    
    /* Allocate private data */
    agent_priv_t *priv = (agent_priv_t *)calloc(1, sizeof(agent_priv_t));
    if (!priv) {
        AC_LOG_ERROR("Failed to allocate agent private data");
        free(agent);
        return NULL;
    }
    
    /* Create agent's arena */
    priv->arena = arena_create(DEFAULT_ARENA_SIZE);
    if (!priv->arena) {
        AC_LOG_ERROR("Failed to create arena");
        free(priv);
        free(agent);
        return NULL;
    }
    
    /* Store session reference */
    priv->session = session;
    
    /* Initialize message history */
    priv->messages = NULL;
    priv->message_count = 0;
    
    /* Copy name and instructions to arena */
    if (params->name) {
        priv->name = arena_strdup(priv->arena, params->name);
    }
    
    if (params->instructions) {
        priv->instructions = arena_strdup(priv->arena, params->instructions);
    }
    
    /* Set max iterations */
    priv->max_iterations = params->max_iterations > 0 ? 
        params->max_iterations : AC_AGENT_DEFAULT_MAX_ITERATIONS;
    
    /* Create LLM using arena */
    priv->llm = ac_llm_create(priv->arena, &params->llm);
    if (!priv->llm) {
        AC_LOG_ERROR("Failed to create LLM");
        arena_destroy(priv->arena);
        free(priv);
        free(agent);
        return NULL;
    }
    
    /* Store tool registry reference */
    priv->tools = params->tools;
    
    if (priv->tools) {
        size_t tool_count = ac_tool_registry_count(priv->tools);
        AC_LOG_DEBUG("Agent configured with %zu tools", tool_count);
    }
    
    /* Setup agent */
    agent->priv = priv;
    
    /* Add agent to session */
    if (ac_session_add_agent(session, agent) != AGENTC_OK) {
        AC_LOG_ERROR("Failed to add agent to session");
        arena_destroy(priv->arena);
        free(priv);
        free(agent);
        return NULL;
    }
    
    AC_LOG_INFO("Agent created: %s (arena=%zuKB, max_iter=%d)",
                priv->name ? priv->name : "unnamed",
                DEFAULT_ARENA_SIZE / 1024,
                priv->max_iterations);
    
    return agent;
}

ac_agent_result_t *ac_agent_run(ac_agent_t *agent, const char *message) {
    if (!agent || !agent->priv || !message) {
        AC_LOG_ERROR("Invalid arguments to ac_agent_run");
        return NULL;
    }
    
    return agent_run_impl(agent->priv, message);
}

void ac_agent_destroy(ac_agent_t *agent) {
    if (!agent) {
        return;
    }
    
    agent_priv_t *priv = agent->priv;
    if (priv) {
        /* Cleanup LLM provider resources (HTTP client, etc) */
        if (priv->llm) {
            ac_llm_cleanup(priv->llm);
        }
        
        /* Destroy arena (this frees llm, messages, and all other allocations) */
        if (priv->arena) {
            AC_LOG_DEBUG("Destroying agent arena");
            arena_destroy(priv->arena);
        }
        free(priv);
    }
    
    free(agent);
}
