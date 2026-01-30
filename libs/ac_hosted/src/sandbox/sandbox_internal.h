/**
 * @file sandbox_internal.h
 * @brief Sandbox Internal API
 *
 * Internal functions shared between sandbox implementations.
 * This header is NOT part of the public API.
 */

#ifndef ARC_SANDBOX_INTERNAL_H
#define ARC_SANDBOX_INTERNAL_H

#include <arc/sandbox.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Internal Sandbox Structure
 *============================================================================*/

struct ac_sandbox {
    /* Configuration (copied from user config) */
    char *workspace_path;
    ac_sandbox_path_rule_t *path_rules;
    size_t path_rules_count;
    char **readonly_paths;          /* NULL-terminated array */
    int allow_network;
    int allow_process_exec;
    int strict_mode;
    int log_violations;

    /* State */
    int is_active;
    ac_sandbox_backend_t backend;
    ac_sandbox_level_t level;

    /* Human-in-the-loop callback */
    ac_sandbox_confirm_fn confirm_callback;
    void *confirm_user_data;

    /* Session-level permissions (from ALLOW_SESSION responses) */
    int session_allow_dangerous_commands;
    int session_allow_external_paths;
    int session_allow_network;

    /* Platform-specific data */
    void *platform_data;
};

/*============================================================================
 * Error Handling (from sandbox_common.c)
 *============================================================================*/

/**
 * @brief Set the last error
 */
void ac_sandbox_set_error(
    ac_sandbox_error_code_t code,
    const char *message,
    const char *ai_explanation,
    const char *suggestion,
    const char *blocked_resource,
    int platform_errno
);

/**
 * @brief Set denial reason for check functions
 */
void ac_sandbox_set_denial_reason(const char *reason);

/*============================================================================
 * Path Utilities (from sandbox_common.c)
 *============================================================================*/

/**
 * @brief Normalize a path
 */
int ac_sandbox_normalize_path(const char *path, char *buffer, size_t size);

/**
 * @brief Check if child path is under parent path
 */
int ac_sandbox_path_is_under(const char *parent, const char *child);

/**
 * @brief Check if command contains dangerous patterns
 */
int ac_sandbox_is_command_dangerous(const char *command);

/**
 * @brief Get default readonly paths for current platform
 */
const char **ac_sandbox_get_default_readonly_paths(void);

/*============================================================================
 * Platform Detection Helpers
 *============================================================================*/

#if defined(__linux__)

/**
 * @brief Check if Landlock is supported
 * @return Landlock ABI version (>0) if supported, 0 if not
 */
int ac_sandbox_linux_landlock_abi(void);

/**
 * @brief Check if Seccomp is supported
 * @return 1 if supported, 0 if not
 */
int ac_sandbox_linux_seccomp_available(void);

#endif /* __linux__ */

#if defined(__APPLE__)

/**
 * @brief Check if Seatbelt is available
 * @return 1 if available, 0 if not
 */
int ac_sandbox_macos_seatbelt_available(void);

#endif /* __APPLE__ */

#ifdef __cplusplus
}
#endif

#endif /* ARC_SANDBOX_INTERNAL_H */
