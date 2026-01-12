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
#ifdef _WIN32
#include <windows.h>
#endif
#include "agentc.h"
#include "dotenv.h"
#include "render/markdown/md.h"

#define MAX_INPUT_LEN 4096
#define MAX_HISTORY 20

static volatile int g_running = 1;
static md_stream_t *g_md_stream = NULL;
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
    printf("  /stream   - Toggle streaming mode\n");
    printf("  /md       - Toggle markdown rendering\n");
    printf("  /quit     - Exit\n\n");
}

/* Streaming callback - print tokens as they arrive */
static int on_stream_chunk(const char *data, size_t len, void *user_data) {
    (void)user_data;
    if (g_use_markdown && g_md_stream) {
        md_stream_feed(g_md_stream, data, len);
    } else {
        fwrite(data, 1, len, stdout);
        fflush(stdout);
    }
    return g_running ? 0 : -1;  /* Return -1 to abort if interrupted */
}

static void on_stream_done(const char *finish_reason, int total_tokens, void *user_data) {
    (void)user_data;
    if (g_use_markdown && g_md_stream) {
        md_stream_finish(g_md_stream);
    }
    printf("\n");
    if (finish_reason) {
        printf("[%s, %d tokens]\n", finish_reason, total_tokens);
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#ifdef _WIN32
    /* Set console to UTF-8 mode for proper Unicode display */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    
    /* Load environment from .env file */
    if (env_load(".", false) == 0) {
        printf("[Loaded .env file]\n");
    } else {
        printf("[No .env file found, using environment variables]\n");
    }
    
    /* Get API key from environment */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        fprintf(stderr, "Error: OPENAI_API_KEY not set\n");
        fprintf(stderr, "Create a .env file with: OPENAI_API_KEY=sk-xxx\n");
        return 1;
    }
    
    /* Optional: custom base URL and model */
    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    
    /* Initialize AgentC */
    agentc_err_t err = agentc_init();
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to initialize AgentC: %s\n", agentc_strerror(err));
        return 1;
    }
    
    /* Create LLM client */
    agentc_llm_config_t config = {
        .api_key = api_key,
        .base_url = base_url,
        .model = model,
        .timeout_ms = 120000,  /* 2 minutes for slow models */
    };
    
    agentc_llm_client_t *llm = NULL;
    err = agentc_llm_create(&config, &llm);
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to create LLM client: %s\n", agentc_strerror(err));
        agentc_cleanup();
        return 1;
    }
    
    printf("\n=== AgentC Chat Demo ===\n");
    printf("Model: %s\n", model ? model : "gpt-3.5-turbo");
    printf("Endpoint: %s\n", base_url ? base_url : "https://api.openai.com/v1");
    printf("Markdown: %s (use /md to toggle)\n", g_use_markdown ? "ON" : "OFF");
    printf("Type /help for commands, /quit to exit\n\n");
    
    /* Conversation history */
    agentc_message_t *history = NULL;
    int use_streaming = 1;
    
    /* Add system message */
    agentc_message_append(&history, 
        agentc_message_create(AGENTC_ROLE_SYSTEM, 
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
                agentc_message_free(history);
                history = NULL;
                agentc_message_append(&history,
                    agentc_message_create(AGENTC_ROLE_SYSTEM,
                        "You are a helpful assistant. Be concise and clear."));
                printf("[History cleared]\n");
                continue;
            } else if (strcmp(input, "/model") == 0) {
                printf("[Model: %s]\n", model ? model : "gpt-3.5-turbo");
                continue;
            } else if (strcmp(input, "/stream") == 0) {
                use_streaming = !use_streaming;
                printf("[Streaming: %s]\n", use_streaming ? "ON" : "OFF");
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
        agentc_message_append(&history, 
            agentc_message_create(AGENTC_ROLE_USER, input));
        
        /* Build request */
        agentc_chat_request_t req = {
            .messages = history,
            .temperature = 0.7f,
        };
        
        printf("Assistant: ");
        fflush(stdout);
        
        if (use_streaming) {
            /* Streaming mode - create markdown stream if enabled */
            if (g_use_markdown) {
                g_md_stream = md_stream_new();
            }
            
            err = agentc_llm_chat_stream(llm, &req, 
                on_stream_chunk, on_stream_done, NULL);
            
            if (err != AGENTC_OK) {
                printf("\n[Error: %s]\n", agentc_strerror(err));
            }
            
            /* Clean up markdown stream */
            if (g_md_stream) {
                md_stream_free(g_md_stream);
                g_md_stream = NULL;
            }
            /* Note: In streaming mode, we don't easily get the full response
               to add to history. A real implementation would accumulate it. */
        } else {
            /* Blocking mode */
            agentc_chat_response_t resp = {0};
            err = agentc_llm_chat(llm, &req, &resp);
            
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
                agentc_message_append(&history,
                    agentc_message_create(AGENTC_ROLE_ASSISTANT, resp.content));
            } else {
                printf("[Error: %s]\n", agentc_strerror(err));
            }
            
            agentc_chat_response_free(&resp);
        }
        
        printf("\n");
    }
    
    /* Cleanup */
    agentc_message_free(history);
    agentc_llm_destroy(llm);
    agentc_cleanup();
    
    printf("Goodbye!\n");
    return 0;
}
