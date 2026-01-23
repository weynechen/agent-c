/**
 * @file minimal_cli.c
 * @brief Minimal CLI Core Implementation
 */

#include "minimal_cli.h"
#include <agentc/agent.h>
#include <agentc/session.h>
#include <agentc/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Application State
 *============================================================================*/

struct minimal_cli {
    minimal_cli_config_t config;
    ac_session_t *session;
};

/*============================================================================
 * Provider Configuration
 *============================================================================*/

static const char *get_provider_name(const char *provider) {
    if (!provider) return "openai";
    
    if (strcmp(provider, "anthropic") == 0) return "anthropic";
    if (strcmp(provider, "claude") == 0) return "anthropic";
    if (strcmp(provider, "deepseek") == 0) return "openai";  /* DeepSeek uses OpenAI API */
    
    return "openai";
}

static const char *get_default_model(const char *provider) {
    if (!provider) return "gpt-4o-mini";
    
    if (strcmp(provider, "anthropic") == 0 || strcmp(provider, "claude") == 0) {
        return "claude-3-5-sonnet-20241022";
    }
    if (strcmp(provider, "deepseek") == 0) {
        return "deepseek-chat";
    }
    
    return "gpt-4o-mini";
}

/*============================================================================
 * Create/Destroy
 *============================================================================*/

minimal_cli_t *minimal_cli_create(const minimal_cli_config_t *config) {
    if (!config || !config->api_key) {
        AC_LOG_ERROR("Invalid configuration or missing API key\n");
        return NULL;
    }
    
    /* Allocate instance */
    minimal_cli_t *cli = calloc(1, sizeof(minimal_cli_t));
    if (!cli) {
        return NULL;
    }
    
    /* Copy config */
    memcpy(&cli->config, config, sizeof(minimal_cli_config_t));
    
    /* Open session */
    cli->session = ac_session_open();
    if (!cli->session) {
        AC_LOG_ERROR("Failed to open session\n");
        free(cli);
        return NULL;
    }
    
    return cli;
}

void minimal_cli_destroy(minimal_cli_t *cli) {
    if (!cli) return;
    
    if (cli->session) {
        ac_session_close(cli->session);
    }
    
    free(cli);
}

/*============================================================================
 * Run Once Mode
 *============================================================================*/

int minimal_cli_run_once(minimal_cli_t *cli, const char *prompt) {
    if (!cli || !prompt) return -1;
    
    /* Determine provider and model */
    const char *provider = get_provider_name(cli->config.provider);
    const char *model = cli->config.model ? cli->config.model : get_default_model(cli->config.provider);
    
    /* Show prompt */
    if (!cli->config.quiet) {
        AC_LOG_INFO("[User] %s\n", prompt);
    }
    
    /* Create agent for this request */
    ac_agent_t *agent = ac_agent_create(cli->session, &(ac_agent_params_t){
        .name = "MinimalCLI",
        .instructions = 
            "You are a helpful assistant. "
            "Provide clear and concise responses.",
        .llm_params = {
            .provider = provider,
            .model = model,
            .api_key = cli->config.api_key,
            .api_base = cli->config.api_base,
        },
        .tools_name = cli->config.enable_tools ? "builtin" : NULL,
        .max_iterations = cli->config.max_iterations > 0 ? cli->config.max_iterations : 5,
    });
    
    if (!agent) {
        AC_LOG_ERROR("Failed to create agent\n");
        return -1;
    }
    
    /* Run agent */
    ac_agent_result_t *result = ac_agent_run_sync(agent, prompt);
    
    if (!result || !result->content) {
        AC_LOG_ERROR("[Error] Agent run failed\n");
        return -1;
    }
    
    /* Show result */
    if (cli->config.json_output) {
        /* JSON output mode */
        printf("{\"status\":\"success\",\"response\":");
        /* Escape JSON string */
        printf("\"");
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
        /* Normal output */
        if (!cli->config.quiet) {
            printf("[Assistant] ");
        }
        printf("%s\n", result->content);
    }
    
    return 0;
}

/*============================================================================
 * Interactive Mode
 *============================================================================*/

int minimal_cli_run_interactive(minimal_cli_t *cli) {
    if (!cli) return -1;
    
    /* Determine provider and model */
    const char *provider = get_provider_name(cli->config.provider);
    const char *model = cli->config.model ? cli->config.model : get_default_model(cli->config.provider);
    
    char input[4096];
    
    if (!cli->config.quiet) {
        printf("Minimal CLI Interactive Mode\n");
        printf("Model: %s | Provider: %s\n", model, provider);
        printf("Type 'exit' or 'quit' to exit, 'help' for help.\n\n");
    }
    
    /* Create agent for interactive session */
    ac_agent_t *agent = ac_agent_create(cli->session, &(ac_agent_params_t){
        .name = "MinimalCLI",
        .instructions = 
            "You are a helpful assistant in an interactive chat. "
            "Provide clear and concise responses. "
            "Remember the conversation context.",
        .llm_params = {
            .provider = provider,
            .model = model,
            .api_key = cli->config.api_key,
            .api_base = cli->config.api_base,
        },
        .tools_name = cli->config.enable_tools ? "builtin" : NULL,
        .max_iterations = cli->config.max_iterations > 0 ? cli->config.max_iterations : 5,
    });
    
    if (!agent) {
        AC_LOG_ERROR("Failed to create agent\n");
        return -1;
    }
    
    while (1) {
        /* Prompt */
        printf("> ");
        fflush(stdout);
        
        /* Read input */
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        /* Remove trailing newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
            len--;
        }
        
        /* Skip empty input */
        if (len == 0) {
            continue;
        }
        
        /* Handle special commands */
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            if (!cli->config.quiet) {
                printf("Goodbye!\n");
            }
            break;
        }
        
        if (strcmp(input, "help") == 0) {
            printf("Commands:\n");
            printf("  exit, quit  - Exit interactive mode\n");
            printf("  help        - Show this help message\n");
            printf("\n");
            continue;
        }
        
        /* Send message to agent */
        ac_agent_result_t *result = ac_agent_run_sync(agent, input);
        
        if (!result || !result->content) {
            AC_LOG_ERROR("[Error] Agent run failed\n");
            continue;
        }
        
        /* Show result */
        if (!cli->config.quiet) {
            printf("[Assistant] ");
        }
        printf("%s\n\n", result->content);
    }
    
    return 0;
}
