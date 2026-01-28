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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_ARENA_SIZE (1024 * 1024)  // 1MB per agent

/*============================================================================
 * Forward Declarations
 *============================================================================*/

// Session internal API
agentc_err_t ac_session_add_agent(struct ac_session* session, ac_agent_t* agent);

/*============================================================================
 * Agent Private Data
 *============================================================================*/

typedef struct {
    arena_t* arena;
    ac_llm_t* llm;
    ac_tool_group_t* tools;
    struct ac_session* session;
    
    // Message history (stored in arena)
    ac_message_t* messages;
    size_t message_count;
    
    const char* name;
    const char* instructions;
    int max_iterations;
    
    // MOC tool integration
    const char** tool_names;            // Selected tool names (NULL-terminated)
    const ac_tool_entry_t* tool_table;  // Global tool table from MOC
} agent_priv_t;

/*============================================================================
 * Agent Structure
 *============================================================================*/

struct ac_agent {
    agent_priv_t* priv;
};

/*============================================================================
 * Tool Schema Builder
 *============================================================================*/

/**
 * @brief Build tools JSON schema for selected tools
 *
 * Generates OpenAI-compatible tools array from selected tool names.
 * Uses the MOC-generated G_TOOL_TABLE for schema lookup.
 *
 * @param priv Agent private data
 * @return JSON string (caller must free), NULL if no tools
 */
static char* build_tools_schema(agent_priv_t* priv) {
    if (!priv->tool_names || !priv->tool_table) {
        return NULL;
    }
    
    /* Count tools and calculate buffer size */
    size_t total_len = 2;  /* For "[]" */
    int count = 0;
    
    for (const char** name = priv->tool_names; *name; name++) {
        /* Find tool in table */
        const ac_tool_entry_t* entry = priv->tool_table;
        while (entry->name) {
            if (strcmp(entry->name, *name) == 0 && entry->schema) {
                /* Schema is already in OpenAI function format:
                 * {"type":"function","function":{...}}
                 */
                total_len += strlen(entry->schema) + 2;  /* +2 for comma and safety */
                count++;
                break;
            }
            entry++;
        }
    }
    
    if (count == 0) {
        return NULL;
    }
    
    /* Allocate buffer */
    char* result = (char*)malloc(total_len + 1);
    if (!result) {
        return NULL;
    }
    
    /* Build JSON array */
    char* p = result;
    *p++ = '[';
    
    int first = 1;
    for (const char** name = priv->tool_names; *name; name++) {
        const ac_tool_entry_t* entry = priv->tool_table;
        while (entry->name) {
            if (strcmp(entry->name, *name) == 0 && entry->schema) {
                if (!first) {
                    *p++ = ',';
                }
                first = 0;
                
                size_t len = strlen(entry->schema);
                memcpy(p, entry->schema, len);
                p += len;
                break;
            }
            entry++;
        }
    }
    
    *p++ = ']';
    *p = '\0';
    
    AC_LOG_DEBUG("Built tools schema with %d tools, %zu bytes", count, strlen(result));
    return result;
}

/**
 * @brief Find tool entry by name
 */
static const ac_tool_entry_t* find_tool(agent_priv_t* priv, const char* name) {
    if (!priv->tool_table || !name) {
        return NULL;
    }
    
    const ac_tool_entry_t* entry = priv->tool_table;
    while (entry->name) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
        entry++;
    }
    return NULL;
}

/**
 * @brief Execute a single tool call
 *
 * @param priv Agent private data
 * @param call Tool call to execute
 * @return Result string (caller must free), NULL on error
 */
static char* execute_tool_call(agent_priv_t* priv, const ac_tool_call_t* call) {
    if (!call || !call->name) {
        return strdup("{\"error\":\"Invalid tool call\"}");
    }
    
    /* Find tool */
    const ac_tool_entry_t* tool = find_tool(priv, call->name);
    if (!tool) {
        AC_LOG_WARN("Tool not found: %s", call->name);
        char* err = (char*)malloc(256);
        snprintf(err, 256, "{\"error\":\"Tool '%s' not found\"}", call->name);
        return err;
    }
    
    if (!tool->wrapper) {
        AC_LOG_WARN("Tool has no wrapper: %s", call->name);
        return strdup("{\"error\":\"Tool has no wrapper function\"}");
    }
    
    AC_LOG_INFO("Executing tool: %s(%s)", call->name, 
                call->arguments ? call->arguments : "{}");
    
    /* Call wrapper function */
    char* result = tool->wrapper(call->arguments ? call->arguments : "{}");
    
    AC_LOG_DEBUG("Tool %s returned: %s", call->name, result ? result : "NULL");
    
    return result ? result : strdup("{\"error\":\"Tool returned NULL\"}");
}

/*============================================================================
 * Agent Implementation
 *============================================================================*/

/**
 * @brief Copy tool calls from response to arena
 */
static ac_tool_call_t* copy_tool_calls_to_arena(
    arena_t* arena,
    ac_tool_call_t* calls
) {
    if (!arena || !calls) {
        return NULL;
    }
    
    ac_tool_call_t* first = NULL;
    ac_tool_call_t* last = NULL;
    
    for (ac_tool_call_t* call = calls; call; call = call->next) {
        ac_tool_call_t* copy = ac_tool_call_create(
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

static ac_agent_result_t* agent_run_sync_impl(agent_priv_t* priv, const char* message) {
    if (!priv || !priv->arena || !priv->llm) {
        return NULL;
    }
    
    /* Add system message if this is the first message */
    if (!priv->messages && priv->instructions) {
        ac_message_t* sys_msg = ac_message_create(
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
    ac_message_t* user_msg = ac_message_create(priv->arena, AC_ROLE_USER, message);
    if (!user_msg) {
        AC_LOG_ERROR("Failed to create user message");
        return NULL;
    }
    ac_message_append(&priv->messages, user_msg);
    priv->message_count++;
    
    AC_LOG_DEBUG("Added user message, total messages: %zu", priv->message_count);
    
    /* Build tools schema if tools are configured */
    char* tools_schema = build_tools_schema(priv);
    
    /* ReACT loop */
    char* final_content = NULL;
    int iteration = 0;
    
    while (iteration < priv->max_iterations) {
        iteration++;
        AC_LOG_DEBUG("ReACT iteration %d/%d", iteration, priv->max_iterations);
        
        /* Call LLM with tools */
        ac_chat_response_t response;
        ac_chat_response_init(&response);
        
        agentc_err_t err = ac_llm_chat_with_tools(
            priv->llm,
            priv->messages,
            tools_schema,
            &response
        );
        
        if (err != AGENTC_OK) {
            AC_LOG_ERROR("LLM chat failed: %d", err);
            if (tools_schema) free(tools_schema);
            return NULL;
        }
        
        /* Check if there are tool calls */
        if (ac_chat_response_has_tool_calls(&response)) {
            AC_LOG_INFO("LLM requested %d tool call(s)", response.tool_call_count);
            
            /* Copy tool calls to arena and add assistant message */
            ac_tool_call_t* arena_calls = copy_tool_calls_to_arena(
                priv->arena, response.tool_calls
            );
            
            ac_message_t* asst_msg = ac_message_create_with_tool_calls(
                priv->arena,
                response.content,  /* May be NULL */
                arena_calls
            );
            
            if (asst_msg) {
                ac_message_append(&priv->messages, asst_msg);
                priv->message_count++;
            }
            
            /* Execute each tool call and add results */
            for (ac_tool_call_t* call = response.tool_calls; call; call = call->next) {
                char* result = execute_tool_call(priv, call);
                
                /* Add tool result message */
                ac_message_t* tool_msg = ac_message_create_tool_result(
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
            
            /* Free response and continue loop */
            ac_chat_response_free(&response);
            continue;
        }
        
        /* No tool calls - we have the final response */
        if (response.content) {
            final_content = arena_strdup(priv->arena, response.content);
            
            /* Add assistant message to history */
            ac_message_t* asst_msg = ac_message_create(
                priv->arena, AC_ROLE_ASSISTANT, response.content
            );
            if (asst_msg) {
                ac_message_append(&priv->messages, asst_msg);
                priv->message_count++;
            }
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
    
    /* Allocate result from agent's arena */
    ac_agent_result_t* result = (ac_agent_result_t*)arena_alloc(
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

ac_agent_t* ac_agent_create(ac_session_t* session, const ac_agent_params_t* params) {
    if (!session || !params) {
        AC_LOG_ERROR("Invalid arguments to ac_agent_create");
        return NULL;
    }
    
    // Allocate agent structure
    ac_agent_t* agent = (ac_agent_t*)calloc(1, sizeof(ac_agent_t));
    if (!agent) {
        AC_LOG_ERROR("Failed to allocate agent");
        return NULL;
    }
    
    // Allocate private data
    agent_priv_t* priv = (agent_priv_t*)calloc(1, sizeof(agent_priv_t));
    if (!priv) {
        AC_LOG_ERROR("Failed to allocate agent private data");
        free(agent);
        return NULL;
    }
    
    // Create agent's arena
    priv->arena = arena_create(DEFAULT_ARENA_SIZE);
    if (!priv->arena) {
        AC_LOG_ERROR("Failed to create arena");
        free(priv);
        free(agent);
        return NULL;
    }
    
    // Store session reference
    priv->session = session;
    
    // Initialize message history
    priv->messages = NULL;
    priv->message_count = 0;
    
    // Copy name and instructions to arena
    if (params->name) {
        priv->name = arena_strdup(priv->arena, params->name);
    }
    
    if (params->instructions) {
        priv->instructions = arena_strdup(priv->arena, params->instructions);
    }
    
    // Set max iterations
    priv->max_iterations = params->max_iterations > 0 ? 
        params->max_iterations : AC_AGENT_DEFAULT_MAX_ITERATIONS;
    
    // Create LLM using arena
    priv->llm = ac_llm_create(priv->arena, &params->llm_params);
    if (!priv->llm) {
        AC_LOG_ERROR("Failed to create LLM");
        arena_destroy(priv->arena);
        free(priv);
        free(agent);
        return NULL;
    }
    
    // Create tools using arena (if specified)
    // Note: tools and tool_table are used together:
    //   - tools: NULL-terminated array of tool names to use
    //   - tool_table: pointer to MOC-generated G_TOOL_TABLE
    priv->tool_names = params->tools;
    priv->tool_table = params->tool_table;
    
    if (params->tools && params->tool_table) {
        // Create tool group for compatibility with existing tool system
        priv->tools = ac_tool_group_create(priv->arena, "moc_tools");
        // Tools are optional, so don't fail if creation fails
        
        // Count selected tools
        size_t tool_count = 0;
        for (const char** p = params->tools; *p; p++) {
            tool_count++;
        }
        AC_LOG_DEBUG("Agent configured with %zu tools from MOC table", tool_count);
    }
    
    // Setup agent
    agent->priv = priv;
    
    // Add agent to session
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

ac_agent_result_t* ac_agent_run_sync(ac_agent_t* agent, const char* message) {
    if (!agent || !agent->priv || !message) {
        AC_LOG_ERROR("Invalid arguments to ac_agent_run_sync");
        return NULL;
    }
    
    return agent_run_sync_impl(agent->priv, message);
}

void ac_agent_destroy(ac_agent_t* agent) {
    if (!agent) {
        return;
    }
    
    agent_priv_t* priv = agent->priv;
    if (priv) {
        // Cleanup LLM provider resources (HTTP client, etc)
        if (priv->llm) {
            ac_llm_cleanup(priv->llm);
        }
        
        // Destroy arena (this frees llm, tools, messages, and all other allocations)
        if (priv->arena) {
            AC_LOG_DEBUG("Destroying agent arena");
            arena_destroy(priv->arena);
        }
        free(priv);
    }
    
    free(agent);
}
