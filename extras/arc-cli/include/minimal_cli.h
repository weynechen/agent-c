/**
 * @file minimal_cli.h
 * @brief Minimal CLI - Lightweight AI Command Line Tool
 *
 * A lightweight AI command line tool built on ArC framework.
 */

#ifndef MINIMAL_CLI_H
#define MINIMAL_CLI_H

#include <arc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version Information
 *============================================================================*/

#define MINIMAL_CLI_VERSION_MAJOR 0
#define MINIMAL_CLI_VERSION_MINOR 1
#define MINIMAL_CLI_VERSION_PATCH 0

/*============================================================================
 * Configuration
 *============================================================================*/

typedef struct {
    /* LLM Configuration */
    const char *provider;      /* openai, anthropic, deepseek */
    const char *model;
    const char *api_key;
    const char *api_base;
    float temperature;
    int timeout_ms;

    /* Agent Configuration */
    int max_iterations;
    int enable_tools;
    int enable_stream;

    /* Safety Configuration */
    int safe_mode;             /* Require confirmation for dangerous commands */

    /* Sandbox Configuration */
    int enable_sandbox;        /* Enable sandbox for command execution */
    const char *workspace_path; /* Workspace path for sandbox (defaults to cwd) */
    int sandbox_allow_network; /* Allow network access in sandbox */
    int sandbox_strict_mode;   /* Strict sandbox mode */

    /* Output Configuration */
    int verbose;
    int quiet;
    int json_output;
} minimal_cli_config_t;

/*============================================================================
 * Main Application Handle
 *============================================================================*/

typedef struct minimal_cli minimal_cli_t;

/**
 * @brief Create minimal CLI instance
 *
 * @param config  Configuration
 * @return Instance handle, NULL on error
 */
minimal_cli_t *minimal_cli_create(const minimal_cli_config_t *config);

/**
 * @brief Run interactive mode
 *
 * @param cli  Minimal CLI instance
 * @return 0 on success, error code otherwise
 */
int minimal_cli_run_interactive(minimal_cli_t *cli);

/**
 * @brief Run with single prompt
 *
 * @param cli     Minimal CLI instance
 * @param prompt  User prompt
 * @return 0 on success, error code otherwise
 */
int minimal_cli_run_once(minimal_cli_t *cli, const char *prompt);

/**
 * @brief Destroy minimal CLI instance
 *
 * @param cli  Instance to destroy
 */
void minimal_cli_destroy(minimal_cli_t *cli);

#ifdef __cplusplus
}
#endif

#endif /* MINIMAL_CLI_H */
