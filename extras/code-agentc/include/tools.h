/**
 * @file tools.h
 * @brief Code-AgentC Tool Definitions
 *
 * All built-in tools for code generation and manipulation.
 */

#ifndef CODE_AGENTC_TOOLS_H
#define CODE_AGENTC_TOOLS_H

#include <agentc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Tool Registration
 *============================================================================*/

/**
 * @brief Register all built-in tools
 *
 * @param tools  Tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t code_agentc_register_all_tools(ac_tools_t *tools);

/*============================================================================
 * File Tools
 *============================================================================*/

/**
 * @brief Register file operation tools
 *
 * Tools: read_file, write_file, list_directory, delete_file
 *
 * @param tools  Tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t code_agentc_register_file_tools(ac_tools_t *tools);

/*============================================================================
 * Search Tools
 *============================================================================*/

/**
 * @brief Register code search tools
 *
 * Tools: grep_code, find_files, search_definition
 *
 * @param tools  Tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t code_agentc_register_search_tools(ac_tools_t *tools);

/*============================================================================
 * Shell Tools
 *============================================================================*/

/**
 * @brief Register shell execution tools
 *
 * Tools: execute_command, get_env
 *
 * @param tools  Tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t code_agentc_register_shell_tools(ac_tools_t *tools);

/*============================================================================
 * Git Tools
 *============================================================================*/

/**
 * @brief Register git integration tools
 *
 * Tools: git_status, git_diff, git_commit, git_log
 *
 * @param tools  Tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t code_agentc_register_git_tools(ac_tools_t *tools);

#ifdef __cplusplus
}
#endif

#endif /* CODE_AGENTC_TOOLS_H */
