/**
 * @file sandbox_fallback.c
 * @brief Fallback Sandbox Implementation
 *
 * Software-based sandbox for platforms without kernel sandbox support.
 * Used on Windows and other unsupported platforms.
 *
 * This implementation provides:
 * - Path-based access control (software checks)
 * - Dangerous command detection
 * - Consistent API with hardware-backed sandboxes
 *
 * IMPORTANT: This does NOT provide kernel-level security.
 * It only provides application-level filtering that can be bypassed.
 */

#if !defined(__linux__) && !(defined(__APPLE__) && defined(__MACH__))

#include "sandbox_internal.h"
#include <arc/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <sys/wait.h>
#define PATH_SEP '/'
#endif

/*============================================================================
 * Fallback-Specific Data
 *============================================================================*/

typedef struct {
    int initialized;
} fallback_sandbox_data_t;

/*============================================================================
 * Public API Implementation
 *============================================================================*/

int ac_sandbox_is_supported(void) {
    /* Software filtering is always "supported" but not secure */
    return 1;
}

ac_sandbox_backend_t ac_sandbox_get_backend(void) {
    return AC_SANDBOX_BACKEND_SOFTWARE;
}

const char *ac_sandbox_backend_name(void) {
    return "Software";
}

ac_sandbox_level_t ac_sandbox_get_level(void) {
    return AC_SANDBOX_LEVEL_BASIC;
}

const char *ac_sandbox_platform_info(void) {
    static char info[256];

#if defined(_WIN32)
    const char *platform = "Windows";
#else
    const char *platform = "Unknown";
#endif

    snprintf(info, sizeof(info),
        "{"
        "\"platform\":\"%s\","
        "\"backend\":\"Software\","
        "\"level\":\"basic\","
        "\"warning\":\"No kernel sandbox - software filtering only\""
        "}",
        platform
    );

    return info;
}

ac_sandbox_t *ac_sandbox_create(const ac_sandbox_config_t *config) {
    if (!config) {
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_INVALID_CONFIG,
            "NULL configuration",
            "The sandbox configuration pointer is NULL. A valid configuration "
            "structure must be provided to create a sandbox.",
            "Provide a valid ac_sandbox_config_t structure.",
            NULL, 0
        );
        return NULL;
    }

    ac_sandbox_clear_error();

    /* Allocate sandbox structure */
    ac_sandbox_t *sandbox = calloc(1, sizeof(ac_sandbox_t));
    if (!sandbox) {
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_INTERNAL,
            "Memory allocation failed",
            "Failed to allocate memory for sandbox structure.",
            "Check available memory.",
            NULL, errno
        );
        return NULL;
    }

    /* Allocate platform data */
    fallback_sandbox_data_t *data = calloc(1, sizeof(fallback_sandbox_data_t));
    if (!data) {
        free(sandbox);
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_INTERNAL,
            "Memory allocation failed",
            "Failed to allocate memory for platform-specific data.",
            "Check available memory.",
            NULL, errno
        );
        return NULL;
    }
    sandbox->platform_data = data;

    /* Copy configuration */
    if (config->workspace_path) {
        sandbox->workspace_path = strdup(config->workspace_path);
    }

    sandbox->allow_network = config->allow_network;
    sandbox->allow_process_exec = config->allow_process_exec;
    sandbox->strict_mode = config->strict_mode;
    sandbox->log_violations = config->log_violations;

    /* Copy path rules */
    if (config->path_rules && config->path_rules_count > 0) {
        sandbox->path_rules = calloc(config->path_rules_count,
                                     sizeof(ac_sandbox_path_rule_t));
        if (sandbox->path_rules) {
            for (size_t i = 0; i < config->path_rules_count; i++) {
                sandbox->path_rules[i].path = strdup(config->path_rules[i].path);
                sandbox->path_rules[i].permissions = config->path_rules[i].permissions;
            }
            sandbox->path_rules_count = config->path_rules_count;
        }
    }

    /* Copy readonly paths */
    if (config->readonly_paths) {
        size_t count = 0;
        while (config->readonly_paths[count]) count++;

        sandbox->readonly_paths = calloc(count + 1, sizeof(char *));
        if (sandbox->readonly_paths) {
            for (size_t i = 0; i < count; i++) {
                sandbox->readonly_paths[i] = strdup(config->readonly_paths[i]);
            }
        }
    }

    sandbox->backend = AC_SANDBOX_BACKEND_SOFTWARE;
    sandbox->level = AC_SANDBOX_LEVEL_BASIC;

    AC_LOG_WARN("Created software-only sandbox (no kernel protection)");
    AC_LOG_WARN("This sandbox provides application-level filtering only!");

    return sandbox;
}

arc_err_t ac_sandbox_enter(ac_sandbox_t *sandbox) {
    if (!sandbox) {
        return ARC_ERR_INVALID_ARG;
    }

    if (sandbox->is_active) {
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_ALREADY_ACTIVE,
            "Sandbox already active",
            "The sandbox has already been entered and is currently active.",
            "The sandbox can only be entered once.",
            NULL, 0
        );
        return ARC_ERR_INVALID_ARG;
    }

    fallback_sandbox_data_t *data = (fallback_sandbox_data_t *)sandbox->platform_data;

    /* No kernel-level sandbox to set up, just mark as active */
    sandbox->is_active = 1;
    data->initialized = 1;

    AC_LOG_WARN("Software sandbox entered - NO KERNEL PROTECTION");
    AC_LOG_WARN("Security relies on application-level checks via ac_sandbox_check_*()");

    return ARC_OK;
}

int ac_sandbox_is_active(const ac_sandbox_t *sandbox) {
    return sandbox ? sandbox->is_active : 0;
}

void ac_sandbox_destroy(ac_sandbox_t *sandbox) {
    if (!sandbox) {
        return;
    }

    fallback_sandbox_data_t *data = (fallback_sandbox_data_t *)sandbox->platform_data;
    if (data) {
        free(data);
    }

    free(sandbox->workspace_path);

    if (sandbox->path_rules) {
        for (size_t i = 0; i < sandbox->path_rules_count; i++) {
            free((void *)sandbox->path_rules[i].path);
        }
        free(sandbox->path_rules);
    }

    if (sandbox->readonly_paths) {
        for (int i = 0; sandbox->readonly_paths[i]; i++) {
            free(sandbox->readonly_paths[i]);
        }
        free(sandbox->readonly_paths);
    }

    free(sandbox);
    AC_LOG_DEBUG("Sandbox destroyed");
}

/*============================================================================
 * Software Filtering Implementation
 *============================================================================*/

/**
 * @brief Normalize path for comparison (Windows-aware)
 */
static int normalize_path_for_check(const char *path, char *buffer, size_t size) {
    if (!path || !buffer || size == 0) {
        return -1;
    }

#if defined(_WIN32)
    /* Get full path on Windows */
    if (_fullpath(buffer, path, size) == NULL) {
        strncpy(buffer, path, size - 1);
        buffer[size - 1] = '\0';
    }

    /* Convert backslashes to forward slashes for comparison */
    for (char *p = buffer; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* Convert to lowercase for case-insensitive comparison */
    for (char *p = buffer; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = *p - 'A' + 'a';
        }
    }
#else
    /* Use common normalization on other platforms */
    if (ac_sandbox_normalize_path(path, buffer, size) < 0) {
        strncpy(buffer, path, size - 1);
        buffer[size - 1] = '\0';
    }
#endif

    return 0;
}

/**
 * @brief Check if child path is under parent (Windows-aware)
 */
static int path_is_under_fallback(const char *parent, const char *child) {
    char norm_parent[4096];
    char norm_child[4096];

    if (normalize_path_for_check(parent, norm_parent, sizeof(norm_parent)) < 0) {
        return 0;
    }
    if (normalize_path_for_check(child, norm_child, sizeof(norm_child)) < 0) {
        return 0;
    }

    size_t parent_len = strlen(norm_parent);

    /* Remove trailing slash from parent if present */
    if (parent_len > 0 && norm_parent[parent_len - 1] == '/') {
        norm_parent[parent_len - 1] = '\0';
        parent_len--;
    }

    /* Check if child starts with parent path */
    if (strncmp(norm_parent, norm_child, parent_len) != 0) {
        return 0;
    }

    /* Child must either equal parent or have a separator after parent */
    if (norm_child[parent_len] == '\0' || norm_child[parent_len] == '/') {
        return 1;
    }

    return 0;
}

int ac_sandbox_check_path(
    const ac_sandbox_t *sandbox,
    const char *path,
    unsigned int permissions
) {
    if (!sandbox || !path) {
        return 0;
    }

    /* Check workspace path */
    if (sandbox->workspace_path &&
        path_is_under_fallback(sandbox->workspace_path, path)) {
        return 1;
    }

    /* Check custom path rules */
    for (size_t i = 0; i < sandbox->path_rules_count; i++) {
        const ac_sandbox_path_rule_t *rule = &sandbox->path_rules[i];
        if (path_is_under_fallback(rule->path, path)) {
            if ((rule->permissions & permissions) == permissions) {
                return 1;
            }
        }
    }

    /* Check readonly paths for read-only access */
    if ((permissions & ~AC_SANDBOX_PERM_FS_READ) == 0) {
        if (sandbox->readonly_paths) {
            for (int i = 0; sandbox->readonly_paths[i]; i++) {
                if (path_is_under_fallback(sandbox->readonly_paths[i], path)) {
                    return 1;
                }
            }
        }
    }

    /* If not strict mode and sandbox is not fully active, allow */
    if (!sandbox->strict_mode && !sandbox->is_active) {
        return 1;
    }

    /* Path not in allowed list - request human confirmation */
    if (sandbox->session_allow_external_paths) {
        return 1;
    }

    ac_sandbox_confirm_type_t type =
        (permissions & (AC_SANDBOX_PERM_FS_WRITE | AC_SANDBOX_PERM_FS_CREATE | AC_SANDBOX_PERM_FS_DELETE))
        ? AC_SANDBOX_CONFIRM_PATH_WRITE
        : AC_SANDBOX_CONFIRM_PATH_READ;

    char reason[256];
    snprintf(reason, sizeof(reason), "Path '%s' is outside the workspace", path);

    ac_sandbox_confirm_request_t request = {
        .type = type,
        .resource = path,
        .reason = reason,
        .ai_suggestion = type == AC_SANDBOX_CONFIRM_PATH_WRITE
            ? "This file is outside the workspace."
            : "This file is outside the workspace."
    };

    ac_sandbox_confirm_result_t result = ac_sandbox_request_confirm(
        (ac_sandbox_t *)sandbox, &request);

    if (result == AC_SANDBOX_ALLOW || result == AC_SANDBOX_ALLOW_SESSION) {
        return 1;
    }

    ac_sandbox_set_denial_reason(reason);
    if (sandbox->log_violations) {
        AC_LOG_WARN("Sandbox: access denied - %s", reason);
    }

    return 0;
}

int ac_sandbox_check_command(
    const ac_sandbox_t *sandbox,
    const char *command
) {
    if (!sandbox || !command) {
        return 0;
    }

    /* Check for dangerous command patterns */
    if (ac_sandbox_is_command_dangerous(command)) {
        if (!sandbox->session_allow_dangerous_commands) {
            ac_sandbox_confirm_request_t request = {
                .type = AC_SANDBOX_CONFIRM_DANGEROUS,
                .resource = command,
                .reason = "Command contains potentially dangerous patterns",
                .ai_suggestion = "This command may be destructive."
            };

            ac_sandbox_confirm_result_t result = ac_sandbox_request_confirm(
                (ac_sandbox_t *)sandbox, &request);

            if (result != AC_SANDBOX_ALLOW && result != AC_SANDBOX_ALLOW_SESSION) {
                ac_sandbox_set_denial_reason("Dangerous command denied by user");
                return 0;
            }
        }
    }

#if defined(_WIN32)
    /* Windows-specific dangerous patterns */
    const char *win_dangerous[] = {
        "format ", "del /s", "rd /s", "rmdir /s",
        "net user", "net localgroup", "reg delete", "reg add",
        "bcdedit", "diskpart", "takeown", "icacls", NULL
    };

    for (int i = 0; win_dangerous[i]; i++) {
        if (strstr(command, win_dangerous[i])) {
            if (!sandbox->session_allow_dangerous_commands) {
                ac_sandbox_confirm_request_t request = {
                    .type = AC_SANDBOX_CONFIRM_DANGEROUS,
                    .resource = command,
                    .reason = "Windows dangerous command detected",
                    .ai_suggestion = "This Windows command may be destructive."
                };

                ac_sandbox_confirm_result_t result = ac_sandbox_request_confirm(
                    (ac_sandbox_t *)sandbox, &request);

                if (result != AC_SANDBOX_ALLOW && result != AC_SANDBOX_ALLOW_SESSION) {
                    ac_sandbox_set_denial_reason("Windows dangerous command denied");
                    return 0;
                }
            }
            break;
        }
    }
#endif

    /* Check process exec permission */
    if (!sandbox->allow_process_exec && sandbox->strict_mode) {
        ac_sandbox_set_denial_reason("Process execution is disabled in strict mode");
        return 0;
    }

    /* Check for network commands if network is disabled */
    if (!sandbox->allow_network && !sandbox->session_allow_network) {
#if defined(_WIN32)
        const char *net_commands[] = {
            "curl", "wget", "Invoke-WebRequest", "Invoke-RestMethod",
            "ssh", "scp", "sftp", "ftp", "telnet", NULL
        };
#else
        const char *net_commands[] = {
            "curl", "wget", "nc", "netcat", "ssh", "scp", NULL
        };
#endif
        for (int i = 0; net_commands[i]; i++) {
            if (strstr(command, net_commands[i])) {
                if (strstr(command, "--version") || strstr(command, "-V") ||
                    strstr(command, "/version") || strstr(command, "/?")) {
                    continue;
                }

                ac_sandbox_confirm_request_t request = {
                    .type = AC_SANDBOX_CONFIRM_NETWORK,
                    .resource = command,
                    .reason = "Command requires network access",
                    .ai_suggestion = "This command will access the network."
                };

                ac_sandbox_confirm_result_t result = ac_sandbox_request_confirm(
                    (ac_sandbox_t *)sandbox, &request);

                if (result != AC_SANDBOX_ALLOW && result != AC_SANDBOX_ALLOW_SESSION) {
                    ac_sandbox_set_denial_reason("Network command denied by user");
                    return 0;
                }
                break;
            }
        }
    }

    return 1;
}

/*============================================================================
 * Sandboxed Subprocess Execution (Fallback - Software filtering only)
 *============================================================================*/

arc_err_t ac_sandbox_exec_timeout(
    ac_sandbox_t *sandbox,
    const char *command,
    char *output,
    size_t output_size,
    int *exit_code,
    int timeout_ms
) {
    if (!sandbox || !command) {
        return ARC_ERR_INVALID_ARG;
    }

    /* Software filtering: check command before execution */
    if (!ac_sandbox_check_command(sandbox, command)) {
        if (output && output_size > 0) {
            snprintf(output, output_size,
                     "{\"error\":\"Command blocked by sandbox\",\"reason\":\"%s\"}",
                     ac_sandbox_denial_reason());
        }
        if (exit_code) *exit_code = -1;
        return ARC_ERR_INVALID_ARG;
    }

    AC_LOG_WARN("Fallback sandbox: executing without kernel isolation");

#if defined(_WIN32)
    /* Windows implementation using _popen */
    FILE *fp = _popen(command, "r");
    if (!fp) {
        if (output && output_size > 0) {
            snprintf(output, output_size,
                     "{\"error\":\"Failed to execute command\"}");
        }
        if (exit_code) *exit_code = -1;
        return ARC_ERR_IO;
    }

    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total_read = 0;
        char buf[256];

        while (fgets(buf, sizeof(buf), fp) != NULL) {
            size_t len = strlen(buf);
            size_t remaining = output_size - total_read - 1;
            if (remaining > 0) {
                size_t to_copy = len < remaining ? len : remaining;
                memcpy(output + total_read, buf, to_copy);
                total_read += to_copy;
                output[total_read] = '\0';
            }
        }
    }

    int status = _pclose(fp);
    if (exit_code) *exit_code = status;

#else
    /* Non-Windows fallback using popen (no kernel sandbox) */
    FILE *fp = popen(command, "r");
    if (!fp) {
        if (output && output_size > 0) {
            snprintf(output, output_size,
                     "{\"error\":\"Failed to execute command\"}");
        }
        if (exit_code) *exit_code = -1;
        return ARC_ERR_IO;
    }

    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total_read = 0;
        char buf[256];

        while (fgets(buf, sizeof(buf), fp) != NULL) {
            size_t len = strlen(buf);
            size_t remaining = output_size - total_read - 1;
            if (remaining > 0) {
                size_t to_copy = len < remaining ? len : remaining;
                memcpy(output + total_read, buf, to_copy);
                total_read += to_copy;
                output[total_read] = '\0';
            }
        }
    }

    int status = pclose(fp);
    if (exit_code) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else {
            *exit_code = -1;
        }
    }
#endif

    (void)timeout_ms;  /* Timeout not implemented in fallback */

    return ARC_OK;
}

arc_err_t ac_sandbox_exec(
    ac_sandbox_t *sandbox,
    const char *command,
    char *output,
    size_t output_size,
    int *exit_code
) {
    return ac_sandbox_exec_timeout(sandbox, command, output, output_size,
                                   exit_code, 0);
}

#endif /* fallback platforms */
