/**
 * @file chat_stream_tools.c
 * @brief Streaming chat demo with tool calling support using Agent API
 *
 * Demonstrates:
 * - Agent API with streaming mode and tool calling
 * - Extended thinking mode
 * - Automatic ReACT loop and tool execution
 *
 * Usage:
 *   1. Create .env file with ANTHROPIC_API_KEY=xxx
 *   2. Run ./chat_stream_tools
 *
 * Environment variables:
 *   ANTHROPIC_API_KEY  - Required: Anthropic API key
 *   ANTHROPIC_MODEL    - Optional: Model name (default: claude-sonnet-4-5-20250514)
 *   ANTHROPIC_BASE_URL - Optional: API base URL
 *   ENABLE_THINKING    - Optional: Enable thinking mode (default: 0)
 *   THINKING_BUDGET    - Optional: Thinking token budget (default: 10000)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arc.h>
#include <arc/env.h>
#include <arc/tool.h>

/* MOC-generated tool definitions */
#include "demo_tools_gen.h"
#include "demo_tools.h"

#define MAX_INPUT_LEN 4096
#define DEFAULT_MODEL "claude-sonnet-4-5-20250514"
#define MAX_TOOL_ITERATIONS 10

static volatile int g_running = 1;
static int g_show_thinking = 1;

/* ANSI color codes */
#define COLOR_RESET    "\033[0m"
#define COLOR_THINKING "\033[36m"   /* Cyan for thinking */
#define COLOR_TEXT     "\033[0m"    /* Default for text */
#define COLOR_INFO     "\033[33m"   /* Yellow for info */
#define COLOR_PROMPT   "\033[32m"   /* Green for prompt */
#define COLOR_TOOL     "\033[35m"   /* Magenta for tools */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

static void print_usage(void) {
    printf("\nCommands:\n");
    printf("  /help      - Show this help\n");
    printf("  /show      - Toggle showing thinking content\n");
    printf("  /tools     - List available tools\n");
    printf("  /quit      - Exit\n\n");
}

static void print_tools(void) {
    printf("\nAvailable tools:\n");
    printf("  - get_current_time: Get current date and time\n");
    printf("  - calculator: Perform arithmetic (add, subtract, multiply, divide, power, mod)\n");
    printf("  - get_weather: Get weather for a location\n");
    printf("  - convert_temperature: Convert between Celsius and Fahrenheit\n");
    printf("  - random_number: Generate random number in range\n\n");
}

/**
 * @brief Stream callback - handles streaming events
 */
static int stream_callback(const ac_stream_event_t* event, void* user_data) {
    (void)user_data;
    
    switch (event->type) {
        case AC_STREAM_MESSAGE_START:
            break;
            
        case AC_STREAM_CONTENT_BLOCK_START:
            if (event->block_type == AC_BLOCK_THINKING && g_show_thinking) {
                printf("%s[thinking] ", COLOR_THINKING);
                fflush(stdout);
            } else if (event->block_type == AC_BLOCK_TEXT) {
                printf("%s", COLOR_TEXT);
            } else if (event->block_type == AC_BLOCK_TOOL_USE) {
                printf("%s[calling: %s] ", COLOR_TOOL, 
                       event->tool_name ? event->tool_name : "?");
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
                /* Skip input_json_delta display - too verbose */
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
            break;
            
        case AC_STREAM_MESSAGE_STOP:
            printf("%s", COLOR_RESET);
            break;
            
        case AC_STREAM_ERROR:
            printf("\n%s[Error: %s]%s\n", COLOR_INFO, 
                   event->error_msg ? event->error_msg : "Unknown", COLOR_RESET);
            return -1;
            
        default:
            break;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Load environment */
    ac_env_load_verbose(NULL);

    /* Get API key */
    const char *api_key = ac_env_require("ANTHROPIC_API_KEY");
    if (!api_key) {
        ac_env_print_help("chat_stream_tools");
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

    /* Create tool registry */
    ac_tool_registry_t *tools = ac_tool_registry_create(session);
    if (!tools) {
        fprintf(stderr, "Failed to create tool registry\n");
        ac_session_close(session);
        return 1;
    }

    /* Add tools */
    arc_err_t err = ac_tool_registry_add_array(tools,
        AC_TOOLS(get_current_time, calculator, get_weather, convert_temperature, random_number)
    );
    if (err != ARC_OK) {
        fprintf(stderr, "Warning: Failed to add some tools\n");
    }

    /* Create agent with streaming and tools */
    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "ToolBot",
        .instructions = 
            "You are a helpful assistant with access to tools.\n"
            "Use the available tools to help answer user questions.\n"
            "Always use tools when they can provide accurate information.\n"
            "Be concise and clear in your responses.",
        .llm = {
            .provider = "anthropic",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
            .max_tokens = 4096,
            .timeout_ms = 120000,
            .thinking = {
                .enabled = thinking_mode,
                .budget_tokens = thinking_budget,
            },
            .stream = 1,
        },
        .tools = tools,
        .max_iterations = MAX_TOOL_ITERATIONS,
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

    printf("\n=== ArC Streaming Chat + Tools Demo (Agent API) ===\n");
    printf("Model: %s\n", model);
    printf("Provider: anthropic\n");
    printf("Tools: %zu available\n", ac_tool_registry_count(tools));
    printf("Thinking mode: %s\n", thinking_mode ? "ON" : "OFF");
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
            } else if (strcmp(input, "/tools") == 0) {
                print_tools();
                continue;
            } else {
                printf("[Unknown command: %s]\n", input);
                continue;
            }
        }

        printf("%sAssistant: %s", COLOR_PROMPT, COLOR_RESET);
        fflush(stdout);

        /* Run agent - streaming and tool execution happen automatically */
        ac_agent_result_t *result = ac_agent_run(agent, input);

        if (!result) {
            printf("[Error: Agent run failed]\n");
        }

        printf("\n\n");
    }

    /* Cleanup - session handles everything */
    ac_session_close(session);

    printf("Goodbye!\n");
    return 0;
}
