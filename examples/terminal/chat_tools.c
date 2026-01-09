/**
 * @file chat_tools.c
 * @brief ReACT Agent demo with tool calling
 *
 * Demonstrates how to use the AgentC ReACT loop with custom tools.
 *
 * Usage:
 *   ./chat_tools "What time is it?"
 *   ./chat_tools "Calculate 123 * 456"
 *   ./chat_tools "What's the weather in Beijing?"
 */

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
 * Agent Hooks (for observability)
 *============================================================================*/

static int on_start(const char *user_input, void *user_data) {
    (void)user_data;
    printf("\n\033[1;34m[User]\033[0m %s\n\n", user_input);
    return 0;
}

static int on_content(const char *content, size_t len, int is_complete, void *user_data) {
    (void)user_data;

    if (len > 0) {
        /* Print assistant response in green */
        printf("\033[1;32m%.*s\033[0m", (int)len, content);
        fflush(stdout);
    }

    if (is_complete) {
        printf("\n");
    }

    return 0;
}

static int on_tool_call(const agentc_tool_call_t *calls, void *user_data) {
    (void)user_data;

    printf("\033[1;33m[Tool Calls]\033[0m\n");
    for (const agentc_tool_call_t *c = calls; c; c = c->next) {
        printf("  • \033[1;36m%s\033[0m(%s)\n", c->name, c->arguments ? c->arguments : "{}");
    }
    printf("\n");

    return 0;
}

static int on_tool_result(const agentc_tool_result_t *results, void *user_data) {
    (void)user_data;

    printf("\033[1;33m[Tool Results]\033[0m\n");
    for (const agentc_tool_result_t *r = results; r; r = r->next) {
        if (r->is_error) {
            printf("  ✗ \033[1;31m%s\033[0m\n", r->output);
        } else {
            printf("  ✓ \033[0;37m%s\033[0m\n", r->output);
        }
    }
    printf("\n");

    return 0;
}

static void on_complete(const agentc_run_result_t *result, void *user_data) {
    (void)user_data;

    printf("\n\033[1;35m[Stats]\033[0m iterations=%d, tokens=%d, status=%d\n",
        result->iterations, result->total_tokens, result->status);
}

static void on_error(agentc_err_t error, const char *message, void *user_data) {
    (void)user_data;
    fprintf(stderr, "\033[1;31m[Error]\033[0m %s (code=%d)\n", message, error);
}

/*============================================================================
 * Tool Registration
 *============================================================================*/

static agentc_tool_registry_t *create_tools(void) {
    agentc_tool_registry_t *registry = agentc_tool_registry_create();
    if (!registry) return NULL;

    /* Tool 1: get_current_time */
    {
        agentc_tool_t tool = {
            .name = "get_current_time",
            .description = "Get the current date and time",
            .parameters = NULL,  /* No parameters */
            .handler = tool_get_current_time,
        };
        agentc_tool_register(registry, &tool);
    }

    /* Tool 2: calculator */
    {
        agentc_param_t *params = NULL;

        agentc_param_t *op = agentc_param_create("operation", AGENTC_PARAM_STRING,
            "The operation to perform: add, subtract, multiply, divide, power, mod", 1);
        op->enum_values = strdup("add,subtract,multiply,divide,power,mod");
        agentc_param_append(&params, op);

        agentc_param_t *a = agentc_param_create("a", AGENTC_PARAM_NUMBER,
            "First operand", 1);
        agentc_param_append(&params, a);

        agentc_param_t *b = agentc_param_create("b", AGENTC_PARAM_NUMBER,
            "Second operand", 1);
        agentc_param_append(&params, b);

        agentc_tool_t tool = {
            .name = "calculator",
            .description = "Perform basic arithmetic calculations",
            .parameters = params,
            .handler = tool_calculator,
        };
        agentc_tool_register(registry, &tool);
        agentc_param_free(params);
    }

    /* Tool 3: get_weather */
    {
        agentc_param_t *params = agentc_param_create("location", AGENTC_PARAM_STRING,
            "The city or location to get weather for", 1);

        agentc_tool_t tool = {
            .name = "get_weather",
            .description = "Get the current weather for a location",
            .parameters = params,
            .handler = tool_get_weather,
        };
        agentc_tool_register(registry, &tool);
        agentc_param_free(params);
    }

    return registry;
}

/*============================================================================
 * Main
 *============================================================================*/

static void print_usage(const char *prog) {
    printf("Usage: %s <prompt>\n\n", prog);
    printf("Examples:\n");
    printf("  %s \"What time is it?\"\n", prog);
    printf("  %s \"Calculate 123 * 456\"\n", prog);
    printf("  %s \"What's the weather in Tokyo?\"\n", prog);
    printf("  %s \"What's 2^10 and what time is it now?\"\n", prog);
    printf("\nEnvironment:\n");
    printf("  OPENAI_API_KEY    - OpenAI API key (required)\n");
    printf("  OPENAI_BASE_URL   - API base URL (optional)\n");
    printf("  OPENAI_MODEL      - Model name (default: gpt-4o-mini)\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Load .env file */
    env_load(".", 0);

    /* Get API key */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        fprintf(stderr, "Error: OPENAI_API_KEY environment variable is not set\n");
        return 1;
    }

    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) model = "gpt-4o-mini";

    /* Initialize AgentC */
    agentc_err_t err = agentc_init();
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to initialize AgentC: %s\n", agentc_strerror(err));
        return 1;
    }

    /* Create LLM client */
    agentc_llm_config_t llm_config = {
        .api_key = api_key,
        .base_url = base_url,
        .model = model,
        .timeout_ms = 60000,
    };

    agentc_llm_client_t *llm = NULL;
    err = agentc_llm_create(&llm_config, &llm);
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to create LLM client: %s\n", agentc_strerror(err));
        agentc_cleanup();
        return 1;
    }

    printf("\033[1;37mAgentC Tool Demo\033[0m\n");
    printf("Model: %s\n", model);
    if (base_url) printf("URL: %s\n", base_url);

    /* Create tools */
    agentc_tool_registry_t *tools = create_tools();
    if (!tools) {
        fprintf(stderr, "Failed to create tool registry\n");
        agentc_llm_destroy(llm);
        agentc_cleanup();
        return 1;
    }

    printf("Tools: %zu registered\n", agentc_tool_count(tools));

    /* Create agent */
    agentc_agent_config_t agent_config = {
        .llm = llm,
        .tools = tools,
        .name = "ToolAgent",
        .instructions =
            "You are a helpful assistant with access to tools.\n"
            "Use the available tools to help answer user questions.\n"
            "Always use tools when they can provide accurate information.\n"
            "For calculations, use the calculator tool.\n"
            "For time queries, use get_current_time.\n"
            "For weather queries, use get_weather.",
        .max_iterations = 5,
        .temperature = 0.7f,
        .tool_choice = "auto",
        .parallel_tool_calls = 1,
        .stream = 0,  /* Non-streaming for clearer output */
        .hooks = {
            .on_start = on_start,
            .on_content = on_content,
            .on_tool_call = on_tool_call,
            .on_tool_result = on_tool_result,
            .on_complete = on_complete,
            .on_error = on_error,
        },
    };

    agentc_agent_t *agent = NULL;
    err = agentc_agent_create(&agent_config, &agent);
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to create agent: %s\n", agentc_strerror(err));
        agentc_tool_registry_destroy(tools);
        agentc_llm_destroy(llm);
        agentc_cleanup();
        return 1;
    }

    /* Run agent with user input */
    const char *user_input = argv[1];
    agentc_run_result_t result = {0};

    err = agentc_agent_run(agent, user_input, &result);
    if (err != AGENTC_OK) {
        fprintf(stderr, "Agent run failed: %s\n", agentc_strerror(err));
    }

    /* Show final output if not already shown via hooks */
    if (result.status == AGENTC_RUN_SUCCESS && result.final_output) {
        printf("\n\033[1;32m[Assistant]\033[0m\n%s\n", result.final_output);
    } else if (result.status == AGENTC_RUN_MAX_ITERATIONS) {
        printf("\n\033[1;33m[Warning]\033[0m Hit max iterations limit\n");
    }

    /* Cleanup */
    agentc_run_result_free(&result);
    agentc_agent_destroy(agent);
    agentc_tool_registry_destroy(tools);
    agentc_llm_destroy(llm);
    agentc_cleanup();

    return (err == AGENTC_OK && result.status == AGENTC_RUN_SUCCESS) ? 0 : 1;
}
