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
#include "agentc.h"
#include "dotenv.h"
#include "markdown/md.h"
#include "platform_init.h"

#define MAX_INPUT_LEN 4096
#define MAX_HISTORY 20

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
    printf("  /clear    - Clear conversation history\n");
    printf("  /model    - Show current model\n");
    printf("  /md       - Toggle markdown rendering\n");
    printf("  /quit     - Exit\n\n");
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
    
    /* Initialize AgentC */
    agentc_err_t err = ac_init();
    if (err != AGENTC_OK) {
        AC_LOG_ERROR("Failed to initialize AgentC: %s\n", ac_strerror(err));
        return 1;
    }
    
    /* Create LLM client */
    ac_llm_params_t params = {
        .model = model,
        .api_key = api_key,
        .api_base = base_url,
        .temperature = 0.7f,
        .timeout_ms = 120000,  /* 2 minutes for slow models */
    };
    
    ac_llm_t *llm = ac_llm_create(&params);
    if (!llm) {
        AC_LOG_ERROR("Failed to create LLM client\n");
        ac_cleanup();
        return 1;
    }
    
    printf("\n=== AgentC Chat Demo ===\n");
    printf("Model: %s\n", model);
    printf("Endpoint: %s\n", base_url ? base_url : "https://api.openai.com/v1");
    printf("Markdown: %s (use /md to toggle)\n", g_use_markdown ? "ON" : "OFF");
    printf("Type /help for commands, /quit to exit\n\n");
    
    /* Conversation history */
    ac_message_t *history = NULL;
    
    /* Add system message */
    ac_message_append(&history, 
        ac_message_create(AC_ROLE_SYSTEM, 
            "You are a helpful assistant. Be concise and clear."));
    
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
                ac_message_free(history);
                history = NULL;
                ac_message_append(&history,
                    ac_message_create(AC_ROLE_SYSTEM,
                        "You are a helpful assistant. Be concise and clear."));
                printf("[History cleared]\n");
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
        
        /* Add user message to history */
        ac_message_append(&history, 
            ac_message_create(AC_ROLE_USER, input));
        
        printf("Assistant: ");
        fflush(stdout);
        
        /* Blocking mode */
        ac_chat_response_t resp = {0};
        err = ac_llm_chat(llm, history, NULL, &resp);
        
        if (err == AGENTC_OK && resp.content) {
            if (g_use_markdown) {
                md_render(resp.content);
            } else {
                printf("%s\n", resp.content);
            }
            printf("[%s, %d tokens]\n", 
                resp.finish_reason ? resp.finish_reason : "done",
                resp.total_tokens);
            
            /* Add assistant response to history */
            ac_message_append(&history,
                ac_message_create(AC_ROLE_ASSISTANT, resp.content));
        } else {
            printf("[Error: %s]\n", ac_strerror(err));
        }
        
        ac_chat_response_free(&resp);
        printf("\n");
    }
    
    /* Cleanup */
    ac_message_free(history);
    ac_llm_destroy(llm);
    ac_cleanup();
    platform_cleanup_terminal();
    
    printf("Goodbye!\n");
    return 0;
}
