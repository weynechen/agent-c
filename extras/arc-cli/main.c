/**
 * @file main.c
 * @brief Minimal CLI Main Entry Point
 */

#include "minimal_cli.h"
#include "builtin_tools.h"
#include <arc/log.h>
#include <arc/sandbox.h>
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

    /* Display confirmation prompt */
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│ SANDBOX CONFIRMATION REQUIRED                                   │\n");
    printf("├─────────────────────────────────────────────────────────────────┤\n");

    /* Show type */
    const char *type_str;
    switch (request->type) {
        case AC_SANDBOX_CONFIRM_COMMAND:
            type_str = "Command Execution";
            break;
        case AC_SANDBOX_CONFIRM_PATH_READ:
            type_str = "File Read (outside workspace)";
            break;
        case AC_SANDBOX_CONFIRM_PATH_WRITE:
            type_str = "File Write (outside workspace)";
            break;
        case AC_SANDBOX_CONFIRM_NETWORK:
            type_str = "Network Access";
            break;
        case AC_SANDBOX_CONFIRM_DANGEROUS:
            type_str = "Potentially Dangerous Operation";
            break;
        default:
            type_str = "Unknown Operation";
    }
    printf("│ Type: %-57s │\n", type_str);

    /* Show resource (truncate if too long) */
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

    /* Show reason */
    if (request->reason) {
        printf("│ Reason: %-55s │\n", request->reason);
    }

    printf("├─────────────────────────────────────────────────────────────────┤\n");

    /* Show AI suggestion */
    if (request->ai_suggestion) {
        printf("│ AI Note: %-54s │\n", "");
        /* Word wrap the suggestion */
        const char *p = request->ai_suggestion;
        while (*p) {
            char line[56];
            int i = 0;
            while (*p && i < 55) {
                if (*p == '\n') {
                    p++;
                    break;
                }
                line[i++] = *p++;
            }
            line[i] = '\0';
            printf("│   %-61s │\n", line);
        }
    }

    printf("├─────────────────────────────────────────────────────────────────┤\n");
    printf("│ Options:                                                        │\n");
    printf("│   [y] Yes, allow this operation                                 │\n");
    printf("│   [n] No, deny this operation                                   │\n");
    printf("│   [a] Allow all similar operations this session                 │\n");
    printf("└─────────────────────────────────────────────────────────────────┘\n");

    printf("\nYour choice [y/n/a]: ");
    fflush(stdout);

    /* Read user input */
    char input[16];
    if (!fgets(input, sizeof(input), stdin)) {
        printf("No input received, denying.\n");
        return AC_SANDBOX_DENY;
    }

    /* Parse response */
    char choice = input[0];
    if (choice == 'y' || choice == 'Y') {
        printf("Allowed.\n\n");
        return AC_SANDBOX_ALLOW;
    } else if (choice == 'a' || choice == 'A') {
        printf("Allowed for this session.\n\n");
        return AC_SANDBOX_ALLOW_SESSION;
    } else {
        printf("Denied.\n\n");
        return AC_SANDBOX_DENY;
    }
}

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
    printf("Sandbox Options (sandbox is enabled by default):\n");
    printf("  --no-sandbox            Disable sandbox protection\n");
    printf("  --workspace PATH        Workspace path for sandbox (default: current dir)\n");
    printf("  --sandbox-network       Allow network access in sandbox\n");
    printf("  --sandbox-strict        Enable strict sandbox mode\n");
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
    printf("Built on ArC framework\n");
    printf("Sandbox: %s (%s)\n",
           ac_sandbox_backend_name(),
           ac_sandbox_is_supported() ? "available" : "not available");
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

    /* Load multi-level config */
    ac_env_load_verbose("arc-cli");

    const char *api_key = ac_env_require("OPENAI_API_KEY");
    const char *model = ac_env_get("OPENAI_MODEL", "gpt-3.5-turbo");
    const char *base_url = ac_env_get("OPENAI_BASE_URL", NULL);
    const char *env_provider = ac_env_get("PROVIDER", "openai");

    if (!api_key) {
        ac_env_print_help("arc-cli");
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
    config->enable_stream = 1;

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
    config->workspace_path = ac_env_get("SANDBOX_WORKSPACE", NULL);

    const char *sandbox_net_str = ac_env_get("SANDBOX_ALLOW_NETWORK", "false");
    if (sandbox_net_str && (strcmp(sandbox_net_str, "true") == 0 || strcmp(sandbox_net_str, "1") == 0)) {
        config->sandbox_allow_network = 1;
    }

    const char *sandbox_strict_str = ac_env_get("SANDBOX_STRICT", "false");
    if (sandbox_strict_str && (strcmp(sandbox_strict_str, "true") == 0 || strcmp(sandbox_strict_str, "1") == 0)) {
        config->sandbox_strict_mode = 1;
    }

    *interactive = 1;  /* Default to interactive mode */
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
        } else if (strcmp(argv[i], "--no-sandbox") == 0) {
            config->enable_sandbox = 0;
        } else if (strcmp(argv[i], "--workspace") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --workspace requires an argument\n");
                return -1;
            }
            config->workspace_path = argv[i];
        } else if (strcmp(argv[i], "--sandbox-network") == 0) {
            config->sandbox_allow_network = 1;
        } else if (strcmp(argv[i], "--sandbox-strict") == 0) {
            config->sandbox_strict_mode = 1;
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
    ac_sandbox_t *sandbox = NULL;

    /* Parse arguments */
    ret = parse_args(argc, argv, &config, &interactive, &prompt);
    if (ret != 0) {
        return ret > 0 ? 0 : 1;
    }

    /* Initialize sandbox if enabled */
    if (config.enable_sandbox) {
        /* Get workspace path (default to current directory) */
        char cwd[4096];
        const char *workspace = config.workspace_path;
        if (!workspace) {
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                workspace = cwd;
            } else {
                workspace = ".";
            }
        }

        /* Create sandbox configuration */
        ac_sandbox_config_t sb_config = {
            .workspace_path = workspace,
            .path_rules = NULL,
            .path_rules_count = 0,
            .readonly_paths = NULL,
            .allow_network = config.sandbox_allow_network,
            .allow_process_exec = 1,  /* Need to execute commands */
            .strict_mode = config.sandbox_strict_mode,
            .log_violations = config.verbose,
        };

        /* Create sandbox (but do NOT enter it in main process) */
        sandbox = ac_sandbox_create(&sb_config);
        if (!sandbox) {
            const ac_sandbox_error_t *err = ac_sandbox_last_error();
            fprintf(stderr, "Warning: Failed to create sandbox");
            if (err && err->message) {
                fprintf(stderr, ": %s", err->message);
            }
            fprintf(stderr, "\n");
            fprintf(stderr, "Continuing without sandbox protection.\n");
        } else {
            /* Set up human-in-the-loop confirmation callback */
            ac_sandbox_set_confirm_callback(sandbox, sandbox_confirm_callback, NULL);

            if (!config.quiet) {
                printf("Sandbox configured: %s (workspace: %s)\n",
                       ac_sandbox_backend_name(), workspace);
                printf("Commands will be executed in sandboxed subprocesses.\n");
                printf("You will be prompted to confirm operations outside the workspace.\n");
            }

            /* Set sandbox for tools - tools will use ac_sandbox_exec() */
            builtin_tools_set_sandbox(sandbox);
        }
    }

    /* Create CLI instance */
    minimal_cli_t *cli = minimal_cli_create(&config);
    if (!cli) {
        fprintf(stderr, "Error: Failed to initialize Minimal CLI\n");
        if (sandbox) {
            ac_sandbox_destroy(sandbox);
        }
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

    /* Clear sandbox reference before destroying */
    builtin_tools_set_sandbox(NULL);
    if (sandbox) {
        ac_sandbox_destroy(sandbox);
    }

    return ret;
}
