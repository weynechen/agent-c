/**
 * @file code_agent.h
 * @brief Code Agent - AI Coding Assistant
 *
 * A code-focused AI agent built on ArC framework.
 * Inspired by opencode's tool design and prompting patterns.
 */

#ifndef CODE_AGENT_H
#define CODE_AGENT_H

#include <arc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version Information
 *============================================================================*/

#define CODE_AGENT_VERSION_MAJOR 0
#define CODE_AGENT_VERSION_MINOR 1
#define CODE_AGENT_VERSION_PATCH 0

/*============================================================================
 * Configuration
 *============================================================================*/

typedef struct {
    /* LLM Configuration */
    const char *provider;       /* openai, anthropic, deepseek */
    const char *model;
    const char *api_key;
    const char *api_base;
    float temperature;
    int timeout_ms;

    /* Workspace Configuration */
    const char *workspace;      /* Working directory for code operations */

    /* Agent Configuration */
    int max_iterations;         /* Max tool call iterations */
    int enable_tools;           /* Enable tool calling */

    /* Safety Configuration */
    int safe_mode;              /* Confirm dangerous operations */
    int enable_sandbox;         /* Enable sandbox protection */
    int sandbox_allow_network;  /* Allow network in sandbox */

    /* System Prompt Selection */
    const char *system_prompt;  /* System prompt name (e.g., "anthropic") */

    /* Output Configuration */
    int verbose;
    int quiet;
    int json_output;
} code_agent_config_t;

/*============================================================================
 * Code Agent Handle
 *============================================================================*/

typedef struct code_agent code_agent_t;

/**
 * @brief Create code agent instance
 *
 * @param config  Configuration
 * @return Instance handle, NULL on error
 */
code_agent_t *code_agent_create(const code_agent_config_t *config);

/**
 * @brief Run interactive mode (REPL)
 *
 * @param agent  Code agent instance
 * @return 0 on success, error code otherwise
 */
int code_agent_run_interactive(code_agent_t *agent);

/**
 * @brief Run with single task
 *
 * @param agent  Code agent instance
 * @param task   Task description
 * @return 0 on success, error code otherwise
 */
int code_agent_run_once(code_agent_t *agent, const char *task);

/**
 * @brief Destroy code agent instance
 *
 * @param agent  Instance to destroy
 */
void code_agent_destroy(code_agent_t *agent);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Get default configuration
 *
 * @return Default configuration with sensible defaults
 */
code_agent_config_t code_agent_default_config(void);

#ifdef __cplusplus
}
#endif

#endif /* CODE_AGENT_H */
