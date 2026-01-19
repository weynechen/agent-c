/**
 * @file main.c
 * @brief Code-AgentC Main Entry Point
 */

#include "code_agentc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Print usage information */
static void print_usage(const char *prog_name) {
    printf("Code-AgentC - Terminal-based Code Generation AI\n\n");
    printf("Usage: %s [OPTIONS] [PROMPT]\n\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -i, --interactive       Run in interactive mode (default)\n");
    printf("  -m, --model MODEL       LLM model to use\n");
    printf("  -k, --api-key KEY       API key for LLM\n");
    printf("  -b, --api-base URL      API base URL\n");
    printf("  -p, --project DIR       Project directory (default: current)\n");
    printf("  -r, --rules DIR         Rules directory (default: .code-agentc/rules)\n");
    printf("  -s, --skills DIR        Skills directory (default: .code-agentc/skills)\n");
    printf("  --mcp URL               MCP server URL\n");
    printf("  --no-mcp                Disable MCP integration\n");
    printf("  --max-iter N            Maximum iterations (default: 10)\n");
    printf("  --temp FLOAT            Temperature (default: 0.7)\n");
    printf("  --verbose               Enable verbose logging\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -i                                    # Interactive mode\n", prog_name);
    printf("  %s \"Create a hello world program\"       # One-shot mode\n", prog_name);
    printf("  %s -m gpt-4 \"Refactor this function\"    # Use specific model\n", prog_name);
    printf("\n");
    printf("Environment Variables:\n");
    printf("  OPENAI_API_KEY          OpenAI API key\n");
    printf("  DEEPSEEK_API_KEY        DeepSeek API key\n");
    printf("  MODEL                   Default model name\n");
    printf("  MCP_SERVER_URL          MCP server URL\n");
}

/* Print version information */
static void print_version(void) {
    printf("Code-AgentC v%d.%d.%d\n",
           CODE_AGENTC_VERSION_MAJOR,
           CODE_AGENTC_VERSION_MINOR,
           CODE_AGENTC_VERSION_PATCH);
    printf("Built on AgentC framework\n");
}

/* Parse command line arguments */
static int parse_args(int argc, char **argv, code_agentc_config_t *config, 
                     int *interactive, char **prompt) {
    *interactive = 1;  /* Default to interactive */
    *prompt = NULL;
    
    /* Set defaults */
    memset(config, 0, sizeof(*config));
    config->model = getenv("MODEL") ? getenv("MODEL") : "gpt-4";
    config->api_key = getenv("OPENAI_API_KEY");
    if (!config->api_key) {
        config->api_key = getenv("DEEPSEEK_API_KEY");
    }
    config->project_dir = ".";
    config->rules_dir = ".code-agentc/rules";
    config->skills_dir = ".code-agentc/skills";
    config->mcp_server_url = getenv("MCP_SERVER_URL");
    config->enable_mcp = config->mcp_server_url ? 1 : 0;
    config->max_iterations = 10;
    config->temperature = 0.7f;
    config->verbose = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            *interactive = 1;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --model requires an argument\n");
                return -1;
            }
            config->model = argv[i];
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--api-key") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --api-key requires an argument\n");
                return -1;
            }
            config->api_key = argv[i];
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--api-base") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --api-base requires an argument\n");
                return -1;
            }
            config->api_base = argv[i];
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--project") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --project requires an argument\n");
                return -1;
            }
            config->project_dir = argv[i];
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rules") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --rules requires an argument\n");
                return -1;
            }
            config->rules_dir = argv[i];
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--skills") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --skills requires an argument\n");
                return -1;
            }
            config->skills_dir = argv[i];
        } else if (strcmp(argv[i], "--mcp") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --mcp requires an argument\n");
                return -1;
            }
            config->mcp_server_url = argv[i];
            config->enable_mcp = 1;
        } else if (strcmp(argv[i], "--no-mcp") == 0) {
            config->enable_mcp = 0;
        } else if (strcmp(argv[i], "--max-iter") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --max-iter requires an argument\n");
                return -1;
            }
            config->max_iterations = atoi(argv[i]);
        } else if (strcmp(argv[i], "--temp") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --temp requires an argument\n");
                return -1;
            }
            config->temperature = atof(argv[i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
        } else if (argv[i][0] != '-') {
            /* First non-option argument is the prompt */
            *prompt = argv[i];
            *interactive = 0;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    
    /* Validate configuration */
    if (!config->api_key) {
        fprintf(stderr, "Error: API key not provided. Use --api-key or set OPENAI_API_KEY/DEEPSEEK_API_KEY\n");
        return -1;
    }
    
    return 0;
}

int main(int argc, char **argv) {
    code_agentc_config_t config;
    int interactive;
    char *prompt;
    int ret;
    
    /* Parse arguments */
    ret = parse_args(argc, argv, &config, &interactive, &prompt);
    if (ret != 0) {
        return ret > 0 ? 0 : 1;
    }
    
    /* Create code-agentc instance */
    code_agentc_t *app = code_agentc_create(&config);
    if (!app) {
        fprintf(stderr, "Error: Failed to initialize code-agentc\n");
        return 1;
    }
    
    /* Run application */
    if (interactive) {
        printf("Starting Code-AgentC in interactive mode...\n");
        printf("Type 'exit' or 'quit' to quit, 'help' for help.\n\n");
        ret = code_agentc_run_interactive(app);
    } else {
        ret = code_agentc_run_once(app, prompt);
    }
    
    /* Cleanup */
    code_agentc_destroy(app);
    
    return ret;
}
