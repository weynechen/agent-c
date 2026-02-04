/**
 * @file chat_stream.c
 * @brief Streaming chat demo with thinking support using Agent API
 *
 * Demonstrates:
 * - Agent API with streaming mode
 * - Extended thinking mode (Claude thinking blocks)
 * - Automatic message history management
 *
 * Usage:
 *   1. Create .env file with ANTHROPIC_API_KEY=sk-xxx
 *   2. Run ./chat_stream
 *
 * Environment variables:
 *   ANTHROPIC_API_KEY  - Required: Anthropic API key
 *   ANTHROPIC_MODEL    - Optional: Model name (default: claude-sonnet-4-5-20250514)
 *   ENABLE_THINKING    - Optional: Enable thinking mode (default: 0)
 *   THINKING_BUDGET    - Optional: Thinking token budget (default: 10000)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arc.h>
#include <arc/env.h>

#define MAX_INPUT_LEN 4096
#define DEFAULT_MODEL "claude-sonnet-4-5-20250514"

static volatile int g_running = 1;
static int g_show_thinking = 1;

/* ANSI color codes */
#define COLOR_RESET    "\033[0m"
#define COLOR_THINKING "\033[36m"   /* Cyan for thinking */
#define COLOR_TEXT     "\033[0m"    /* Default for text */
#define COLOR_INFO     "\033[33m"   /* Yellow for info */
#define COLOR_PROMPT   "\033[32m"   /* Green for prompt */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

static void print_usage(void) {
    printf("\nCommands:\n");
    printf("  /help      - Show this help\n");
    printf("  /show      - Toggle showing thinking content\n");
    printf("  /quit      - Exit\n\n");
}

/**
 * @brief Stream callback - called for each streaming event
 */
static int stream_callback(const ac_stream_event_t* event, void* user_data) {
    (void)user_data;
    
    switch (event->type) {
        case AC_STREAM_MESSAGE_START:
            /* Message started */
            break;
            
        case AC_STREAM_CONTENT_BLOCK_START:
            if (event->block_type == AC_BLOCK_THINKING && g_show_thinking) {
                printf("%s[thinking] ", COLOR_THINKING);
                fflush(stdout);
            } else if (event->block_type == AC_BLOCK_TEXT) {
                printf("%s", COLOR_TEXT);
            } else if (event->block_type == AC_BLOCK_TOOL_USE) {
                printf("%s[tool: %s] ", COLOR_INFO, event->tool_name ? event->tool_name : "?");
                fflush(stdout);
            }
            break;
            
        case AC_STREAM_DELTA:
            if (event->delta && event->delta_len > 0) {
                if (event->delta_type == AC_DELTA_THINKING) {
                    if (g_show_thinking) {
                        printf("%.*s", (int)event->delta_len, event->delta);
                        fflush(stdout);
                    }
                } else if (event->delta_type == AC_DELTA_TEXT) {
                    printf("%.*s", (int)event->delta_len, event->delta);
                    fflush(stdout);
                }
            }
            break;
            
        case AC_STREAM_CONTENT_BLOCK_STOP:
            if (event->block_type == AC_BLOCK_THINKING && g_show_thinking) {
                printf("%s\n", COLOR_RESET);
            } else if (event->block_type == AC_BLOCK_TOOL_USE) {
                printf("%s\n", COLOR_RESET);
            }
            break;
            
        case AC_STREAM_MESSAGE_DELTA:
            /* Message level update (stop_reason, usage) */
            break;
            
        case AC_STREAM_MESSAGE_STOP:
            printf("%s\n", COLOR_RESET);
            break;
            
        case AC_STREAM_ERROR:
            printf("\n%s[Error: %s]%s\n", COLOR_INFO, 
                   event->error_msg ? event->error_msg : "Unknown", COLOR_RESET);
            return -1;  /* Abort */
            
        default:
            break;
    }
    
    return 0;  /* Continue */
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Load environment */
    ac_env_load_verbose(NULL);

    /* Get API key */
    const char *api_key = ac_env_require("ANTHROPIC_API_KEY");
    if (!api_key) {
        ac_env_print_help("chat_stream");
        return 1;
    }

    /* Get optional settings */
    const char *model = ac_env_get("ANTHROPIC_MODEL", DEFAULT_MODEL);
    const char *base_url = ac_env_get("ANTHROPIC_BASE_URL", NULL);
    int thinking_mode = atoi(ac_env_get("ENABLE_THINKING", "0"));
    int thinking_budget = atoi(ac_env_get("THINKING_BUDGET", "10000"));

    /* Setup signal handler */
    signal(SIGINT, signal_handler);

    /* Create session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        fprintf(stderr, "Failed to create session\n");
        return 1;
    }

    /* Create agent with streaming enabled */
    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "StreamBot",
        .instructions = "You are a helpful assistant. Be concise and clear.",
        .llm = {
            .provider = "anthropic",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
            .max_tokens = 4096,
            .timeout_ms = 120000,  /* 2 minutes for streaming */
            .thinking = {
                .enabled = thinking_mode,
                .budget_tokens = thinking_budget,
            },
            .stream = 1,
        },
        .callbacks = {
            .on_stream = stream_callback,
            .user_data = NULL
        }
    });

    if (!agent) {
        fprintf(stderr, "Failed to create agent\n");
        ac_session_close(session);
        return 1;
    }

    printf("\n=== ArC Streaming Chat Demo (Agent API) ===\n");
    printf("Model: %s\n", model);
    printf("Provider: anthropic\n");
    printf("Thinking mode: %s\n", thinking_mode ? "ON" : "OFF");
    if (thinking_mode) {
        printf("Thinking budget: %d tokens\n", thinking_budget);
    }
    printf("Type /help for commands, /quit to exit\n\n");

    char input[MAX_INPUT_LEN];

    while (g_running) {
        printf("%sYou: %s", COLOR_PROMPT, COLOR_RESET);
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
            } else if (strcmp(input, "/show") == 0) {
                g_show_thinking = !g_show_thinking;
                printf("[Show thinking: %s]\n", g_show_thinking ? "ON" : "OFF");
                continue;
            } else {
                printf("[Unknown command: %s]\n", input);
                continue;
            }
        }

        printf("%sAssistant: %s", COLOR_PROMPT, COLOR_RESET);
        fflush(stdout);

        /* Run agent - streaming happens via callback */
        ac_agent_result_t *result = ac_agent_run(agent, input);

        if (!result) {
            printf("[Error: Agent run failed]\n");
        }

        printf("\n");
    }

    /* Cleanup - session handles everything */
    ac_session_close(session);

    printf("Goodbye!\n");
    return 0;
}
