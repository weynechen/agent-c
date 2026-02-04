/**
 * @file chat_kimi2.5.c
 * @brief Kimi K2.5 Streaming chat demo with thinking (reasoning_content) support using Agent API
 *
 * Demonstrates:
 * - Agent API with streaming mode for OpenAI-compatible providers
 * - Kimi K2.5 thinking mode (reasoning_content field)
 * - Automatic message history management
 *
 * Usage:
 *   1. Create .env file with MOONSHOT_API_KEY=sk-xxx
 *   2. Run ./chat_kimi2.5
 *
 * Environment variables:
 *   MOONSHOT_API_KEY   - Required: Moonshot/Kimi API key
 *   MOONSHOT_MODEL     - Optional: Model name (default: kimi-k2-thinking)
 *   MOONSHOT_BASE_URL  - Optional: API base URL (default: https://api.moonshot.cn/v1)
 *
 * API Reference:
 *   https://platform.moonshot.cn/docs/guide/use-kimi-k2-thinking-model
 *
 * Note: Kimi K2.5 uses `reasoning_content` field (parallel to `content`) for
 * thinking content in streaming responses. The reasoning_content always appears
 * before content in the stream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arc.h>
#include <arc/env.h>

#define MAX_INPUT_LEN 4096
#define DEFAULT_MODEL "kimi-k2-thinking"
#define DEFAULT_BASE_URL "https://api.moonshot.cn/v1"

static volatile int g_running = 1;
static int g_show_thinking = 1;

/* ANSI color codes */
#define COLOR_RESET    "\033[0m"
#define COLOR_THINKING "\033[36m"   /* Cyan for thinking/reasoning */
#define COLOR_TEXT     "\033[0m"    /* Default for text */
#define COLOR_INFO     "\033[33m"   /* Yellow for info */
#define COLOR_PROMPT   "\033[32m"   /* Green for prompt */
#define COLOR_ERROR    "\033[31m"   /* Red for errors */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

static void print_usage(void) {
    printf("\nCommands:\n");
    printf("  /help      - Show this help\n");
    printf("  /show      - Toggle showing thinking/reasoning content\n");
    printf("  /quit      - Exit\n\n");
}

/**
 * @brief Stream callback - called for each streaming event
 *
 * Handles Kimi K2.5's reasoning_content (displayed as thinking) and
 * regular content streaming.
 */
static int stream_callback(const ac_stream_event_t* event, void* user_data) {
    (void)user_data;
    
    switch (event->type) {
        case AC_STREAM_MESSAGE_START:
            /* Message started */
            break;
            
        case AC_STREAM_CONTENT_BLOCK_START:
            /* Block started - Kimi uses REASONING for thinking */
            if (event->block_type == AC_BLOCK_REASONING && g_show_thinking) {
                printf("%s[thinking] ", COLOR_THINKING);
                fflush(stdout);
            } else if (event->block_type == AC_BLOCK_THINKING && g_show_thinking) {
                /* Fallback for standard thinking blocks */
                printf("%s[thinking] ", COLOR_THINKING);
                fflush(stdout);
            } else if (event->block_type == AC_BLOCK_TEXT) {
                /* Newline to separate from thinking, then reset color */
                if (event->block_index > 0) {
                    printf("\n");  /* Separate from previous block (thinking) */
                }
                printf("%s", COLOR_RESET);
                fflush(stdout);
            } else if (event->block_type == AC_BLOCK_TOOL_USE) {
                /* Newline to separate from previous blocks */
                if (event->block_index > 0) {
                    printf("\n");
                }
                printf("%s[tool: %s] ", COLOR_INFO, event->tool_name ? event->tool_name : "?");
                fflush(stdout);
            }
            break;
            
        case AC_STREAM_DELTA:
            if (event->delta && event->delta_len > 0) {
                if (event->delta_type == AC_DELTA_REASONING) {
                    /* Kimi's reasoning_content - use thinking color */
                    if (g_show_thinking) {
                        printf("%s%.*s", COLOR_THINKING, (int)event->delta_len, event->delta);
                        fflush(stdout);
                    }
                } else if (event->delta_type == AC_DELTA_THINKING) {
                    /* Standard thinking delta (fallback) */
                    if (g_show_thinking) {
                        printf("%s%.*s", COLOR_THINKING, (int)event->delta_len, event->delta);
                        fflush(stdout);
                    }
                } else if (event->delta_type == AC_DELTA_TEXT) {
                    /* Text content - use reset/default color */
                    printf("%s%.*s", COLOR_RESET, (int)event->delta_len, event->delta);
                    fflush(stdout);
                }
            }
            break;
            
        case AC_STREAM_CONTENT_BLOCK_STOP:
            /* Always reset color at block end */
            printf("%s", COLOR_RESET);
            if ((event->block_type == AC_BLOCK_REASONING || 
                 event->block_type == AC_BLOCK_THINKING) && g_show_thinking) {
                printf("\n");  /* Newline after thinking block */
            } else if (event->block_type == AC_BLOCK_TOOL_USE) {
                printf("\n");
            }
            fflush(stdout);
            break;
            
        case AC_STREAM_MESSAGE_DELTA:
            /* Message level update (stop_reason, usage) */
            break;
            
        case AC_STREAM_MESSAGE_STOP:
            printf("%s\n", COLOR_RESET);
            break;
            
        case AC_STREAM_ERROR:
            printf("\n%s[Error: %s]%s\n", COLOR_ERROR, 
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
    const char *api_key = ac_env_require("MOONSHOT_API_KEY");
    if (!api_key) {
        fprintf(stderr, "Error: MOONSHOT_API_KEY environment variable is required.\n");
        fprintf(stderr, "Get your API key from: https://platform.moonshot.cn/console/api-keys\n\n");
        fprintf(stderr, "Create a .env file with:\n");
        fprintf(stderr, "  MOONSHOT_API_KEY=sk-your-api-key\n");
        return 1;
    }

    /* Get optional settings */
    const char *model = ac_env_get("MOONSHOT_MODEL", DEFAULT_MODEL);
    const char *base_url = ac_env_get("MOONSHOT_BASE_URL", DEFAULT_BASE_URL);

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
        .name = "KimiBot",
        .instructions = "You are Kimi, a helpful AI assistant. Be concise and clear in your responses.",
        .llm = {
            .provider = "openai",           /* Use OpenAI-compatible provider */
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
            .max_tokens = 8192,
            .timeout_ms = 120000,           /* 2 minutes for streaming */
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

    printf("\n=== Kimi K2.5 Streaming Chat Demo (Agent API) ===\n");
    printf("Model: %s\n", model);
    printf("API Base: %s\n", base_url);
    printf("Thinking display: %s\n", g_show_thinking ? "ON" : "OFF");
    printf("\nNote: Kimi K2.5 uses reasoning_content for thinking in stream mode.\n");
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
                printf("[Thinking display: %s]\n", g_show_thinking ? "ON" : "OFF");
                continue;
            } else {
                printf("[Unknown command: %s]\n", input);
                continue;
            }
        }

        printf("%sKimi: %s", COLOR_PROMPT, COLOR_RESET);
        fflush(stdout);

        /* Run agent - streaming happens via callback */
        ac_agent_result_t *result = ac_agent_run(agent, input);

        if (!result) {
            printf("%s[Error: Agent run failed]%s\n", COLOR_ERROR, COLOR_RESET);
        }

        printf("\n");
    }

    /* Cleanup - session handles everything */
    ac_session_close(session);

    printf("Goodbye!\n");
    return 0;
}
