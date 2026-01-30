/**
 * @file chat_mcp.c
 * @brief ReACT Agent demo with MCP (Model Context Protocol) integration
 *
 * This example demonstrates how to:
 * 1. Create a tool registry
 * 2. Add MOC-generated builtin tools
 * 3. Load MCP server configuration from .mcp.json
 * 4. Connect to multiple MCP servers and discover tools
 * 5. Combine builtin and MCP tools in a single agent
 *
 * Usage:
 *   ./chat_mcp "What time is it?"
 *   ./chat_mcp "Query fastapi documentation"
 *
 * Configuration:
 *   .env         - LLM API keys (OPENAI_API_KEY, etc.)
 *   .mcp.json    - MCP server configuration
 *
 * Example .mcp.json:
 *   {
 *     "servers": [
 *       {"name": "context7", "url": "https://mcp.context7.com/mcp"},
 *       {"name": "local", "url": "http://localhost:3001/mcp", "enabled": false}
 *     ]
 *   }
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
 * Usage
 *============================================================================*/

static void print_usage(const char *prog) {
    printf("Usage: %s <prompt>\n\n", prog);
    printf("ArC MCP Integration Demo\n\n");
    printf("This demo shows how to combine builtin tools (from MOC) with\n");
    printf("dynamically discovered MCP tools in a single agent.\n\n");
    printf("Examples:\n");
    printf("  %s \"What time is it?\"\n", prog);
    printf("  %s \"Calculate 123 * 456\"\n", prog);
    printf("  %s \"Query fastapi documentation using context7\"\n", prog);
    printf("\nConfiguration files:\n");
    printf("  .env        - LLM API keys (OPENAI_API_KEY, OPENAI_MODEL, etc.)\n");
    printf("  .mcp.json   - MCP server configuration\n");
    printf("\nExample .mcp.json:\n");
    printf("  {\n");
    printf("    \"servers\": [\n");
    printf("      {\"name\": \"context7\", \"url\": \"https://mcp.context7.com/mcp\"},\n");
    printf("      {\"name\": \"local\", \"url\": \"http://localhost:3001/mcp\", \"enabled\": false}\n");
    printf("    ]\n");
    printf("  }\n");
}

/*============================================================================
 * Main
 *============================================================================*/

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
        AC_LOG_ERROR("Create a .env file with: OPENAI_API_KEY=your-key");
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) model = "gpt-4o-mini";

    printf("=== ArC MCP Integration Demo ===\n");
    printf("Model: %s\n", model);
    if (base_url) printf("API URL: %s\n", base_url);
    printf("\n");

    /*========================================================================
     * Step 1: Open session
     *========================================================================*/

    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    /*========================================================================
     * Step 2: Create tool registry
     *========================================================================*/

    ac_tool_registry_t *tools = ac_tool_registry_create(session);
    if (!tools) {
        AC_LOG_ERROR("Failed to create tool registry");
        ac_session_close(session);
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    /*========================================================================
     * Step 3: Add builtin tools (from MOC)
     *========================================================================*/

    printf("Adding builtin tools...\n");

    arc_err_t err = ac_tool_registry_add_array(tools,
        AC_TOOLS(get_current_time, calculator, get_weather, convert_temperature, random_number)
    );

    if (err != ARC_OK) {
        AC_LOG_WARN("Failed to add some builtin tools: %s", ac_strerror(err));
    }

    printf("  Builtin tools: %zu\n", ac_tool_registry_count(tools));

    /*========================================================================
     * Step 4: Load MCP configuration and connect to servers
     *
     * Configuration is loaded from .mcp.json file.
     * This is a dotfile to protect API keys.
     *========================================================================*/

    printf("\nLoading MCP configuration from .mcp.json...\n");

    ac_mcp_servers_config_t *mcp_config = ac_mcp_load_config(NULL);

    if (mcp_config) {
        size_t total = ac_mcp_config_server_count(mcp_config);
        size_t enabled = ac_mcp_config_enabled_count(mcp_config);
        printf("  Found %zu servers (%zu enabled)\n", total, enabled);

        if (enabled > 0) {
            printf("\nConnecting to MCP servers...\n");
            size_t connected = ac_mcp_connect_all(session, mcp_config, tools);
            printf("  Connected: %zu/%zu\n", connected, enabled);
        }

        ac_mcp_config_free(mcp_config);
    } else {
        printf("  No .mcp.json found (MCP disabled)\n");
        printf("  Create .mcp.json to enable MCP servers\n");
    }

    /*========================================================================
     * Step 5: Show all available tools
     *========================================================================*/

    size_t total_tools = ac_tool_registry_count(tools);
    printf("\nTotal tools available: %zu\n", total_tools);

    /* Generate and show schema (for debugging) */
    char *schema = ac_tool_registry_schema(tools);
    if (schema) {
        printf("Tools schema size: %zu bytes\n", strlen(schema));
        free(schema);
    }

    /*========================================================================
     * Step 6: Create agent with the tool registry
     *========================================================================*/

    printf("\nCreating agent...\n");

    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "MCPAgent",
        .instructions =
            "You are a helpful assistant with access to various tools.\n"
            "Use the available tools to help answer user questions.\n"
            "Always prefer using tools when they can provide accurate information.\n"
            "If a tool fails, explain the error and try an alternative approach.\n",
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

    /*========================================================================
     * Step 7: Run the agent
     *========================================================================*/

    printf("\n[User] %s\n\n", user_prompt);

    ac_agent_result_t *result = ac_agent_run(agent, user_prompt);

    if (result && result->content) {
        printf("[Assistant] %s\n\n", result->content);
    } else {
        printf("[Error] No response from agent\n\n");
    }

    /*========================================================================
     * Step 8: Cleanup
     *========================================================================*/

    printf("Closing session...\n");
    ac_session_close(session);

    platform_free_argv_utf8(utf8_argv, argc);
    platform_cleanup_terminal();

    printf("Done.\n");
    return 0;
}
