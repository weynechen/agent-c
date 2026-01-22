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
} agent_priv_t;

/*============================================================================
 * Agent Structure
 *============================================================================*/

struct ac_agent {
    agent_priv_t* priv;
};

/*============================================================================
 * Agent Implementation
 *============================================================================*/

static ac_agent_result_t* agent_run_sync_impl(agent_priv_t* priv, const char* message) {
    if (!priv || !priv->arena || !priv->llm) {
        return NULL;
    }
    
    // Add system message if this is the first message
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
    
    // Add user message to history
    ac_message_t* user_msg = ac_message_create(priv->arena, AC_ROLE_USER, message);
    if (!user_msg) {
        AC_LOG_ERROR("Failed to create user message");
        return NULL;
    }
    ac_message_append(&priv->messages, user_msg);
    priv->message_count++;
    
    AC_LOG_DEBUG("Added user message, total messages: %zu", priv->message_count);
    
    // Call LLM with full message history
    char* response = ac_llm_chat(priv->llm, priv->messages);
    if (!response) {
        AC_LOG_ERROR("LLM chat failed");
        return NULL;
    }
    
    // Add assistant response to history
    ac_message_t* asst_msg = ac_message_create(priv->arena, AC_ROLE_ASSISTANT, response);
    if (asst_msg) {
        ac_message_append(&priv->messages, asst_msg);
        priv->message_count++;
    }
    
    // Allocate result from agent's arena
    ac_agent_result_t* result = (ac_agent_result_t*)arena_alloc(
        priv->arena, 
        sizeof(ac_agent_result_t)
    );
    
    if (!result) {
        AC_LOG_ERROR("Failed to allocate result from arena");
        return NULL;
    }
    
    result->content = response;
    
    AC_LOG_DEBUG("Agent run completed, total messages: %zu", priv->message_count);
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
    if (params->tools_name) {
        priv->tools = ac_tool_group_create(priv->arena, params->tools_name);
        // Tools are optional, so don't fail if creation fails
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
