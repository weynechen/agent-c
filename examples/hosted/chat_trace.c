/**
 * @file chat_trace.c
 * @brief Demo for Agent observability/tracing feature
 *
 * This example demonstrates how to use the built-in JSON file exporter
 * for agent execution tracing. The trace includes:
 * - Agent start/end events
 * - ReACT iteration events
 * - LLM request/response details (messages, tools, tokens)
 * - Tool call/result events
 *
 * Trace files are saved to ./logs directory with format:
 *   {agent_name}_{YYYYMMDD_HHMMSS}.json
 *
 * Usage:
 *   ./chat_trace "What time is it?"
 *   ./chat_trace "Calculate 123 * 456"
 *
 * Environment:
 *   OPENAI_API_KEY    - OpenAI API key (required)
 *   OPENAI_BASE_URL   - API base URL (optional)
 *   OPENAI_MODEL      - Model name (default: gpt-4o-mini)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arc.h>

/* Trace exporters for hosted environments */
#include <arc/trace_exporters.h>

/* Platform wrapper for terminal UTF-8 support and argument encoding */
#include "platform_wrap.h"

/* Environment configuration */
#include <arc/env.h>

/* MOC-generated tool definitions */
#include "demo_tools_gen.h"

/* Original tool declarations (for reference) */
#include "demo_tools.h"

/*============================================================================
 * Main
 *============================================================================*/

static void print_usage(const char *prog) {
    printf("Usage: %s <prompt>\n\n", prog);
    printf("ArC Trace Demo - Observability for Agent Execution\n\n");
    printf("This demo shows how to use the built-in JSON trace exporter.\n");
    printf("Trace files are saved to ./logs directory.\n\n");
    printf("Examples:\n");
    printf("  %s \"What time is it?\"\n", prog);
    printf("  %s \"Calculate 123 * 456\"\n", prog);
    printf("  %s \"What's the weather in Tokyo?\"\n", prog);
    printf("\nEnvironment:\n");
    printf("  OPENAI_API_KEY    - OpenAI API key (required)\n");
    printf("  OPENAI_BASE_URL   - API base URL (optional)\n");
    printf("  OPENAI_MODEL      - Model name (default: gpt-4o-mini)\n");
    printf("\nTrace output:\n");
    printf("  Directory: ./logs\n");
    printf("  Format: {agent_name}_{timestamp}.json\n");
}

int main(int argc, char *argv[]) {
    /* Initialize terminal with UTF-8 support */
    platform_init_terminal(NULL);

    if (argc < 2) {
        print_usage(argv[0]);
        platform_cleanup_terminal();
        return 1;
    }

    /* Get UTF-8 encoded command line arguments */
    char **utf8_argv = platform_get_argv_utf8(argc, argv);
    const char *user_prompt = utf8_argv[1];

    /* Load multi-level config */
    ac_env_load_verbose(NULL);

    /* Get API configuration */
    const char *api_key = ac_env_require("OPENAI_API_KEY");
    if (!api_key) {
        ac_env_print_help("chat_trace");
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    const char *base_url = ac_env_get("OPENAI_BASE_URL", NULL);
    const char *model = ac_env_get("OPENAI_MODEL", "gpt-4o-mini");

    printf("=== ArC Trace Demo ===\n");
    printf("Model: %s\n", model);
    if (base_url) printf("URL: %s\n", base_url);
    printf("Tools: %zu available\n\n", ALL_TOOLS_COUNT);

    /*
     * Initialize the JSON file exporter with default settings:
     * - Output directory: ./logs
     * - Pretty-print JSON: enabled
     * - Include ISO timestamps: enabled
     *
     * This sets up the global trace handler. All agent runs will
     * automatically emit trace events to JSON files.
     */
    printf("Initializing trace exporter...\n");

    ac_trace_json_config_t trace_config = {
        .output_dir = "logs",        /* Output directory */
        .pretty_print = 1,           /* Pretty-print JSON */
        .include_timestamps = 1,     /* Include ISO timestamps */
        .flush_after_event = 0       /* Don't flush after each event (faster) */
    };

    if (ac_trace_json_exporter_init(&trace_config) != 0) {
        AC_LOG_ERROR("Failed to initialize trace exporter");
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    printf("Trace output: ./logs/{agent_name}_{timestamp}.json\n\n");

    /* Open session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        ac_trace_json_exporter_cleanup();
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    /* Create tool registry and add MOC-generated tools */
    ac_tool_registry_t *tools = ac_tool_registry_create(session);
    if (!tools) {
        AC_LOG_ERROR("Failed to create tool registry");
        ac_session_close(session);
        ac_trace_json_exporter_cleanup();
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    /* Add selected tools using AC_TOOLS macro */
    arc_err_t err = ac_tool_registry_add_array(tools,
        AC_TOOLS(get_current_time, calculator, get_weather, convert_temperature, random_number)
    );

    if (err != ARC_OK) {
        AC_LOG_WARN("Failed to add some tools: %s", ac_strerror(err));
    }

    printf("Registered tools: %zu\n\n", ac_tool_registry_count(tools));

    /* Create agent with tool registry */
    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "TraceAgent",
        .instructions =
            "You are a helpful assistant with access to tools.\n"
            "Use the available tools to help answer user questions.\n"
            "Always use tools when they can provide accurate information.\n",
        .llm = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
        },
        .tools = tools,
        .max_iterations = 10
    });

    if (!agent) {
        AC_LOG_ERROR("Failed to create agent");
        ac_session_close(session);
        ac_trace_json_exporter_cleanup();
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    /* Run agent with user input */
    printf("[User] %s\n\n", user_prompt);

    ac_agent_result_t *result = ac_agent_run(agent, user_prompt);

    if (result && result->content) {
        printf("[Assistant] %s\n\n", result->content);
    } else {
        printf("[Error] No response from agent\n\n");
    }

    /* Get the trace file path */
    const char *trace_path = ac_trace_json_exporter_get_path();
    if (trace_path) {
        printf("=== Trace saved to: %s ===\n", trace_path);
    }

    /* Cleanup - session closes everything */
    ac_session_close(session);

    /* Cleanup trace exporter */
    ac_trace_json_exporter_cleanup();

    platform_free_argv_utf8(utf8_argv, argc);
    platform_cleanup_terminal();

    return 0;
}
