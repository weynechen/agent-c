/**
 * @file chat_tools.c
 * @brief ReACT Agent demo with tool calling (Updated for new API)
 *
 * Demonstrates how to use the AgentC ReACT loop with custom tools.
 *
 * Usage:
 *   ./chat_tools "What time is it?"
 *   ./chat_tools "Calculate 123 * 456"
 *   ./chat_tools "What's the weather in Beijing?"
 */

#include "agentc/platform.h"
#include <agentc.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* dotenv for loading .env file */
#include "dotenv.h"

/*============================================================================
 * Tool Implementations
 *============================================================================*/

/**
 * Tool: get_current_time
 * Returns the current date and time
 */
static agentc_err_t tool_get_current_time(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)args;
    (void)user_data;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    char result[128];
    snprintf(result, sizeof(result),
        "{\"current_time\": \"%s\", \"timezone\": \"local\"}",
        time_buf);

    *output = strdup(result);
    return AGENTC_OK;
}

/**
 * Tool: calculator
 * Performs basic arithmetic operations
 */
static agentc_err_t tool_calculator(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)user_data;

    cJSON *op = cJSON_GetObjectItem(args, "operation");
    cJSON *a = cJSON_GetObjectItem(args, "a");
    cJSON *b = cJSON_GetObjectItem(args, "b");

    if (!op || !cJSON_IsString(op) || !a || !cJSON_IsNumber(a) || !b || !cJSON_IsNumber(b)) {
        *output = strdup("{\"error\": \"Invalid arguments. Need operation, a, b\"}");
        return AGENTC_OK;
    }

    double num_a = a->valuedouble;
    double num_b = b->valuedouble;
    double result = 0;
    const char *operation = op->valuestring;

    if (strcmp(operation, "add") == 0 || strcmp(operation, "+") == 0) {
        result = num_a + num_b;
    } else if (strcmp(operation, "subtract") == 0 || strcmp(operation, "-") == 0) {
        result = num_a - num_b;
    } else if (strcmp(operation, "multiply") == 0 || strcmp(operation, "*") == 0) {
        result = num_a * num_b;
    } else if (strcmp(operation, "divide") == 0 || strcmp(operation, "/") == 0) {
        if (num_b == 0) {
            *output = strdup("{\"error\": \"Division by zero\"}");
            return AGENTC_OK;
        }
        result = num_a / num_b;
    } else if (strcmp(operation, "power") == 0 || strcmp(operation, "^") == 0) {
        result = pow(num_a, num_b);
    } else if (strcmp(operation, "mod") == 0 || strcmp(operation, "%") == 0) {
        result = fmod(num_a, num_b);
    } else {
        *output = strdup("{\"error\": \"Unknown operation. Use: add, subtract, multiply, divide, power, mod\"}");
        return AGENTC_OK;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"operation\": \"%s\", \"a\": %g, \"b\": %g, \"result\": %g}",
        operation, num_a, num_b, result);

    *output = strdup(buf);
    return AGENTC_OK;
}

/**
 * Tool: get_weather (mock)
 * Returns simulated weather data
 */
static agentc_err_t tool_get_weather(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)user_data;

    cJSON *location = cJSON_GetObjectItem(args, "location");
    if (!location || !cJSON_IsString(location)) {
        *output = strdup("{\"error\": \"location is required\"}");
        return AGENTC_OK;
    }

    const char *city = location->valuestring;

    /* Mock weather data based on city name hash */
    unsigned int hash = 0;
    for (const char *p = city; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }

    int temp = 15 + (hash % 20);  /* 15-35°C */
    int humidity = 40 + (hash % 40);  /* 40-80% */
    const char *conditions[] = {"sunny", "cloudy", "rainy", "windy", "snowy"};
    const char *condition = conditions[hash % 5];

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"location\": \"%s\", \"temperature\": %d, \"unit\": \"celsius\", "
        "\"humidity\": %d, \"condition\": \"%s\"}",
        city, temp, humidity, condition);

    *output = strdup(buf);
    return AGENTC_OK;
}

/*============================================================================
 * Tool Registration
 *============================================================================*/

static ac_tools_t *create_tools(void) {
    ac_tools_t *tools = ac_tools_create();
    if (!tools) return NULL;

    /* Tool 1: get_current_time */
    {
        ac_tool_t tool = {
            .name = "get_current_time",
            .description = "Get the current date and time",
            .parameters = NULL,  /* No parameters */
            .handler = tool_get_current_time,
        };
        ac_tool_register(tools, &tool);
    }

    /* Tool 2: calculator */
    {
        ac_param_t *params = NULL;

        ac_param_t *op = ac_param_create("operation", AC_PARAM_STRING,
            "The operation to perform: add, subtract, multiply, divide, power, mod", 1);
        op->enum_values = strdup("add,subtract,multiply,divide,power,mod");
        ac_param_append(&params, op);

        ac_param_t *a = ac_param_create("a", AC_PARAM_NUMBER,
            "First operand", 1);
        ac_param_append(&params, a);

        ac_param_t *b = ac_param_create("b", AC_PARAM_NUMBER,
            "Second operand", 1);
        ac_param_append(&params, b);

        ac_tool_t tool = {
            .name = "calculator",
            .description = "Perform basic arithmetic calculations",
            .parameters = params,
            .handler = tool_calculator,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }

    /* Tool 3: get_weather */
    {
        ac_param_t *params = ac_param_create("location", AC_PARAM_STRING,
            "The city or location to get weather for", 1);

        ac_tool_t tool = {
            .name = "get_weather",
            .description = "Get the current weather for a location",
            .parameters = params,
            .handler = tool_get_weather,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }

    return tools;
}

/*============================================================================
 * Main
 *============================================================================*/

static void print_usage(const char *prog) {
    AC_LOG_INFO("Usage: %s <prompt>\n\n", prog);
    AC_LOG_INFO("Examples:\n");
    AC_LOG_INFO("  %s \"What time is it?\"\n", prog);
    AC_LOG_INFO("  %s \"Calculate 123 * 456\"\n", prog);
    AC_LOG_INFO("  %s \"What's the weather in Tokyo?\"\n", prog);
    AC_LOG_INFO("  %s \"What's 2^10 and what time is it now?\"\n", prog);
    AC_LOG_INFO("\nEnvironment:\n");
    AC_LOG_INFO("  OPENAI_API_KEY    - OpenAI API key (required)\n");
    AC_LOG_INFO("  OPENAI_BASE_URL   - API base URL (optional)\n");
    AC_LOG_INFO("  OPENAI_MODEL      - Model name (default: gpt-4o-mini)\n");
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Set console to UTF-8 mode for proper Unicode display */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

#ifdef _WIN32
    /* 
     * On Windows, argv[] uses system default encoding (usually GBK).
     * We need to use Windows API to get UTF-8 encoded arguments.
     */
    char *user_prompt = NULL;
    {
        int wargc;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv && wargc >= 2) {
            /* Convert wargv[1] (user prompt) to UTF-8 */
            int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wargv[1], -1, NULL, 0, NULL, NULL);
            if (utf8_len > 0) {
                user_prompt = (char*)malloc(utf8_len);
                if (user_prompt) {
                    WideCharToMultiByte(CP_UTF8, 0, wargv[1], -1, user_prompt, utf8_len, NULL, NULL);
                }
            }
            LocalFree(wargv);
        }
    }
    if (!user_prompt) {
        AC_LOG_ERROR( "Error: Failed to parse command line arguments\n");
        return 1;
    }
#else
    const char *user_prompt = argv[1];
#endif

    /* Load .env file */
    env_load(".", 0);

    /* Get API key */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        AC_LOG_ERROR( "Error: OPENAI_API_KEY environment variable is not set\n");
        return 1;
    }

    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) model = "gpt-4o-mini";

    /* Initialize AgentC */
    agentc_err_t err = ac_init();
    if (err != AGENTC_OK) {
        AC_LOG_ERROR( "Failed to initialize AgentC: %s\n", ac_strerror(err));
        return 1;
    }

    AC_LOG_INFO("AgentC Tool Demo (New API)\n");
    AC_LOG_INFO("Model: %s\n", model);
    if (base_url) AC_LOG_INFO("URL: %s\n", base_url);

    /* Create LLM client */
    ac_llm_t *llm = ac_llm_create(&(ac_llm_params_t){
        .model = model,
        .api_key = api_key,
        .api_base = base_url,
        .instructions = 
            "You are a helpful assistant with access to tools.\n"
            "Use the available tools to help answer user questions.\n"
            "Always use tools when they can provide accurate information.\n"
            "For calculations, use the calculator tool.\n"
            "For time queries, use get_current_time.\n"
            "For weather queries, use get_weather.",
        .temperature = 0.7f,
        .timeout_ms = 60000,
    });

    if (!llm) {
        AC_LOG_ERROR( "Failed to create LLM client\n");
        ac_cleanup();
        return 1;
    }

    /* Create tools */
    ac_tools_t *tools = create_tools();
    if (!tools) {
        AC_LOG_ERROR( "Failed to create tool registry\n");
        ac_llm_destroy(llm);
        ac_cleanup();
        return 1;
    }

    AC_LOG_INFO("Tools: %zu registered\n", ac_tool_count(tools));

    /* Create agent */
    ac_agent_t *agent = ac_agent_create(&(ac_agent_params_t){
        .name = "ToolAgent",
        .llm = llm,
        .tools = tools,
        .memory = NULL,
        .max_iterations = 5,
        .timeout_ms = 0,
    });

    if (!agent) {
        AC_LOG_ERROR( "Failed to create agent\n");
        ac_tools_destroy(tools);
        ac_llm_destroy(llm);
        ac_cleanup();
        return 1;
    }

    /* Run agent with user input */
    AC_LOG_INFO("[User]%s\n", user_prompt);
    
    ac_agent_result_t result = {0};
    err = ac_agent_run_sync(agent, user_prompt, &result);
    
    if (err != AGENTC_OK) {
        AC_LOG_ERROR( "[Error] Agent run failed: %s\n", ac_strerror(err));
    }

    /* Show final output */
    if (result.status == AC_RUN_SUCCESS && result.response) {
        AC_LOG_INFO("[Assistant]%s\n\n", result.response);
    } else if (result.status == AC_RUN_MAX_ITERATIONS) {
        AC_LOG_WARN("Hit max iterations limit\n");
        if (result.response) {
            AC_LOG_INFO("%s\n", result.response);
        }
    } else if (result.status == AC_RUN_ERROR) {
        AC_LOG_ERROR( "[Error] Run failed with error code %d\n", 
                result.error_code);
    }

    AC_LOG_INFO("iterations=%d, tokens=%d, status=%d\n",
        result.iterations, result.total_tokens, result.status);

    /* Cleanup */
    ac_agent_result_free(&result);
    ac_agent_destroy(agent);
    ac_tools_destroy(tools);
    ac_llm_destroy(llm);
    ac_cleanup();

#ifdef _WIN32
    free(user_prompt);
#endif

    return (err == AGENTC_OK && result.status == AC_RUN_SUCCESS) ? 0 : 1;
}
