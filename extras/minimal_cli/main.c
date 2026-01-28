/**
 * @file main.c
 * @brief Minimal CLI Main Entry Point
 */

#include "minimal_cli.h"
#include <agentc/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dotenv for loading .env file */
#include "dotenv.h"

/*============================================================================
 * Help & Version
 *============================================================================*/

static void print_usage(const char *prog) {
    printf("Minimal CLI - Lightweight AI Command Line Tool\n\n");
    printf("Usage: %s [OPTIONS] [PROMPT]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -i, --interactive       Run in interactive mode (default if no prompt)\n");
    printf("\n");
    printf("  --model MODEL           LLM model to use\n");
    printf("  --provider PROVIDER     LLM provider (openai, anthropic, deepseek)\n");
    printf("  --api-key KEY           API key for LLM provider\n");
    printf("  --api-base URL          API base URL (optional)\n");
    printf("  --temp FLOAT            Temperature (0.0-2.0, default: 0.7)\n");
    printf("  --max-iter N            Maximum iterations (default: 5)\n");
    printf("  --timeout MS            Request timeout in milliseconds (default: 60000)\n");
    printf("\n");
    printf("  --no-tools              Disable all tools\n");
    printf("  --no-stream             Disable streaming output\n");
    printf("  --safe-mode             Enable safe mode (confirm dangerous commands)\n");
    printf("\n");
    printf("  --verbose               Enable verbose output\n");
    printf("  --quiet                 Quiet mode (minimal output)\n");
    printf("  --json                  JSON output format\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s \"What time is it?\"\n", prog);
    printf("  %s \"Calculate 123 * 456\"\n", prog);
    printf("  %s \"List all .c files in current directory\"\n", prog);
    printf("  %s -i                                    # Interactive mode\n", prog);
    printf("  %s --provider anthropic \"Hello\"         # Use Anthropic\n", prog);
    printf("  %s --safe-mode \"Delete old logs\"        # Safe mode\n", prog);
    printf("\n");
    printf("Environment Variables:\n");
    printf("  OPENAI_API_KEY          OpenAI API key\n");
    printf("  OPENAI_BASE_URL         OpenAI API base URL\n");
    printf("  ANTHROPIC_API_KEY       Anthropic API key\n");
    printf("  DEEPSEEK_API_KEY        DeepSeek API key\n");
    printf("  DEEPSEEK_BASE_URL       DeepSeek API base URL\n");
    printf("  MODEL                   Default model name\n");
    printf("  TEMPERATURE             Default temperature\n");
    printf("  MAX_ITERATIONS          Max iterations\n");
    printf("  SAFE_MODE               Safe mode (true/false)\n");
    printf("\n");
    printf("Built-in Tools:\n");
    printf("  - shell_execute         Execute shell commands\n");
    printf("  - read_file             Read file contents\n");
    printf("  - write_file            Write file contents\n");
    printf("  - list_directory        List directory contents\n");
    printf("  - get_current_time      Get current date and time\n");
    printf("  - calculator            Perform arithmetic calculations\n");
}

static void print_version(void) {
    printf("Minimal CLI v%d.%d.%d\n",
           MINIMAL_CLI_VERSION_MAJOR,
           MINIMAL_CLI_VERSION_MINOR,
           MINIMAL_CLI_VERSION_PATCH);
    printf("Built on AgentC framework\n");
}

/*============================================================================
 * Argument Parsing
 *============================================================================*/

static int parse_args(int argc, char **argv,
                     minimal_cli_config_t *config,
                     int *interactive,
                     char **prompt)
{
    /* Set defaults */
    memset(config, 0, sizeof(*config));
    
    /* Load .env file */
    env_load(".", 0);
    
 
    const char *api_key = getenv("OPENAI_API_KEY");
    const char *model = getenv_default("OPENAI_MODEL", "gpt-3.5-turbo");
    const char *base_url = getenv_default("OPENAI_BASE_URL", NULL);
    const char *env_provider = getenv_default("PROVIDER", "openai");

    if (!api_key) {
        AC_LOG_ERROR("Error: OPENAI_API_KEY not set\n");
        return 1;
    }
   
    config->api_key = api_key;
    config->model = model; 
    config->api_base = base_url;
    config->provider = env_provider;
    
    /* Parse temperature from env */
    const char *temp_str = getenv("TEMPERATURE");
    if (temp_str) {
        config->temperature = atof(temp_str);
    } else {
        config->temperature = 0.7f;
    }
    
    /* Parse max iterations from env */
    const char *max_iter_str = getenv("MAX_ITERATIONS");
    if (max_iter_str) {
        config->max_iterations = atoi(max_iter_str);
    } else {
        config->max_iterations = 5;
    }
    
    config->timeout_ms = 60000;
    config->enable_tools = 1;
    config->enable_stream = 1;
    
    /* Parse safe mode from env */
    const char *safe_mode_str = getenv_default("SAFE_MODE", "true");
    if (safe_mode_str && (strcmp(safe_mode_str, "true") == 0 || strcmp(safe_mode_str, "1") == 0)) {
        config->safe_mode = 1;
    }
    
    *interactive = (argc == 1);  /* Default to interactive if no args */
    *prompt = NULL;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            *interactive = 1;
        } else if (strcmp(argv[i], "--model") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --model requires an argument\n");
                return -1;
            }
            config->model = argv[i];
        } else if (strcmp(argv[i], "--provider") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --provider requires an argument\n");
                return -1;
            }
            config->provider = argv[i];
        } else if (strcmp(argv[i], "--api-key") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --api-key requires an argument\n");
                return -1;
            }
            config->api_key = argv[i];
        } else if (strcmp(argv[i], "--api-base") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --api-base requires an argument\n");
                return -1;
            }
            config->api_base = argv[i];
        } else if (strcmp(argv[i], "--temp") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --temp requires an argument\n");
                return -1;
            }
            config->temperature = atof(argv[i]);
        } else if (strcmp(argv[i], "--max-iter") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --max-iter requires an argument\n");
                return -1;
            }
            config->max_iterations = atoi(argv[i]);
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --timeout requires an argument\n");
                return -1;
            }
            config->timeout_ms = atoi(argv[i]);
        } else if (strcmp(argv[i], "--no-tools") == 0) {
            config->enable_tools = 0;
        } else if (strcmp(argv[i], "--no-stream") == 0) {
            config->enable_stream = 0;
        } else if (strcmp(argv[i], "--safe-mode") == 0) {
            config->safe_mode = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            config->quiet = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            config->json_output = 1;
        } else if (argv[i][0] != '-') {
            /* First non-option argument is the prompt */
            *prompt = argv[i];
            *interactive = 0;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            return -1;
        }
    }
    
    /* Validate configuration */
    if (!config->api_key) {
        fprintf(stderr, "Error: No API key provided.\n");
        fprintf(stderr, "Set OPENAI_API_KEY, ANTHROPIC_API_KEY, or DEEPSEEK_API_KEY,\n");
        fprintf(stderr, "or use --api-key option.\n");
        return -1;
    }
    
    /* Adjust provider based on api_key if not explicitly set */
    if (!config->provider) {
        if (getenv("ANTHROPIC_API_KEY")) {
            config->provider = "anthropic";
        } else if (getenv("DEEPSEEK_API_KEY")) {
            config->provider = "deepseek";
        } else {
            config->provider = "openai";
        }
    }
    
    return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char **argv) {
    minimal_cli_config_t config;
    int interactive;
    char *prompt;
    int ret;
    
    /* Parse arguments */
    ret = parse_args(argc, argv, &config, &interactive, &prompt);
    if (ret != 0) {
        return ret > 0 ? 0 : 1;
    }
    
    /* Create CLI instance */
    minimal_cli_t *cli = minimal_cli_create(&config);
    if (!cli) {
        fprintf(stderr, "Error: Failed to initialize Minimal CLI\n");
        return 1;
    }
    
    /* Run */
    if (interactive) {
        ret = minimal_cli_run_interactive(cli);
    } else {
        ret = minimal_cli_run_once(cli, prompt);
    }
    
    /* Cleanup */
    minimal_cli_destroy(cli);
    
    return ret;
}
