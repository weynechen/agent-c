/**
 * @file main.c
 * @brief Code Agent Main Entry Point
 */

#include "code_agent.h"
#include "code_tools.h"
#include "prompt_loader.h"
#include <arc/log.h>
#include <arc/sandbox.h>
#include <arc/trace_exporters.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Environment configuration */
#include <arc/env.h>

/*============================================================================
 * Sandbox Confirmation Callback
 *============================================================================*/

static ac_sandbox_confirm_result_t sandbox_confirm_callback(
    const ac_sandbox_confirm_request_t *request,
    void *user_data
) {
    (void)user_data;

    if (!request) {
        return AC_SANDBOX_DENY;
    }

    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ CONFIRMATION REQUIRED                                           │\n");
    printf("├─────────────────────────────────────────────────────────────────┤\n");

    const char *type_str;
    switch (request->type) {
        case AC_SANDBOX_CONFIRM_COMMAND:
            type_str = "Command Execution";
            break;
        case AC_SANDBOX_CONFIRM_PATH_READ:
            type_str = "File Read";
            break;
        case AC_SANDBOX_CONFIRM_PATH_WRITE:
            type_str = "File Write";
            break;
        case AC_SANDBOX_CONFIRM_NETWORK:
            type_str = "Network Access";
            break;
        case AC_SANDBOX_CONFIRM_DANGEROUS:
            type_str = "Dangerous Operation";
            break;
        default:
            type_str = "Unknown Operation";
    }
    printf("│ Type: %-57s │\n", type_str);

    if (request->resource) {
        char resource_display[56];
        size_t len = strlen(request->resource);
        if (len > 55) {
            strncpy(resource_display, request->resource, 52);
            strcpy(resource_display + 52, "...");
        } else {
            strncpy(resource_display, request->resource, sizeof(resource_display) - 1);
            resource_display[sizeof(resource_display) - 1] = '\0';
        }
        printf("│ Resource: %-53s │\n", resource_display);
    }

    printf("├─────────────────────────────────────────────────────────────────┤\n");
    printf("│ [y] Allow  [n] Deny  [a] Allow all similar                      │\n");
    printf("└─────────────────────────────────────────────────────────────────┘\n");

    printf("Choice [y/n/a]: ");
    fflush(stdout);

    char input[16];
    if (!fgets(input, sizeof(input), stdin)) {
        return AC_SANDBOX_DENY;
    }

    char choice = input[0];
    if (choice == 'y' || choice == 'Y') {
        return AC_SANDBOX_ALLOW;
    } else if (choice == 'a' || choice == 'A') {
        return AC_SANDBOX_ALLOW_SESSION;
    } else {
        return AC_SANDBOX_DENY;
    }
}

/*============================================================================
 * Help & Version
 *============================================================================*/

static void print_usage(const char *prog) {
    printf("Code Agent - AI Coding Assistant\n\n");
    printf("Usage: %s [OPTIONS] [TASK]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -i, --interactive       Run in interactive mode\n");
    printf("\n");
    printf("LLM Options:\n");
    printf("  --model MODEL           LLM model to use\n");
    printf("  --provider PROVIDER     LLM provider (openai, anthropic, deepseek)\n");
    printf("  --api-key KEY           API key for LLM provider\n");
    printf("  --api-base URL          API base URL (optional)\n");
    printf("  --temp FLOAT            Temperature (0.0-2.0, default: 0.7)\n");
    printf("\n");
    printf("Agent Options:\n");
    printf("  --workspace PATH        Workspace directory (default: current dir)\n");
    printf("  --max-iter N            Max tool iterations (default: 10)\n");
    printf("  --system-prompt NAME    System prompt to use (default: anthropic)\n");
    printf("  --timeout MS            Request timeout in ms (default: 120000)\n");
    printf("\n");
    printf("Safety Options:\n");
    printf("  --no-sandbox            Disable sandbox protection\n");
    printf("  --no-safe-mode          Disable dangerous command blocking\n");
    printf("  --sandbox-network       Allow network access in sandbox\n");
    printf("\n");
    printf("Output Options:\n");
    printf("  --verbose               Enable verbose output\n");
    printf("  --quiet                 Quiet mode (minimal output)\n");
    printf("  --json                  JSON output format\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s \"Read main.c and explain what it does\"\n", prog);
    printf("  %s \"Fix the bug in parser.c line 42\"\n", prog);
    printf("  %s \"Add error handling to the http module\"\n", prog);
    printf("  %s -i                           # Interactive mode\n", prog);
    printf("\n");
    printf("Environment Variables:\n");
    printf("  OPENAI_API_KEY          OpenAI API key\n");
    printf("  ANTHROPIC_API_KEY       Anthropic API key\n");
    printf("  CODE_AGENT_MODEL        Default model\n");
    printf("  CODE_AGENT_PROVIDER     Default provider\n");
    printf("  CODE_AGENT_WORKSPACE    Default workspace\n");
    printf("\n");
    printf("Available System Prompts:\n");
    int count = prompt_system_count();
    for (int i = 0; i < count && i < 10; i++) {
        printf("  - %s\n", prompt_system_name(i));
    }
    if (count > 10) {
        printf("  ... and %d more\n", count - 10);
    }
}

static void print_version(void) {
    printf("Code Agent v%d.%d.%d\n",
           CODE_AGENT_VERSION_MAJOR,
           CODE_AGENT_VERSION_MINOR,
           CODE_AGENT_VERSION_PATCH);
    printf("Built on ArC framework\n");
    printf("Sandbox: %s\n", ac_sandbox_backend_name());
}

/*============================================================================
 * Argument Parsing
 *============================================================================*/

static int parse_args(int argc, char **argv,
                      code_agent_config_t *config,
                      int *interactive,
                      char **task)
{
    /* Start with defaults */
    *config = code_agent_default_config();

    /* Load multi-level config */
    ac_env_load_verbose("arc-coder");

    const char *api_key = ac_env_require("OPENAI_API_KEY");
    const char *model = ac_env_get("OPENAI_MODEL", "gpt-3.5-turbo");
    const char *base_url = ac_env_get("OPENAI_BASE_URL", NULL);
    const char *env_provider = ac_env_get("PROVIDER", "openai");

    if (!api_key) {
        ac_env_print_help("arc-coder");
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
        AC_LOG_INFO("max iterations:%d",config->max_iterations);
    } else {
        config->max_iterations = 5;
        AC_LOG_INFO("max iterations default:%d",config->max_iterations);
    }

    config->timeout_ms = 60000;
    config->enable_tools = 1;

    /* Parse safe mode from env */
    const char *safe_mode_str = ac_env_get("SAFE_MODE", "true");
    if (safe_mode_str && (strcmp(safe_mode_str, "true") == 0 || strcmp(safe_mode_str, "1") == 0)) {
        config->safe_mode = 1;
    }

    /* Parse sandbox settings from env (sandbox enabled by default) */
    config->enable_sandbox = 1;  /* Default: enabled */
    const char *sandbox_str = ac_env_get("SANDBOX_ENABLED", "true");
    if (sandbox_str && (strcmp(sandbox_str, "false") == 0 || strcmp(sandbox_str, "0") == 0)) {
        config->enable_sandbox = 0;
    }
    config->workspace = ac_env_get("SANDBOX_WORKSPACE", NULL);

    const char *sandbox_net_str = ac_env_get("SANDBOX_ALLOW_NETWORK", "true");
    if (sandbox_net_str && (strcmp(sandbox_net_str, "true") == 0 || strcmp(sandbox_net_str, "1") == 0)) {
        config->sandbox_allow_network = 1;
    }

    *interactive = 1;  /* Default to interactive mode */
    *task = NULL;

    /* Parse command line */
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
        } else if (strcmp(argv[i], "--workspace") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --workspace requires an argument\n");
                return -1;
            }
            config->workspace = argv[i];
        } else if (strcmp(argv[i], "--max-iter") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --max-iter requires an argument\n");
                return -1;
            }
            config->max_iterations = atoi(argv[i]);
        } else if (strcmp(argv[i], "--system-prompt") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --system-prompt requires an argument\n");
                return -1;
            }
            config->system_prompt = argv[i];
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --timeout requires an argument\n");
                return -1;
            }
            config->timeout_ms = atoi(argv[i]);
        } else if (strcmp(argv[i], "--no-sandbox") == 0) {
            config->enable_sandbox = 0;
        } else if (strcmp(argv[i], "--no-safe-mode") == 0) {
            config->safe_mode = 0;
        } else if (strcmp(argv[i], "--sandbox-network") == 0) {
            config->sandbox_allow_network = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            config->quiet = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            config->json_output = 1;
        } else if (argv[i][0] != '-') {
            *task = argv[i];
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            return -1;
        }
    }

    /* Validate */
    if (!config->api_key) {
        fprintf(stderr, "Error: No API key provided.\n");
        fprintf(stderr, "Set OPENAI_API_KEY or ANTHROPIC_API_KEY environment variable,\n");
        fprintf(stderr, "or use --api-key option.\n");
        return -1;
    }

    /* If no task and not interactive, default to interactive */
    if (!*task && !*interactive) {
        *interactive = 1;
    }

    return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char **argv) {
    code_agent_config_t config;
    int interactive;
    char *task;
    int ret;
    ac_sandbox_t *sandbox = NULL;

    /* Parse arguments */
    ret = parse_args(argc, argv, &config, &interactive, &task);
    if (ret != 0) {
        return ret > 0 ? 0 : 1;
    }

    /* Initialize trace exporter - save traces to ./logs directory */
    ac_trace_json_config_t trace_config = {
        .output_dir = "logs",
        .pretty_print = 1,
        .include_timestamps = 1,
        .flush_after_event = 0
    };

    if (ac_trace_json_exporter_init(&trace_config) != 0) {
        fprintf(stderr, "Warning: Failed to initialize trace exporter\n");
    } else if (!config.quiet) {
        printf("Trace: enabled (output: ./logs)\n");
    }

    /* Initialize sandbox if enabled */
    if (config.enable_sandbox) {
        char cwd[4096];
        const char *workspace = config.workspace;
        if (!workspace) {
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                workspace = cwd;
            } else {
                workspace = ".";
            }
        }

        ac_sandbox_config_t sb_config = {
            .workspace_path = workspace,
            .allow_network = config.sandbox_allow_network,
            .allow_process_exec = 1,
            .strict_mode = 0,
            .log_violations = config.verbose,
        };

        sandbox = ac_sandbox_create(&sb_config);
        if (sandbox) {
            ac_sandbox_set_confirm_callback(sandbox, sandbox_confirm_callback, NULL);
            code_tools_set_sandbox(sandbox);

            if (!config.quiet) {
                printf("Sandbox: %s (workspace: %s)\n",
                       ac_sandbox_backend_name(), workspace);
            }
        } else {
            if (!config.quiet) {
                fprintf(stderr, "Warning: Failed to create sandbox\n");
            }
        }
    }

    /* Create agent */
    code_agent_t *agent = code_agent_create(&config);
    if (!agent) {
        fprintf(stderr, "Error: Failed to create code agent\n");
        if (sandbox) ac_sandbox_destroy(sandbox);
        return 1;
    }

    /* Run */
    if (interactive) {
        ret = code_agent_run_interactive(agent);
    } else {
        ret = code_agent_run_once(agent, task);
    }

    /* Show trace file path */
    const char *trace_path = ac_trace_json_exporter_get_path();
    if (trace_path && !config.quiet) {
        printf("\nTrace saved to: %s\n", trace_path);
    }

    /* Cleanup */
    code_agent_destroy(agent);

    code_tools_set_sandbox(NULL);
    if (sandbox) {
        ac_sandbox_destroy(sandbox);
    }

    /* Cleanup trace exporter */
    ac_trace_json_exporter_cleanup();

    return ret;
}
