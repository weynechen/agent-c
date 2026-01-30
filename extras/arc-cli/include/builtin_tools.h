/**
 * @file builtin_tools.h
 * @brief Built-in Tools for Minimal CLI
 *
 * This file uses AC_TOOL_META markers that are recognized by MOC
 * (Meta-Object Compiler) to generate tool wrappers and schemas.
 */

#ifndef BUILTIN_TOOLS_H
#define BUILTIN_TOOLS_H

#include <stdbool.h>

/* AC_TOOL_META marker - recognized by MOC but ignored by compiler */
#ifndef AC_TOOL_META
#define AC_TOOL_META
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Shell Execution Tool
 *============================================================================*/

/**
 * @description: Execute a shell command and return its output. Use for system operations, file management, git commands, etc.
 * @param: command  The shell command to execute
 */
AC_TOOL_META const char* shell_execute(const char* command);

/*============================================================================
 * File Operation Tools
 *============================================================================*/

/**
 * @description: Read the contents of a file and return as string
 * @param: path  Path to the file to read
 */
AC_TOOL_META const char* read_file(const char* path);

/**
 * @description: Write content to a file. Creates new file or overwrites existing
 * @param: path     Path to the file to write
 * @param: content  Content to write to the file
 */
AC_TOOL_META const char* write_file(const char* path, const char* content);

/**
 * @description: List files and directories in a directory
 * @param: path  Path to the directory to list
 */
AC_TOOL_META const char* list_directory(const char* path);

/*============================================================================
 * Utility Tools
 *============================================================================*/

/**
 * @description: Get the current date, time, and timezone information
 */
AC_TOOL_META const char* get_current_time(void);

/**
 * @description: Perform arithmetic calculations. Supports add, subtract, multiply, divide, power, mod operations
 * @param: operation  Operation to perform (add, subtract, multiply, divide, power, mod or +, -, *, /, ^, %)
 * @param: a  First operand
 * @param: b  Second operand
 */
AC_TOOL_META double calculator(const char* operation, double a, double b);

/*============================================================================
 * Safe Mode Configuration (Internal Use)
 *============================================================================*/

/**
 * @brief Set safe mode for dangerous command protection
 *
 * This is NOT an AC_TOOL_META function - it's for internal configuration.
 *
 * @param enabled  1 to enable safe mode, 0 to disable
 */
void builtin_tools_set_safe_mode(int enabled);

/*============================================================================
 * Sandbox Configuration (Internal Use)
 *============================================================================*/

/* Forward declaration - sandbox handle from ac_hosted */
struct ac_sandbox;

/**
 * @brief Set sandbox for tool execution
 *
 * When sandbox is set, tools will check permissions before accessing files
 * or executing commands.
 *
 * @param sandbox  Sandbox handle (NULL to disable)
 */
void builtin_tools_set_sandbox(struct ac_sandbox *sandbox);

/**
 * @brief Get current sandbox handle
 *
 * @return Current sandbox handle, or NULL if not set
 */
struct ac_sandbox *builtin_tools_get_sandbox(void);

#ifdef __cplusplus
}
#endif

#endif /* BUILTIN_TOOLS_H */
