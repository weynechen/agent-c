/**
 * @file code_agentc.c
 * @brief Code-AgentC Core Implementation
 */

#include "code_agentc.h"
#include "tools.h"
#include <agentc/rules.h>     /* From ac_hosted */
#include <agentc/skills.h>    /* From ac_hosted */
#include <agentc/mcp.h>        /* From ac_hosted */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Internal Structure
 *============================================================================*/

struct code_agentc {
    /* Configuration */
    code_agentc_config_t config;
    
    /* AgentC Components */
    ac_llm_t *llm;
    ac_tools_t *tools;
    ac_memory_t *memory;
    ac_agent_t *agent;
    
    /* Code-AgentC Components (from ac_hosted) */
    ac_rules_t *rules;
    ac_skills_t *skills;
    ac_mcp_client_t *mcp_client;
    
    /* Runtime State */
    int initialized;
};

/*============================================================================
 * Internal Functions
 *============================================================================*/

static int initialize_llm(code_agentc_t *app) {
    /* Build system prompt with rules */
    const char *base_prompt = 
        "You are Code-AgentC, an expert code generation AI assistant.\n"
        "You help users write, refactor, and understand code.\n"
        "Follow these guidelines:\n";
    
    char *system_prompt = ac_rules_build_prompt(app->rules, base_prompt);
    if (!system_prompt) {
        fprintf(stderr, "Warning: Failed to build system prompt, using base prompt\n");
        system_prompt = strdup(base_prompt);
    }
    
    /* Create LLM client */
    app->llm = ac_llm_create(&(ac_llm_params_t){
        .model = app->config.model,
        .api_key = app->config.api_key,
        .api_base = app->config.api_base,
        .instructions = system_prompt,
        .temperature = app->config.temperature,
        .max_tokens = 4096,
        .timeout_ms = 60000,
    });
    
    free(system_prompt);
    
    if (!app->llm) {
        fprintf(stderr, "Error: Failed to create LLM client\n");
        return -1;
    }
    
    return 0;
}

static int initialize_tools(code_agentc_t *app) {
    /* Create tool registry */
    app->tools = ac_tools_create();
    if (!app->tools) {
        fprintf(stderr, "Error: Failed to create tool registry\n");
        return -1;
    }
    
    /* Register built-in tools */
    if (code_agentc_register_all_tools(app->tools) != AGENTC_OK) {
        fprintf(stderr, "Error: Failed to register built-in tools\n");
        return -1;
    }
    
    /* Validate skills' tools */
    if (ac_skills_validate_tools(app->skills, app->tools) != AGENTC_OK) {
        fprintf(stderr, "Warning: Some skills' tools not found\n");
    }
    
    /* Register MCP tools if enabled */
    if (app->config.enable_mcp && app->mcp_client) {
        if (ac_mcp_register_tools(app->mcp_client, app->tools) != AGENTC_OK) {
            fprintf(stderr, "Warning: Failed to register MCP tools\n");
        } else {
            size_t mcp_tool_count = ac_mcp_tool_count(app->mcp_client);
            if (app->config.verbose) {
                printf("Registered %zu MCP tools\n", mcp_tool_count);
            }
        }
    }
    
    if (app->config.verbose) {
        printf("Total tools registered: %zu\n", ac_tool_count(app->tools));
    }
    
    return 0;
}

static int initialize_agent(code_agentc_t *app) {
    /* Create memory manager */
    app->memory = ac_memory_create(&(ac_memory_config_t){
        .session_id = "code-agentc-session",
        .max_messages = 100,
        .max_tokens = 0,  /* No token limit */
    });
    
    if (!app->memory) {
        fprintf(stderr, "Error: Failed to create memory manager\n");
        return -1;
    }
    
    /* Create agent */
    app->agent = ac_agent_create(&(ac_agent_params_t){
        .name = "code-agentc",
        .llm = app->llm,
        .tools = app->tools,
        .memory = app->memory,
        .max_iterations = app->config.max_iterations,
        .timeout_ms = 0,  /* No timeout */
    });
    
    if (!app->agent) {
        fprintf(stderr, "Error: Failed to create agent\n");
        return -1;
    }
    
    return 0;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

code_agentc_t *code_agentc_create(const code_agentc_config_t *config) {
    if (!config) {
        return NULL;
    }
    
    code_agentc_t *app = calloc(1, sizeof(code_agentc_t));
    if (!app) {
        return NULL;
    }
    
    /* Copy configuration */
    memcpy(&app->config, config, sizeof(code_agentc_config_t));
    
    /* Initialize rules (from ac_hosted) */
    app->rules = ac_rules_create();
    if (!app->rules) {
        fprintf(stderr, "Error: Failed to create rules manager\n");
        goto error;
    }
    
    if (ac_rules_load_dir(app->rules, config->rules_dir) != AGENTC_OK) {
        fprintf(stderr, "Warning: Failed to load rules from %s\n", config->rules_dir);
    }
    
    if (config->verbose) {
        printf("Loaded %zu rules\n", ac_rules_count(app->rules));
    }
    
    /* Initialize skills (from ac_hosted) */
    app->skills = ac_skills_create();
    if (!app->skills) {
        fprintf(stderr, "Error: Failed to create skills manager\n");
        goto error;
    }
    
    if (ac_skills_load_dir(app->skills, config->skills_dir) != AGENTC_OK) {
        fprintf(stderr, "Warning: Failed to load skills from %s\n", config->skills_dir);
    }
    
    /* Initialize MCP client if enabled (from ac_hosted) */
    if (config->enable_mcp && config->mcp_server_url) {
        app->mcp_client = ac_mcp_create(&(ac_mcp_config_t){
            .server_url = config->mcp_server_url,
            .transport = "http",
            .timeout_ms = 30000,
        });
        
        if (app->mcp_client) {
            if (ac_mcp_connect(app->mcp_client) != AGENTC_OK) {
                fprintf(stderr, "Warning: Failed to connect to MCP server\n");
                ac_mcp_destroy(app->mcp_client);
                app->mcp_client = NULL;
            } else if (config->verbose) {
                printf("Connected to MCP server: %s\n", config->mcp_server_url);
            }
        }
    }
    
    /* Initialize LLM */
    if (initialize_llm(app) != 0) {
        goto error;
    }
    
    /* Initialize tools */
    if (initialize_tools(app) != 0) {
        goto error;
    }
    
    /* Initialize agent */
    if (initialize_agent(app) != 0) {
        goto error;
    }
    
    app->initialized = 1;
    return app;
    
error:
    code_agentc_destroy(app);
    return NULL;
}

int code_agentc_run_interactive(code_agentc_t *app) {
    if (!app || !app->initialized) {
        return -1;
    }
    
    printf("Interactive mode not yet implemented.\n");
    printf("This will provide a REPL interface for code generation.\n");
    
    /* TODO: Implement interactive REPL
     * - Read user input
     * - Run agent with streaming
     * - Display results with markdown rendering
     * - Handle commands (/help, /exit, /clear, etc.)
     */
    
    return 0;
}

int code_agentc_run_once(code_agentc_t *app, const char *prompt) {
    if (!app || !app->initialized || !prompt) {
        return -1;
    }
    
    printf("Running with prompt: %s\n", prompt);
    
    /* Run agent synchronously */
    ac_agent_result_t result;
    agentc_err_t err = ac_agent_run_sync(app->agent, prompt, &result);
    
    if (err != AGENTC_OK) {
        fprintf(stderr, "Error: Agent execution failed with code %d\n", err);
        return -1;
    }
    
    /* Display result */
    printf("\n=== Result ===\n");
    printf("%s\n", result.response ? result.response : "(no response)");
    printf("\nIterations: %d, Tokens: %d\n", result.iterations, result.total_tokens);
    
    /* Cleanup */
    ac_agent_result_free(&result);
    
    return result.status == AC_RUN_SUCCESS ? 0 : -1;
}

void code_agentc_destroy(code_agentc_t *app) {
    if (!app) {
        return;
    }
    
    /* Destroy in reverse order */
    if (app->agent) {
        ac_agent_destroy(app->agent);
    }
    
    if (app->memory) {
        ac_memory_destroy(app->memory);
    }
    
    if (app->tools) {
        ac_tools_destroy(app->tools);
    }
    
    if (app->llm) {
        ac_llm_destroy(app->llm);
    }
    
    if (app->mcp_client) {
        ac_mcp_disconnect(app->mcp_client);
        ac_mcp_destroy(app->mcp_client);
    }
    
    if (app->skills) {
        ac_skills_destroy(app->skills);
    }
    
    if (app->rules) {
        ac_rules_destroy(app->rules);
    }
    
    free(app);
}
