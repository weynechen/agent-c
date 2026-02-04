/**
 * @file chat_stream_tools.c
 * @brief Streaming chat demo with tool calling support
 *
 * Demonstrates:
 * - Streaming LLM responses with real-time token output
 * - Tool calling with streaming (tool_use content blocks)
 * - Extended thinking mode
 * - Direct LLM API usage with tool execution loop
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
#include <arc/llm.h>
#include <arc/arena.h>
#include <arc/env.h>
#include <arc/tool.h>

/* MOC-generated tool definitions */
#include "arc/log.h"
#include "demo_tools_gen.h"
#include "demo_tools.h"

#define MAX_INPUT_LEN 4096
#define DEFAULT_MODEL "claude-sonnet-4-5-20250514"
#define MAX_TOOL_ITERATIONS 10

static volatile int g_running = 1;
static int g_thinking_mode = 0;
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
    printf("  /thinking  - Toggle thinking mode\n");
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
                printf("%s", COLOR_RESET);
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

/**
 * @brief Check if response requires tool execution
 */
static int has_tool_use(const ac_chat_response_t* response) {
    if (!response) return 0;
    for (ac_content_block_t* b = response->blocks; b; b = b->next) {
        if (b->type == AC_BLOCK_TOOL_USE) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Execute tools and create tool result message
 */
static ac_message_t* execute_tools(arena_t* arena, ac_tool_registry_t* tools, 
                                   const ac_chat_response_t* response) {
    if (!response || !response->blocks) return NULL;
    
    /* Create tool result message */
    ac_message_t* result_msg = (ac_message_t*)arena_alloc(arena, sizeof(ac_message_t));
    if (!result_msg) return NULL;
    memset(result_msg, 0, sizeof(ac_message_t));
    result_msg->role = AC_ROLE_USER;  /* Anthropic uses "user" role for tool results */
    
    ac_content_block_t* last_block = NULL;
    
    for (ac_content_block_t* b = response->blocks; b; b = b->next) {
        if (b->type != AC_BLOCK_TOOL_USE) continue;
        if (!b->id || !b->name) continue;
        
        printf("%s[executing: %s]%s ", COLOR_TOOL, b->name, COLOR_RESET);
        fflush(stdout);
        
        /* Execute the tool */
        ac_tool_ctx_t tool_ctx = {0};  /* Empty context for now */
        char* tool_result = ac_tool_registry_call(tools, b->name, b->input, &tool_ctx);
        int is_error = 0;
        
        if (tool_result) {
            printf("%s[done]%s\n", COLOR_TOOL, COLOR_RESET);
        } else {
            printf("%s[error]%s\n", COLOR_INFO, COLOR_RESET);
            tool_result = ARC_STRDUP("{\"error\": \"Tool execution failed\"}");
            is_error = 1;
        }
        
        /* Create tool_result content block */
        ac_content_block_t* result_block = (ac_content_block_t*)arena_alloc(arena, sizeof(ac_content_block_t));
        if (!result_block) {
            if (tool_result) ARC_FREE(tool_result);
            continue;
        }
        memset(result_block, 0, sizeof(ac_content_block_t));
        result_block->type = AC_BLOCK_TOOL_RESULT;
        result_block->id = arena_strdup(arena, b->id);
        result_block->text = arena_strdup(arena, tool_result ? tool_result : "{}");
        result_block->is_error = is_error;
        
        if (tool_result) ARC_FREE(tool_result);
        
        /* Append to result message */
        if (!result_msg->blocks) {
            result_msg->blocks = result_block;
        } else {
            last_block->next = result_block;
        }
        last_block = result_block;
    }
    
    return result_msg->blocks ? result_msg : NULL;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Load environment */
    ac_env_load_verbose(NULL);
    // ac_log_set_level(AC_LOG_LEVEL_DEBUG);
    /* Get API key */
    const char *api_key = ac_env_require("ANTHROPIC_API_KEY");
    if (!api_key) {
        ac_env_print_help("chat_stream_tools");
        return 1;
    }

    /* Get optional settings */
    const char *model = ac_env_get("ANTHROPIC_MODEL", DEFAULT_MODEL);
    const char *base_url = ac_env_get("ANTHROPIC_BASE_URL", NULL);
    g_thinking_mode = atoi(ac_env_get("ENABLE_THINKING", "0"));
    int thinking_budget = atoi(ac_env_get("THINKING_BUDGET", "10000"));

    /* Setup signal handler */
    signal(SIGINT, signal_handler);

    /* Create arena for memory management */
    arena_t *arena = arena_create(4 * 1024 * 1024);  /* 4MB arena */
    if (!arena) {
        fprintf(stderr, "Failed to create arena\n");
        return 1;
    }

    /* Create tool registry */
    /* Note: We create a temporary session just for the tool registry */
    ac_session_t *session = ac_session_open();
    if (!session) {
        fprintf(stderr, "Failed to open session\n");
        arena_destroy(arena);
        return 1;
    }

    ac_tool_registry_t *tools = ac_tool_registry_create(session);
    if (!tools) {
        fprintf(stderr, "Failed to create tool registry\n");
        ac_session_close(session);
        arena_destroy(arena);
        return 1;
    }

    /* Add tools */
    arc_err_t err = ac_tool_registry_add_array(tools,
        AC_TOOLS(get_current_time, calculator, get_weather, convert_temperature, random_number)
    );
    if (err != ARC_OK) {
        fprintf(stderr, "Warning: Failed to add some tools\n");
    }

    /* Get tools JSON schema */
    char* tools_json = ac_tool_registry_schema(tools);

    /* Create LLM with streaming support */
    ac_llm_params_t llm_params = {
        .provider = "anthropic",
        .model = model,
        .api_key = api_key,
        .api_base = base_url,
        .instructions = 
            "You are a helpful assistant with access to tools.\n"
            "Use the available tools to help answer user questions.\n"
            "Always use tools when they can provide accurate information.\n"
            "Be concise and clear in your responses.",
        .max_tokens = 4096,
        .timeout_ms = 120000,
        .thinking = {
            .enabled = g_thinking_mode,
            .budget_tokens = thinking_budget,
        },
        .stream = 1,
    };

    ac_llm_t *llm = ac_llm_create(arena, &llm_params);
    if (!llm) {
        fprintf(stderr, "Failed to create LLM\n");
        if (tools_json) ARC_FREE(tools_json);
        ac_session_close(session);
        arena_destroy(arena);
        return 1;
    }

    printf("\n=== ArC Streaming Chat + Tools Demo ===\n");
    printf("Model: %s\n", model);
    printf("Provider: anthropic\n");
    printf("Tools: %zu available\n", ac_tool_registry_count(tools));
    printf("Thinking mode: %s\n", g_thinking_mode ? "ON" : "OFF");
    printf("Type /help for commands, /quit to exit\n\n");

    char input[MAX_INPUT_LEN];
    ac_message_t *messages = NULL;

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
            } else if (strcmp(input, "/thinking") == 0) {
                g_thinking_mode = !g_thinking_mode;
                llm_params.thinking.enabled = g_thinking_mode;
                ac_llm_update_params(llm, &llm_params);
                printf("[Thinking mode: %s]\n", g_thinking_mode ? "ON" : "OFF");
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

        /* Add user message */
        ac_message_t *user_msg = ac_message_create(arena, AC_ROLE_USER, input);
        ac_message_append(&messages, user_msg);

        printf("%sAssistant: %s", COLOR_PROMPT, COLOR_RESET);
        fflush(stdout);

        /* Tool execution loop */
        int iteration = 0;
        while (iteration < MAX_TOOL_ITERATIONS) {
            iteration++;
            
            /* Call LLM with streaming */
            ac_chat_response_t response = {0};
            err = ac_llm_chat_stream(llm, messages, tools_json, stream_callback, NULL, &response);

            if (err != ARC_OK) {
                printf("[Error: %s]\n", ac_strerror(err));
                ac_chat_response_free(&response);
                break;
            }

            /* Check if we need to execute tools */
            if (has_tool_use(&response)) {
                /* Add assistant response to history */
                ac_message_t *assistant_msg = ac_message_from_response(arena, &response);
                if (assistant_msg) {
                    ac_message_append(&messages, assistant_msg);
                }
                
                /* Execute tools and create result message */
                ac_message_t *tool_result_msg = execute_tools(arena, tools, &response);
                if (tool_result_msg) {
                    ac_message_append(&messages, tool_result_msg);
                }
                
                ac_chat_response_free(&response);
                
                /* Continue the loop to get final response */
                printf("%s", COLOR_TEXT);
                continue;
            }
            
            /* No tool use - add final response to history */
            ac_message_t *assistant_msg = ac_message_from_response(arena, &response);
            if (assistant_msg) {
                ac_message_append(&messages, assistant_msg);
            }
            
            /* Show usage if available */
            if (response.output_tokens > 0) {
                printf("\n%s[tokens: %d, iterations: %d]%s", 
                       COLOR_INFO, response.output_tokens, iteration, COLOR_RESET);
            }
            
            ac_chat_response_free(&response);
            break;
        }
        
        if (iteration >= MAX_TOOL_ITERATIONS) {
            printf("\n%s[Max tool iterations reached]%s", COLOR_INFO, COLOR_RESET);
        }

        printf("\n\n");
    }

    /* Cleanup */
    if (tools_json) ARC_FREE(tools_json);
    ac_llm_cleanup(llm);
    ac_session_close(session);
    arena_destroy(arena);

    printf("Goodbye!\n");
    return 0;
}
