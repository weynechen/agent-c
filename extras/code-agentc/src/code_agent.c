/**
 * @file code_agent.c
 * @brief Code Agent Core Implementation
 */

#include "code_agent.h"
#include "code_tools.h"
#include "code_tools_enhanced.h"
#include "prompt_loader.h"
#include <agentc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Include MOC-generated tool definitions */
#include "code_tools_gen.h"

/*============================================================================
 * Internal State
 *============================================================================*/

struct code_agent {
    code_agent_config_t config;
    ac_session_t *session;
    char *rendered_system_prompt;
    prompt_context_t prompt_ctx;  /**< Context for prompt placeholder substitution */
};

/*============================================================================
 * Default Configuration
 *============================================================================*/

code_agent_config_t code_agent_default_config(void) {
    code_agent_config_t config = {
        .provider = "openai",
        .model = NULL,  /* Will use provider default */
        .api_key = NULL,
        .api_base = NULL,
        .temperature = 0.7f,
        .timeout_ms = 120000,
        .workspace = NULL,  /* Will default to cwd */
        .max_iterations = 10,
        .enable_tools = 1,
        .safe_mode = 1,
        .enable_sandbox = 1,
        .sandbox_allow_network = 1,  /* Must allow network for LLM API calls */
        .system_prompt = "anthropic",  /* Default system prompt */
        .verbose = 0,
        .quiet = 0,
        .json_output = 0,
    };
    return config;
}

/*============================================================================
 * Provider Helpers
 *============================================================================*/

static const char *get_provider_name(const char *provider) {
    if (!provider) return "openai";
    
    if (strcmp(provider, "anthropic") == 0) return "anthropic";
    if (strcmp(provider, "claude") == 0) return "anthropic";
    if (strcmp(provider, "deepseek") == 0) return "openai";
    
    return "openai";
}

static const char *get_default_model(const char *provider) {
    if (!provider) return "gpt-4o-mini";
    
    if (strcmp(provider, "anthropic") == 0 || strcmp(provider, "claude") == 0) {
        return "claude-sonnet-4-20250514";
    }
    if (strcmp(provider, "deepseek") == 0) {
        return "deepseek-chat";
    }
    
    return "gpt-4o-mini";
}

/*============================================================================
 * Create/Destroy
 *============================================================================*/

code_agent_t *code_agent_create(const code_agent_config_t *config) {
    if (!config || !config->api_key) {
        AC_LOG_ERROR("Invalid configuration or missing API key");
        return NULL;
    }
    
    /* Allocate instance */
    code_agent_t *agent = calloc(1, sizeof(code_agent_t));
    if (!agent) {
        return NULL;
    }
    
    /* Copy config */
    memcpy(&agent->config, config, sizeof(code_agent_config_t));
    
    /* Set default workspace if not provided */
    if (!agent->config.workspace) {
        static char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            agent->config.workspace = cwd;
        } else {
            agent->config.workspace = ".";
        }
    }
    
    /* Configure tools */
    code_tools_set_workspace(agent->config.workspace);
    code_tools_set_safe_mode(agent->config.safe_mode);
    
    /* Initialize prompt context for placeholder substitution */
    prompt_context_init(&agent->prompt_ctx, agent->config.workspace);
    agent->prompt_ctx.safe_mode = agent->config.safe_mode;
    agent->prompt_ctx.sandbox_enabled = agent->config.enable_sandbox;
    
    /* Render system prompt with full context */
    const char *prompt_name = agent->config.system_prompt ? 
                              agent->config.system_prompt : "anthropic";
    agent->rendered_system_prompt = prompt_render_system_ctx(prompt_name, 
                                                              &agent->prompt_ctx);
    
    if (!agent->rendered_system_prompt) {
        AC_LOG_WARN("System prompt '%s' not found, using default", prompt_name);
        agent->rendered_system_prompt = strdup(
            "You are an AI coding assistant. "
            "Help users with software engineering tasks. "
            "Use the provided tools for file operations and command execution."
        );
    }
    
    /* Open session */
    agent->session = ac_session_open();
    if (!agent->session) {
        AC_LOG_ERROR("Failed to open session");
        free(agent->rendered_system_prompt);
        free(agent);
        return NULL;
    }
    
    return agent;
}

void code_agent_destroy(code_agent_t *agent) {
    if (!agent) return;
    
    if (agent->session) {
        ac_session_close(agent->session);
    }
    
    if (agent->rendered_system_prompt) {
        free(agent->rendered_system_prompt);
    }
    
    free(agent);
}

/*============================================================================
 * Run Once Mode
 *============================================================================*/

int code_agent_run_once(code_agent_t *agent, const char *task) {
    if (!agent || !task) return -1;
    
    const char *provider = get_provider_name(agent->config.provider);
    const char *model = agent->config.model ? 
                        agent->config.model : 
                        get_default_model(agent->config.provider);
    
    if (!agent->config.quiet) {
        printf("[Task] %s\n\n", task);
    }
    
    /* Create tool registry with enhanced descriptions */
    ac_tool_registry_t *tools = NULL;
    if (agent->config.enable_tools) {
        tools = ac_tool_registry_create(agent->session);
        if (tools) {
            /* Use enhanced registration with prompt-based descriptions */
            int registered = code_tools_register_enhanced(tools, &agent->prompt_ctx);
            if (!agent->config.quiet && registered > 0) {
                printf("Registered %d tools with enhanced descriptions\n", registered);
            }
        }
    }
    
    /* Build agent configuration */
    ac_agent_params_t params = {
        .name = "CodeAgent",
        .instructions = agent->rendered_system_prompt,
        .llm = {
            .provider = provider,
            .model = model,
            .api_key = agent->config.api_key,
            .api_base = agent->config.api_base,
            .temperature = agent->config.temperature,
            .timeout_ms = agent->config.timeout_ms,
        },
        .tools = tools,
        .max_iterations = agent->config.max_iterations,
    };
    
    /* Create and run agent */
    ac_agent_t *ac_agent = ac_agent_create(agent->session, &params);
    if (!ac_agent) {
        AC_LOG_ERROR("Failed to create agent");
        return -1;
    }
    
    ac_agent_result_t *result = ac_agent_run(ac_agent, task);
    
    if (!result || !result->content) {
        AC_LOG_ERROR("Agent run failed");
        return -1;
    }
    
    /* Output result */
    if (agent->config.json_output) {
        printf("{\"status\":\"success\",\"response\":\"");
        for (const char *p = result->content; *p; p++) {
            if (*p == '"') printf("\\\"");
            else if (*p == '\\') printf("\\\\");
            else if (*p == '\n') printf("\\n");
            else if (*p == '\r') printf("\\r");
            else if (*p == '\t') printf("\\t");
            else printf("%c", *p);
        }
        printf("\"}\n");
    } else {
        if (!agent->config.quiet) {
            printf("\n[Assistant]\n");
        }
        printf("%s\n", result->content);
    }
    
    return 0;
}

/*============================================================================
 * Interactive Mode
 *============================================================================*/

int code_agent_run_interactive(code_agent_t *agent) {
    if (!agent) return -1;
    
    const char *provider = get_provider_name(agent->config.provider);
    const char *model = agent->config.model ? 
                        agent->config.model : 
                        get_default_model(agent->config.provider);
    
    char input[8192];
    
    if (!agent->config.quiet) {
        printf("Code Agent Interactive Mode\n");
        printf("Model: %s | Provider: %s\n", model, provider);
        printf("Workspace: %s\n", agent->config.workspace);
        printf("Type 'exit' or 'quit' to exit, 'help' for commands.\n\n");
    }
    
    /* Create tool registry with enhanced descriptions */
    ac_tool_registry_t *tools = NULL;
    if (agent->config.enable_tools) {
        tools = ac_tool_registry_create(agent->session);
        if (tools) {
            /* Use enhanced registration with prompt-based descriptions */
            int registered = code_tools_register_enhanced(tools, &agent->prompt_ctx);
            if (!agent->config.quiet && registered > 0) {
                printf("Tools: %d registered with enhanced prompts\n\n", registered);
            }
        }
    }
    
    /* Build agent configuration */
    ac_agent_params_t params = {
        .name = "CodeAgent",
        .instructions = agent->rendered_system_prompt,
        .llm = {
            .provider = provider,
            .model = model,
            .api_key = agent->config.api_key,
            .api_base = agent->config.api_base,
            .temperature = agent->config.temperature,
            .timeout_ms = agent->config.timeout_ms,
        },
        .tools = tools,
        .max_iterations = agent->config.max_iterations,
    };
    
    /* Create agent for session */
    ac_agent_t *ac_agent = ac_agent_create(agent->session, &params);
    if (!ac_agent) {
        AC_LOG_ERROR("Failed to create agent");
        return -1;
    }
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        /* Remove trailing newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
            len--;
        }
        
        if (len == 0) continue;
        
        /* Handle commands */
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            if (!agent->config.quiet) {
                printf("Goodbye!\n");
            }
            break;
        }
        
        if (strcmp(input, "help") == 0) {
            printf("\nCommands:\n");
            printf("  exit, quit     Exit interactive mode\n");
            printf("  help           Show this help\n");
            printf("  /prompts       List available system prompts\n");
            printf("  /tools         List available tools\n");
            printf("\nAvailable Tools:\n");
            printf("  bash           Execute shell commands\n");
            printf("  read_file      Read file contents\n");
            printf("  write_file     Write/create files\n");
            printf("  edit_file      Edit files (string replacement)\n");
            printf("  ls             List directory contents\n");
            printf("  grep           Search file contents\n");
            printf("  glob_files     Find files by pattern\n");
            printf("\n");
            continue;
        }
        
        if (strcmp(input, "/prompts") == 0) {
            printf("\nSystem Prompts:\n");
            int count = prompt_system_count();
            for (int i = 0; i < count; i++) {
                const char *name = prompt_system_name(i);
                printf("  - %s%s\n", name, 
                       strcmp(name, agent->config.system_prompt) == 0 ? " (active)" : "");
            }
            printf("\n");
            continue;
        }
        
        if (strcmp(input, "/tools") == 0) {
            printf("\nTool Prompts:\n");
            int count = prompt_tool_count();
            for (int i = 0; i < count; i++) {
                printf("  - %s\n", prompt_tool_name(i));
            }
            printf("\n");
            continue;
        }
        
        /* Run task */
        ac_agent_result_t *result = ac_agent_run(ac_agent, input);
        
        if (!result || !result->content) {
            printf("[Error] Agent run failed\n\n");
            continue;
        }
        
        if (!agent->config.quiet) {
            printf("\n[Assistant]\n");
        }
        printf("%s\n\n", result->content);
    }
    
    return 0;
}
