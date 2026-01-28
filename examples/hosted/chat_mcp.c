/**
 * @file chat_mcp.c
 * @brief ReACT Agent demo with MCP (Model Context Protocol) integration
 *
 * This example demonstrates how to:
 * 1. Create a tool registry
 * 2. Add MOC-generated builtin tools
 * 3. Connect to an MCP server and discover tools
 * 4. Combine builtin and MCP tools in a single agent
 *
 * Usage:
 *   ./chat_mcp "List files in the current directory"
 *   ./chat_mcp "What time is it?"
 *
 * Environment:
 *   OPENAI_API_KEY    - OpenAI API key (required)
 *   OPENAI_BASE_URL   - API base URL (optional)
 *   OPENAI_MODEL      - Model name (default: gpt-4o-mini)
 *   MCP_SERVER_URL    - MCP server URL (optional, default: http://localhost:3000/mcp)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agentc.h>

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
    printf("AgentC MCP Integration Demo\n\n");
    printf("This demo shows how to combine builtin tools (from MOC) with\n");
    printf("dynamically discovered MCP tools in a single agent.\n\n");
    printf("Examples:\n");
    printf("  %s \"What time is it?\"\n", prog);
    printf("  %s \"Calculate 123 * 456\"\n", prog);
    printf("  %s \"List files in the current directory\"\n", prog);
    printf("\nEnvironment:\n");
    printf("  OPENAI_API_KEY    - OpenAI API key (required)\n");
    printf("  OPENAI_BASE_URL   - API base URL (optional)\n");
    printf("  OPENAI_MODEL      - Model name (default: gpt-4o-mini)\n");
    printf("  MCP_SERVER_URL    - MCP server URL (optional)\n");
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
        platform_free_argv_utf8(utf8_argv, argc);
        platform_cleanup_terminal();
        return 1;
    }

    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) model = "gpt-4o-mini";

    // const char *mcp_url = getenv("MCP_SERVER_URL");
    const char *mcp_url = "https://mcp.context7.com/mcp";

    printf("=== AgentC MCP Integration Demo ===\n");
    printf("Model: %s\n", model);
    if (base_url) printf("API URL: %s\n", base_url);
    if (mcp_url) printf("MCP Server: %s\n", mcp_url);
    printf("\n");

    /*========================================================================
     * Step 1: Open session
     * 
     * The session manages lifecycle of all resources: agents, registries,
     * and MCP clients. When session closes, everything is cleaned up.
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
     * 
     * The registry holds all tools that will be available to the agent.
     * It's created within the session, so it's automatically cleaned up.
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
     * 
     * Use AC_TOOLS() macro to select which MOC-generated tools to add.
     * The macro expands to: &TOOL_xxx, &TOOL_yyy, NULL
     *========================================================================*/
    
    printf("Adding builtin tools...\n");
    
    agentc_err_t err = ac_tool_registry_add_array(tools, 
        AC_TOOLS(get_current_time, calculator, get_weather, convert_temperature, random_number)
    );
    
    if (err != AGENTC_OK) {
        AC_LOG_WARN("Failed to add some builtin tools: %s", ac_strerror(err));
    }
    
    printf("  Builtin tools added: %zu\n", ac_tool_registry_count(tools));

    /*========================================================================
     * Step 4: Connect to MCP server and discover tools (optional)
     * 
     * If MCP_SERVER_URL is set, connect to the MCP server and discover
     * additional tools. These are added to the same registry.
     *========================================================================*/
    
    ac_mcp_client_t *mcp = NULL;
    
    if (mcp_url && strlen(mcp_url) > 0) {
        printf("\nConnecting to MCP server: %s\n", mcp_url);
        
        mcp = ac_mcp_create(session, &(ac_mcp_config_t){
            .server_url = mcp_url,
            .timeout_ms = 30000,
            .verify_ssl = 1
        });
        
        if (mcp) {
            err = ac_mcp_connect(mcp);
            if (err == AGENTC_OK) {
                printf("  Connected to MCP server\n");
                
                err = ac_mcp_discover_tools(mcp);
                if (err == AGENTC_OK) {
                    size_t mcp_count = ac_mcp_tool_count(mcp);
                    printf("  Discovered %zu MCP tools\n", mcp_count);
                    
                    /* Add MCP tools to registry */
                    err = ac_tool_registry_add_mcp(tools, mcp);
                    if (err == AGENTC_OK) {
                        printf("  MCP tools added to registry\n");
                    } else {
                        AC_LOG_WARN("Failed to add MCP tools: %s", ac_strerror(err));
                    }
                } else {
                    AC_LOG_WARN("Failed to discover MCP tools: %s", ac_strerror(err));
                }
            } else {
                AC_LOG_WARN("Failed to connect to MCP server: %s", ac_strerror(err));
            }
        } else {
            AC_LOG_WARN("Failed to create MCP client");
        }
    } else {
        printf("\nNo MCP server configured (set MCP_SERVER_URL to enable)\n");
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
    printf("\n");

    /*========================================================================
     * Step 6: Create agent with the tool registry
     * 
     * The agent uses the registry for tool calling. All tools (builtin + MCP)
     * are available through the same interface.
     *========================================================================*/
    
    printf("Creating agent...\n\n");
    
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
    
    printf("[User] %s\n\n", user_prompt);
    
    ac_agent_result_t *result = ac_agent_run(agent, user_prompt);
    
    if (result && result->content) {
        printf("[Assistant] %s\n\n", result->content);
    } else {
        printf("[Error] No response from agent\n\n");
    }

    /*========================================================================
     * Step 8: Cleanup
     * 
     * Just close the session - it automatically destroys:
     * - All agents
     * - All tool registries
     * - All MCP clients
     *========================================================================*/
    
    printf("Closing session...\n");
    ac_session_close(session);
    
    platform_free_argv_utf8(utf8_argv, argc);
    platform_cleanup_terminal();

    printf("Done.\n");
    return 0;
}
