/**
 * @file code_agentc.h
 * @brief Code-AgentC Main Header
 *
 * Terminal-based code generation AI agent built on AgentC framework.
 */

#ifndef CODE_AGENTC_H
#define CODE_AGENTC_H

#include <agentc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version Information
 *============================================================================*/

#define CODE_AGENTC_VERSION_MAJOR 0
#define CODE_AGENTC_VERSION_MINOR 1
#define CODE_AGENTC_VERSION_PATCH 0

/*============================================================================
 * Configuration
 *============================================================================*/

typedef struct {
    /* LLM Configuration */
    const char *model;
    const char *api_key;
    const char *api_base;
    
    /* Project Configuration */
    const char *project_dir;
    const char *rules_dir;
    const char *skills_dir;
    
    /* MCP Configuration */
    const char *mcp_server_url;
    int enable_mcp;
    
    /* Agent Configuration */
    int max_iterations;
    float temperature;
    int verbose;
} code_agentc_config_t;

/*============================================================================
 * Main Application Handle
 *============================================================================*/

typedef struct code_agentc code_agentc_t;

/**
 * @brief Create code-agentc instance
 *
 * @param config  Configuration
 * @return Instance handle, NULL on error
 */
code_agentc_t *code_agentc_create(const code_agentc_config_t *config);

/**
 * @brief Run interactive mode
 *
 * @param app  Code-agentc instance
 * @return 0 on success, error code otherwise
 */
int code_agentc_run_interactive(code_agentc_t *app);

/**
 * @brief Run with single prompt
 *
 * @param app     Code-agentc instance
 * @param prompt  User prompt
 * @return 0 on success, error code otherwise
 */
int code_agentc_run_once(code_agentc_t *app, const char *prompt);

/**
 * @brief Destroy code-agentc instance
 *
 * @param app  Instance to destroy
 */
void code_agentc_destroy(code_agentc_t *app);

#ifdef __cplusplus
}
#endif

#endif /* CODE_AGENTC_H */
