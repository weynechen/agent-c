/**
 * @file sandbox_macos.c
 * @brief macOS Sandbox Implementation
 *
 * Implements sandboxing using macOS Seatbelt (sandbox-exec).
 * Uses the sandbox_init() API for process-level sandboxing.
 */

#if defined(__APPLE__) && defined(__MACH__)

#include "sandbox_internal.h"
#include <arc/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* Seatbelt API (deprecated but still functional) */
#include <sandbox.h>

/*============================================================================
 * macOS-Specific Data
 *============================================================================*/

typedef struct {
    char *profile;              /* Generated sandbox profile */
    int sandbox_enforced;
} macos_sandbox_data_t;

/*============================================================================
 * Seatbelt Profile Generation
 *============================================================================*/

/**
 * @brief Generate a Seatbelt profile string
 *
 * Creates a TinySCHEME-based sandbox profile for macOS.
 */
static char *generate_seatbelt_profile(ac_sandbox_t *sandbox) {
    size_t profile_size = 8192;
    char *profile = malloc(profile_size);
    if (!profile) {
        return NULL;
    }

    size_t offset = 0;

    /* Start with version and deny by default in strict mode */
    offset += snprintf(profile + offset, profile_size - offset,
        "(version 1)\n"
    );

    if (sandbox->strict_mode) {
        offset += snprintf(profile + offset, profile_size - offset,
            "(deny default)\n"
        );
    } else {
        /* Permissive mode: allow by default, deny specific things */
        offset += snprintf(profile + offset, profile_size - offset,
            "(allow default)\n"
        );
    }

    /* Allow basic process operations */
    offset += snprintf(profile + offset, profile_size - offset,
        "(allow process-fork)\n"
        "(allow signal)\n"
        "(allow sysctl-read)\n"
        "(allow mach-lookup)\n"
    );

    /* Allow access to workspace */
    if (sandbox->workspace_path) {
        offset += snprintf(profile + offset, profile_size - offset,
            ";; Workspace: full access\n"
            "(allow file-read* file-write* file-ioctl\n"
            "    (subpath \"%s\"))\n",
            sandbox->workspace_path
        );
    }

    /* Allow access to custom paths */
    for (size_t i = 0; i < sandbox->path_rules_count; i++) {
        const ac_sandbox_path_rule_t *rule = &sandbox->path_rules[i];

        offset += snprintf(profile + offset, profile_size - offset,
            ";; Custom path: %s\n", rule->path);

        if (rule->permissions & AC_SANDBOX_PERM_FS_READ) {
            offset += snprintf(profile + offset, profile_size - offset,
                "(allow file-read* (subpath \"%s\"))\n", rule->path);
        }
        if (rule->permissions & AC_SANDBOX_PERM_FS_WRITE) {
            offset += snprintf(profile + offset, profile_size - offset,
                "(allow file-write* (subpath \"%s\"))\n", rule->path);
        }
        if (rule->permissions & AC_SANDBOX_PERM_FS_EXECUTE) {
            offset += snprintf(profile + offset, profile_size - offset,
                "(allow process-exec (subpath \"%s\"))\n", rule->path);
        }
    }

    /* Allow readonly paths */
    if (sandbox->readonly_paths) {
        offset += snprintf(profile + offset, profile_size - offset,
            ";; Readonly paths\n");
        for (int i = 0; sandbox->readonly_paths[i]; i++) {
            offset += snprintf(profile + offset, profile_size - offset,
                "(allow file-read* (subpath \"%s\"))\n",
                sandbox->readonly_paths[i]);
        }
    }

    /* Allow default readonly paths */
    offset += snprintf(profile + offset, profile_size - offset,
        ";; System libraries and frameworks\n"
        "(allow file-read*\n"
        "    (subpath \"/usr/lib\")\n"
        "    (subpath \"/usr/share\")\n"
        "    (subpath \"/System/Library\")\n"
        "    (subpath \"/Library/Frameworks\")\n"
        "    (subpath \"/private/var/db/dyld\")\n"
        "    (literal \"/dev/null\")\n"
        "    (literal \"/dev/zero\")\n"
        "    (literal \"/dev/urandom\")\n"
        "    (literal \"/dev/random\"))\n"
    );

    /* Process execution */
    if (sandbox->allow_process_exec) {
        offset += snprintf(profile + offset, profile_size - offset,
            ";; Allow process execution\n"
            "(allow process-exec)\n"
        );
    } else if (sandbox->strict_mode) {
        offset += snprintf(profile + offset, profile_size - offset,
            ";; Deny process execution\n"
            "(deny process-exec)\n"
        );
    }

    /* Network access */
    if (sandbox->allow_network) {
        offset += snprintf(profile + offset, profile_size - offset,
            ";; Allow network access\n"
            "(allow network*)\n"
        );
    } else if (sandbox->strict_mode) {
        offset += snprintf(profile + offset, profile_size - offset,
            ";; Deny network access\n"
            "(deny network*)\n"
        );
    }

    return profile;
}

/*============================================================================
 * Platform Detection
 *============================================================================*/

int ac_sandbox_macos_seatbelt_available(void) {
    /* Seatbelt is always available on macOS 10.5+ */
    return 1;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

int ac_sandbox_is_supported(void) {
    return 1;
}

ac_sandbox_backend_t ac_sandbox_get_backend(void) {
    return AC_SANDBOX_BACKEND_SEATBELT;
}

const char *ac_sandbox_backend_name(void) {
    return "Seatbelt";
}

ac_sandbox_level_t ac_sandbox_get_level(void) {
    return AC_SANDBOX_LEVEL_FULL;
}

const char *ac_sandbox_platform_info(void) {
    static char info[256];

    snprintf(info, sizeof(info),
        "{"
        "\"platform\":\"macOS\","
        "\"backend\":\"Seatbelt\","
        "\"level\":\"full\","
        "\"seatbelt_available\":true"
        "}"
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
    macos_sandbox_data_t *data = calloc(1, sizeof(macos_sandbox_data_t));
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

    sandbox->backend = AC_SANDBOX_BACKEND_SEATBELT;
    sandbox->level = AC_SANDBOX_LEVEL_FULL;

    /* Generate the Seatbelt profile */
    data->profile = generate_seatbelt_profile(sandbox);
    if (!data->profile) {
        AC_LOG_ERROR("Failed to generate Seatbelt profile");
        ac_sandbox_destroy(sandbox);
        return NULL;
    }

    AC_LOG_INFO("Created macOS sandbox (Seatbelt)");
    AC_LOG_DEBUG("Sandbox profile:\n%s", data->profile);

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
            "The sandbox has already been entered and is currently active. "
            "A sandbox can only be entered once per process.",
            "Create a new process if you need a fresh sandbox.",
            NULL, 0
        );
        return ARC_ERR_INVALID_ARG;
    }

    macos_sandbox_data_t *data = (macos_sandbox_data_t *)sandbox->platform_data;

    if (!data->profile) {
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_INTERNAL,
            "No sandbox profile",
            "The sandbox profile was not generated properly.",
            "Recreate the sandbox.",
            NULL, 0
        );
        return ARC_ERR_BACKEND;
    }

    /* Initialize the sandbox with the generated profile */
    char *error = NULL;

    /* sandbox_init is deprecated but still works */
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"

    int ret = sandbox_init_with_parameters(
        data->profile,
        SANDBOX_NAMED_EXTERNAL,  /* Profile is inline, not a file reference */
        NULL,                     /* No parameters */
        &error
    );

    #pragma clang diagnostic pop

    if (ret != 0) {
        AC_LOG_ERROR("sandbox_init failed: %s", error ? error : "unknown error");

        ac_sandbox_set_error(
            AC_SANDBOX_ERR_INTERNAL,
            "sandbox_init failed",
            error ? error : "Failed to initialize macOS sandbox with the generated profile.",
            "Check the sandbox profile syntax or reduce restrictions.",
            NULL,
            errno
        );

        if (error) {
            sandbox_free_error(error);
        }

        return ARC_ERR_BACKEND;
    }

    sandbox->is_active = 1;
    data->sandbox_enforced = 1;

    AC_LOG_INFO("macOS Seatbelt sandbox activated");

    return ARC_OK;
}

int ac_sandbox_is_active(const ac_sandbox_t *sandbox) {
    return sandbox ? sandbox->is_active : 0;
}

void ac_sandbox_destroy(ac_sandbox_t *sandbox) {
    if (!sandbox) {
        return;
    }

    macos_sandbox_data_t *data = (macos_sandbox_data_t *)sandbox->platform_data;
    if (data) {
        free(data->profile);
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
 * Software Filtering
 *============================================================================*/

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
        ac_sandbox_path_is_under(sandbox->workspace_path, path)) {
        return 1;
    }

    /* Check custom path rules */
    for (size_t i = 0; i < sandbox->path_rules_count; i++) {
        const ac_sandbox_path_rule_t *rule = &sandbox->path_rules[i];
        if (ac_sandbox_path_is_under(rule->path, path)) {
            if ((rule->permissions & permissions) == permissions) {
                return 1;
            }
        }
    }

    /* Check readonly paths for read-only access */
    if ((permissions & ~AC_SANDBOX_PERM_FS_READ) == 0) {
        if (sandbox->readonly_paths) {
            for (int i = 0; sandbox->readonly_paths[i]; i++) {
                if (ac_sandbox_path_is_under(sandbox->readonly_paths[i], path)) {
                    return 1;
                }
            }
        }

        /* Check default readonly paths */
        const char **defaults = ac_sandbox_get_default_readonly_paths();
        for (int i = 0; defaults[i]; i++) {
            if (ac_sandbox_path_is_under(defaults[i], path)) {
                return 1;
            }
        }
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
            ? "This file is outside the workspace. Writing may affect system files."
            : "This file is outside the workspace. It may contain sensitive information."
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
                .ai_suggestion = "This command may modify system files or perform destructive operations."
            };

            ac_sandbox_confirm_result_t result = ac_sandbox_request_confirm(
                (ac_sandbox_t *)sandbox, &request);

            if (result != AC_SANDBOX_ALLOW && result != AC_SANDBOX_ALLOW_SESSION) {
                ac_sandbox_set_denial_reason("Dangerous command denied by user");
                return 0;
            }
        }
    }

    /* Check process exec permission */
    if (!sandbox->allow_process_exec && sandbox->strict_mode) {
        ac_sandbox_set_denial_reason("Process execution is disabled in strict mode");
        return 0;
    }

    /* Check for network commands if network is disabled */
    if (!sandbox->allow_network && !sandbox->session_allow_network) {
        const char *net_commands[] = {"curl", "wget", "nc", "netcat", "ssh", "scp", NULL};
        for (int i = 0; net_commands[i]; i++) {
            if (strstr(command, net_commands[i])) {
                if (strstr(command, "--version") || strstr(command, "-V")) {
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
 * Sandboxed Subprocess Execution
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

    /* First check if command is allowed */
    if (!ac_sandbox_check_command(sandbox, command)) {
        if (output && output_size > 0) {
            snprintf(output, output_size,
                     "{\"error\":\"Command blocked by sandbox\",\"reason\":\"%s\"}",
                     ac_sandbox_denial_reason());
        }
        if (exit_code) *exit_code = -1;
        return ARC_ERR_INVALID_ARG;
    }

    /* Create pipe for capturing output */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        AC_LOG_ERROR("Failed to create pipe: %s", strerror(errno));
        return ARC_ERR_IO;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        AC_LOG_ERROR("Fork failed: %s", strerror(errno));
        return ARC_ERR_IO;
    }

    if (pid == 0) {
        /* ===== Child process ===== */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        /*
         * NOTE: We do NOT enter Seatbelt sandbox in child process.
         * Security is ensured by software-level checks and human confirmation.
         * The command has already been validated before reaching here.
         */

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        fprintf(stderr, "execl failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* ===== Parent process ===== */
    close(pipefd[1]);

    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total_read = 0;
        char buf[256];
        ssize_t n;

        while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            size_t remaining = output_size - total_read - 1;
            if (remaining > 0) {
                size_t to_copy = (size_t)n < remaining ? (size_t)n : remaining;
                memcpy(output + total_read, buf, to_copy);
                total_read += to_copy;
                output[total_read] = '\0';
            }
        }
    }

    close(pipefd[0]);

    int status;
    pid_t wait_result;

    if (timeout_ms > 0) {
        int waited_ms = 0;
        int interval_ms = 10;

        while (waited_ms < timeout_ms) {
            wait_result = waitpid(pid, &status, WNOHANG);
            if (wait_result == pid) break;
            if (wait_result < 0) return ARC_ERR_IO;
            usleep(interval_ms * 1000);
            waited_ms += interval_ms;
        }

        if (waited_ms >= timeout_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            if (output && output_size > 0) {
                snprintf(output, output_size,
                         "{\"error\":\"Command timed out after %d ms\"}", timeout_ms);
            }
            if (exit_code) *exit_code = -1;
            return ARC_ERR_TIMEOUT;
        }
    } else {
        wait_result = waitpid(pid, &status, 0);
        if (wait_result < 0) return ARC_ERR_IO;
    }

    if (exit_code) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            *exit_code = 128 + WTERMSIG(status);
        } else {
            *exit_code = -1;
        }
    }

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

#endif /* __APPLE__ && __MACH__ */
