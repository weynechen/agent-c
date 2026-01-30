/**
 * @file chat_tools.c
 * @brief ReACT Agent demo with MOC-generated tool calling
 *
 * This example demonstrates how to use MOC (Meta-Object Compiler) generated
 * tool wrappers with the ArC framework.
 *
 * Usage:
 *   ./chat_tools "What time is it?"
 *   ./chat_tools "Calculate 123 * 456"
 *   ./chat_tools "What's the weather in Beijing?"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arc.h>

/* Platform wrapper for terminal UTF-8 support and argument encoding */
#include "platform_wrap.h"

/* dotenv for loading .env file */
#include "dotenv.h"

/* MOC-generated tool definitions */
#include "demo_tools_gen.h"

/* Original tool declarations (for reference) */
#include "demo_tools.h"

/*============================================================================
 * Main
 *============================================================================*/

static void print_usage(const char *prog) {
    printf("Usage: %s <prompt>\n\n", prog);
    printf("ArC Tool Demo with MOC-generated tools\n\n");
    printf("Examples:\n");
    printf("  %s \"What time is it?\"\n", prog);
    printf("  %s \"Calculate 123 * 456\"\n", prog);
    printf("  %s \"What's the weather in Tokyo?\"\n", prog);
    printf("  %s \"Convert 100 fahrenheit to celsius\"\n", prog);
    printf("  %s \"Give me a random number between 1 and 100\"\n", prog);
    printf("\nEnvironment:\n");
    printf("  OPENAI_API_KEY    - OpenAI API key (required)\n");
    printf("  OPENAI_BASE_URL   - API base URL (optional)\n");
    printf("  OPENAI_MODEL      - Model name (default: gpt-4o-mini)\n");
    printf("\nAvailable tools:\n");
    printf("  - get_current_time\n");
    printf("  - calculator\n");
    printf("  - get_weather\n");
    printf("  - convert_temperature\n");
    printf("  - random_number\n");
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

    /* Load .env file */
    env_load(".", 0);

    /* Get API configuration */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        AC_LOG_ERROR("OPENAI_API_KEY environment variable is not set");
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) model = "gpt-4o-mini";

    printf("=== ArC Tool Demo (MOC Integration) ===\n");
    printf("Model: %s\n", model);
    if (base_url) printf("URL: %s\n", base_url);
    printf("Tools: %zu available\n\n", ALL_TOOLS_COUNT);

    /* Open session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    /*
     * Create tool registry and add MOC-generated tools.
     *
     * Use AC_TOOLS() macro to select tools by function name.
     * The macro expands to: &TOOL_func1, &TOOL_func2, ..., NULL
     */
    ac_tool_registry_t *tools = ac_tool_registry_create(session);
    if (!tools) {
        AC_LOG_ERROR("Failed to create tool registry");
        ac_session_close(session);
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

    /* Show selected tools */
    printf("Registered tools: %zu\n", ac_tool_registry_count(tools));
    printf("\n");

    /* Create agent with tool registry */
    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "ToolAgent",
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

    /* Cleanup - session closes everything */
    ac_session_close(session);

    platform_free_argv_utf8(utf8_argv, argc);
    platform_cleanup_terminal();

    return 0;
}
