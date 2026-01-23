/**
 * @file builtin_tools.h
 * @brief Built-in Tools for Minimal CLI
 */

#ifndef BUILTIN_TOOLS_H
#define BUILTIN_TOOLS_H

#include <agentc/tool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and register all built-in tools
 *
 * Registers the following tools:
 * - shell_execute: Execute shell commands
 * - read_file: Read file contents
 * - write_file: Write file contents
 * - list_directory: List directory contents
 * - get_current_time: Get current date and time
 * - calculator: Perform arithmetic calculations
 *
 * @param safe_mode  If true, dangerous commands require confirmation
 * @return Tool registry, NULL on error
 */
ac_tools_t *builtin_tools_create(int safe_mode);

#ifdef __cplusplus
}
#endif

#endif /* BUILTIN_TOOLS_H */
