/**
 * @file code_tools.h
 * @brief Code Agent Tools
 *
 * Tools for code operations, following opencode's design patterns.
 * MOC processes this file to generate wrappers and JSON schemas.
 */

#ifndef CODE_TOOLS_H
#define CODE_TOOLS_H

#include <stdbool.h>

/* AC_TOOL_META marker - recognized by MOC */
#ifndef AC_TOOL_META
#define AC_TOOL_META
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Bash Tool - Shell Command Execution
 *============================================================================*/

/**
 * @description: Execute a bash command with optional working directory and timeout. Use for git, npm, docker, build commands etc. Do NOT use for file operations (reading, writing, editing) - use specialized tools instead.
 * @param: command      The command to execute
 * @param: workdir      Working directory for command execution (optional, defaults to workspace)
 * @param: timeout      Timeout in milliseconds (optional, defaults to 120000)
 * @param: description  Brief description of what this command does (5-10 words)
 */
AC_TOOL_META const char* bash(
    const char* command,
    const char* workdir,
    int timeout,
    const char* description
);

/*============================================================================
 * Read Tool - File Reading
 *============================================================================*/

/**
 * @description: Read a file from the filesystem. Returns file content with line numbers. Use absolute paths.
 * @param: filePath  Absolute path to the file to read
 * @param: offset    Starting line number (0-based, optional)
 * @param: limit     Number of lines to read (optional, defaults to 2000)
 */
AC_TOOL_META const char* read_file(
    const char* filePath,
    int offset,
    int limit
);

/*============================================================================
 * Write Tool - File Writing
 *============================================================================*/

/**
 * @description: Write content to a file. Creates new file or overwrites existing. You MUST read the file first if it exists.
 * @param: filePath  Absolute path to the file to write
 * @param: content   Content to write to the file
 */
AC_TOOL_META const char* write_file(
    const char* filePath,
    const char* content
);

/*============================================================================
 * Edit Tool - String Replacement
 *============================================================================*/

/**
 * @description: Perform exact string replacement in a file. You MUST read the file first. The oldString must be unique in the file unless using replaceAll.
 * @param: filePath    Absolute path to the file to edit
 * @param: oldString   Text to find and replace (must be exact match)
 * @param: newString   Text to replace with
 * @param: replaceAll  Replace all occurrences if true (optional, defaults to false)
 */
AC_TOOL_META const char* edit_file(
    const char* filePath,
    const char* oldString,
    const char* newString,
    bool replaceAll
);

/*============================================================================
 * LS Tool - Directory Listing
 *============================================================================*/

/**
 * @description: List files and directories in a given path. Returns file names with types. Use absolute paths.
 * @param: path    Absolute path to directory to list
 * @param: ignore  Comma-separated glob patterns to ignore (optional, e.g. "node_modules,*.log")
 */
AC_TOOL_META const char* ls(
    const char* path,
    const char* ignore
);

/*============================================================================
 * Grep Tool - Content Search
 *============================================================================*/

/**
 * @description: Search file contents using regular expressions. Returns matching lines with file paths and line numbers.
 * @param: pattern   Regular expression pattern to search for
 * @param: path      File or directory to search in (defaults to workspace)
 * @param: include   Glob pattern to filter files (optional, e.g. "*.c" or "*.{ts,tsx}")
 */
AC_TOOL_META const char* grep(
    const char* pattern,
    const char* path,
    const char* include
);

/*============================================================================
 * Glob Tool - File Pattern Matching
 *============================================================================*/

/**
 * @description: Find files matching a glob pattern. Returns matching file paths sorted by modification time.
 * @param: pattern    Glob pattern to match (e.g. star-star-slash-star.ts)
 * @param: path       Directory to search in (optional, defaults to workspace)
 */
AC_TOOL_META const char* glob_files(
    const char* pattern,
    const char* path
);

/*============================================================================
 * Configuration (Internal Use - NOT Tool)
 *============================================================================*/

/**
 * @brief Set workspace path for tools
 * @param path  Workspace root path
 */
void code_tools_set_workspace(const char *path);

/**
 * @brief Get workspace path
 * @return Current workspace path
 */
const char *code_tools_get_workspace(void);

/**
 * @brief Set safe mode
 * @param enabled  1 to enable, 0 to disable
 */
void code_tools_set_safe_mode(int enabled);

/* Forward declaration */
struct ac_sandbox;

/**
 * @brief Set sandbox for tool execution
 * @param sandbox  Sandbox handle (NULL to disable)
 */
void code_tools_set_sandbox(struct ac_sandbox *sandbox);

/**
 * @brief Get current sandbox handle
 * @return Current sandbox, or NULL
 */
struct ac_sandbox *code_tools_get_sandbox(void);

#ifdef __cplusplus
}
#endif

#endif /* CODE_TOOLS_H */
