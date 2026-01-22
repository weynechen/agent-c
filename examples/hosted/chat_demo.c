/**
 * @file chat_demo.c
 * @brief Terminal chatbot demo using AgentC
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./chat_demo
 *
 * Or with custom endpoint in .env:
 *   OPENAI_API_KEY=xxx
 *   OPENAI_BASE_URL=https://api.deepseek.com/v1
 *   OPENAI_MODEL=deepseek-chat
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "agentc/session.h"
#include "agentc/agent.h"
#include "agentc/log.h"
#include "dotenv.h"
#include "markdown/md.h"
#include "platform_wrap.h"

#define MAX_INPUT_LEN 4096

static volatile int g_running = 1;
static int g_use_markdown = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

static void print_usage(void) {
    printf("\nCommands:\n");
    printf("  /help     - Show this help\n");
    printf("  /clear    - Clear conversation history (create new agent)\n");
    printf("  /model    - Show current model\n");
    printf("  /md       - Toggle markdown rendering\n");
    printf("  /quit     - Exit\n\n");
}

/**
 * @brief Create or recreate agent with current configuration
 */
static ac_agent_t* create_agent(ac_session_t* session, const char* model, 
                                 const char* api_key, const char* base_url) {
    ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "ChatBot",
        .instructions = "You are a helpful assistant. Be concise and clear.",
        .llm_params = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
            .instructions = NULL,  /* Already set in agent instructions */
        },
        .tools_name = NULL,
        .max_iterations = 10
    });
    
    return agent;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Initialize platform-specific terminal settings */
    platform_init_terminal(NULL);
    
    /* Load environment from .env file */
    if (env_load(".", false) == 0) {
        printf("[Loaded .env file]\n");
    } else {
        printf("[No .env file found, using environment variables]\n");
    }
    
    /* Get API key from environment */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        AC_LOG_ERROR("Error: OPENAI_API_KEY not set\n");
        AC_LOG_ERROR("Create a .env file with: OPENAI_API_KEY=sk-xxx\n");
        return 1;
    }
    
    /* Optional: custom base URL and model */
    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) {
        model = "gpt-3.5-turbo";
    }
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    
    /* Open session */
    ac_session_t* session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session\n");
        platform_cleanup_terminal();
        return 1;
    }
    
    /* Create agent */
    ac_agent_t* agent = create_agent(session, model, api_key, base_url);
    if (!agent) {
        AC_LOG_ERROR("Failed to create agent\n");
        ac_session_close(session);
        platform_cleanup_terminal();
        return 1;
    }
    
    printf("\n=== AgentC Chat Demo ===\n");
    printf("Model: %s\n", model);
    printf("Endpoint: %s\n", base_url ? base_url : "https://api.openai.com/v1");
    printf("Markdown: %s (use /md to toggle)\n", g_use_markdown ? "ON" : "OFF");
    printf("Type /help for commands, /quit to exit\n\n");
    
    char input[MAX_INPUT_LEN];
    
    while (g_running) {
        printf("You: ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        /* Remove trailing newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[--len] = '\0';
        }
        
        /* Skip empty input */
        if (len == 0) {
            continue;
        }
        
        /* Handle commands */
        if (input[0] == '/') {
            if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
                break;
            } else if (strcmp(input, "/help") == 0) {
                print_usage();
                continue;
            } else if (strcmp(input, "/clear") == 0) {
                /* Destroy old agent and create new one to clear history */
                ac_agent_destroy(agent);
                agent = create_agent(session, model, api_key, base_url);
                if (!agent) {
                    AC_LOG_ERROR("Failed to recreate agent\n");
                    break;
                }
                printf("[History cleared - new agent created]\n");
                continue;
            } else if (strcmp(input, "/model") == 0) {
                printf("[Model: %s]\n", model);
                continue;
            } else if (strcmp(input, "/md") == 0) {
                g_use_markdown = !g_use_markdown;
                printf("[Markdown rendering: %s]\n", g_use_markdown ? "ON" : "OFF");
                continue;
            } else {
                printf("[Unknown command: %s]\n", input);
                continue;
            }
        }
        
        printf("Assistant: ");
        fflush(stdout);
        
        /* Run agent synchronously */
        ac_agent_result_t* result = ac_agent_run_sync(agent, input);
        
        if (result && result->content) {
            if (g_use_markdown) {
                md_render(result->content);
            } else {
                printf("%s\n", result->content);
            }
        } else {
            printf("[No response from agent]\n");
        }
        
        printf("\n");
    }
    
    /* Cleanup - session automatically destroys all agents */
    ac_session_close(session);
    platform_cleanup_terminal();
    
    printf("Goodbye!\n");
    return 0;
}
