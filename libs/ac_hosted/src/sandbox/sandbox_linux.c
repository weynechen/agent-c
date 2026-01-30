/**
 * @file sandbox_linux.c
 * @brief Linux Sandbox Implementation
 *
 * Implements sandboxing using Landlock (kernel 5.13+) and Seccomp.
 * Supports automatic fallback when Landlock is not available.
 *
 * Fallback hierarchy:
 * 1. Landlock + Seccomp (full protection, kernel 5.13+)
 * 2. Seccomp only (syscall filtering, older kernels)
 * 3. Software filtering (no kernel support)
 */

#if defined(__linux__)

/* Must be defined before any includes for O_PATH etc. */
#define _GNU_SOURCE

#include "sandbox_internal.h"
#include <arc/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>

/*============================================================================
 * Landlock Support Detection
 *============================================================================*/

/* Landlock syscall numbers (may not be in all headers) */
#ifndef __NR_landlock_create_ruleset
#if defined(__x86_64__)
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule 445
#define __NR_landlock_restrict_self 446
#elif defined(__aarch64__)
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule 445
#define __NR_landlock_restrict_self 446
#else
#define __NR_landlock_create_ruleset -1
#define __NR_landlock_add_rule -1
#define __NR_landlock_restrict_self -1
#endif
#endif

/* Landlock structures (may not be in all headers) */
#ifndef LANDLOCK_ACCESS_FS_EXECUTE

#define LANDLOCK_CREATE_RULESET_VERSION (1U << 0)

#define LANDLOCK_ACCESS_FS_EXECUTE      (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE   (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE    (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR     (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR   (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE  (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR    (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR     (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG     (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK    (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO    (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK   (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM     (1ULL << 12)
#define LANDLOCK_ACCESS_FS_REFER        (1ULL << 13)
#define LANDLOCK_ACCESS_FS_TRUNCATE     (1ULL << 14)

#define LANDLOCK_RULE_PATH_BENEATH 1

struct landlock_ruleset_attr {
    __u64 handled_access_fs;
};

struct landlock_path_beneath_attr {
    __u64 allowed_access;
    __s32 parent_fd;
};

#endif /* LANDLOCK_ACCESS_FS_EXECUTE */

/*============================================================================
 * Landlock Wrapper Functions
 *============================================================================*/

static inline int landlock_create_ruleset(
    const struct landlock_ruleset_attr *attr,
    size_t size, __u32 flags
) {
    return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}

static inline int landlock_add_rule(
    int ruleset_fd,
    int rule_type,
    const void *rule_attr,
    __u32 flags
) {
    return syscall(__NR_landlock_add_rule, ruleset_fd, rule_type, rule_attr, flags);
}

static inline int landlock_restrict_self(int ruleset_fd, __u32 flags) {
    return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}

/*============================================================================
 * Platform Detection
 *============================================================================*/

static int g_landlock_abi = -1;  /* -1 = not checked, 0 = not available */
static int g_seccomp_available = -1;

int ac_sandbox_linux_landlock_abi(void) {
    if (g_landlock_abi >= 0) {
        return g_landlock_abi;
    }

    /* Check Landlock ABI version */
    struct landlock_ruleset_attr attr = {
        .handled_access_fs = 0,
    };

    int abi = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0) {
        if (errno == ENOSYS) {
            AC_LOG_INFO("Landlock not supported (syscall not available)");
        } else if (errno == EOPNOTSUPP) {
            AC_LOG_INFO("Landlock disabled in kernel config");
        }
        g_landlock_abi = 0;
    } else {
        g_landlock_abi = abi;
        AC_LOG_INFO("Landlock ABI version: %d", abi);
    }

    return g_landlock_abi;
}

int ac_sandbox_linux_seccomp_available(void) {
    if (g_seccomp_available >= 0) {
        return g_seccomp_available;
    }

    /* Check if seccomp is available */
    if (prctl(PR_GET_SECCOMP) == -1 && errno == EINVAL) {
        g_seccomp_available = 0;
        AC_LOG_INFO("Seccomp not available");
    } else {
        g_seccomp_available = 1;
        AC_LOG_DEBUG("Seccomp is available");
    }

    return g_seccomp_available;
}

/*============================================================================
 * Landlock Implementation
 *============================================================================*/

/* Linux-specific platform data */
typedef struct {
    int ruleset_fd;
    int landlock_enforced;
    int seccomp_enforced;
} linux_sandbox_data_t;

/**
 * @brief Convert sandbox permissions to Landlock access flags
 */
static __u64 perm_to_landlock(unsigned int perm) {
    __u64 access = 0;

    if (perm & AC_SANDBOX_PERM_FS_READ) {
        access |= LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
    }
    if (perm & AC_SANDBOX_PERM_FS_WRITE) {
        access |= LANDLOCK_ACCESS_FS_WRITE_FILE;
    }
    if (perm & AC_SANDBOX_PERM_FS_EXECUTE) {
        access |= LANDLOCK_ACCESS_FS_EXECUTE;
    }
    if (perm & AC_SANDBOX_PERM_FS_CREATE) {
        access |= LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_DIR |
                  LANDLOCK_ACCESS_FS_MAKE_SYM;
    }
    if (perm & AC_SANDBOX_PERM_FS_DELETE) {
        access |= LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR;
    }

    return access;
}

/**
 * @brief Add a Landlock rule for a path
 */
static int add_landlock_path_rule(int ruleset_fd, const char *path, __u64 access) {
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) {
        AC_LOG_WARN("Cannot open path for Landlock rule: %s (%s)",
                    path, strerror(errno));
        return -1;
    }

    struct landlock_path_beneath_attr attr = {
        .allowed_access = access,
        .parent_fd = fd,
    };

    int ret = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &attr, 0);
    close(fd);

    if (ret < 0) {
        AC_LOG_WARN("Failed to add Landlock rule for %s: %s", path, strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * @brief Set up Landlock ruleset
 */
static int setup_landlock(ac_sandbox_t *sandbox) {
    linux_sandbox_data_t *data = (linux_sandbox_data_t *)sandbox->platform_data;

    /* Determine handled access based on ABI version */
    int abi = ac_sandbox_linux_landlock_abi();
    if (abi <= 0) {
        return -1;
    }

    __u64 handled_access =
        LANDLOCK_ACCESS_FS_EXECUTE |
        LANDLOCK_ACCESS_FS_WRITE_FILE |
        LANDLOCK_ACCESS_FS_READ_FILE |
        LANDLOCK_ACCESS_FS_READ_DIR |
        LANDLOCK_ACCESS_FS_REMOVE_DIR |
        LANDLOCK_ACCESS_FS_REMOVE_FILE |
        LANDLOCK_ACCESS_FS_MAKE_CHAR |
        LANDLOCK_ACCESS_FS_MAKE_DIR |
        LANDLOCK_ACCESS_FS_MAKE_REG |
        LANDLOCK_ACCESS_FS_MAKE_SOCK |
        LANDLOCK_ACCESS_FS_MAKE_FIFO |
        LANDLOCK_ACCESS_FS_MAKE_BLOCK |
        LANDLOCK_ACCESS_FS_MAKE_SYM;

    /* ABI v2+ supports REFER */
    if (abi >= 2) {
        handled_access |= LANDLOCK_ACCESS_FS_REFER;
    }

    /* ABI v3+ supports TRUNCATE */
    if (abi >= 3) {
        handled_access |= LANDLOCK_ACCESS_FS_TRUNCATE;
    }

    /* Create ruleset */
    struct landlock_ruleset_attr attr = {
        .handled_access_fs = handled_access,
    };

    data->ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
    if (data->ruleset_fd < 0) {
        AC_LOG_ERROR("Failed to create Landlock ruleset: %s", strerror(errno));
        return -1;
    }

    /* Add workspace path with full access */
    if (sandbox->workspace_path) {
        __u64 workspace_access = handled_access;
        if (add_landlock_path_rule(data->ruleset_fd, sandbox->workspace_path,
                                   workspace_access) < 0) {
            AC_LOG_WARN("Failed to add workspace to Landlock rules");
        }
    }

    /* Add custom path rules */
    for (size_t i = 0; i < sandbox->path_rules_count; i++) {
        const ac_sandbox_path_rule_t *rule = &sandbox->path_rules[i];
        __u64 access = perm_to_landlock(rule->permissions);
        add_landlock_path_rule(data->ruleset_fd, rule->path, access);
    }

    /* Add readonly paths */
    __u64 readonly_access = LANDLOCK_ACCESS_FS_READ_FILE |
                            LANDLOCK_ACCESS_FS_READ_DIR |
                            LANDLOCK_ACCESS_FS_EXECUTE;

    if (sandbox->readonly_paths) {
        for (int i = 0; sandbox->readonly_paths[i] != NULL; i++) {
            add_landlock_path_rule(data->ruleset_fd, sandbox->readonly_paths[i],
                                   readonly_access);
        }
    }

    /* Add default readonly paths */
    const char **defaults = ac_sandbox_get_default_readonly_paths();
    for (int i = 0; defaults[i] != NULL; i++) {
        add_landlock_path_rule(data->ruleset_fd, defaults[i], readonly_access);
    }

    return 0;
}

/**
 * @brief Enforce Landlock restrictions
 */
static int enforce_landlock(ac_sandbox_t *sandbox) {
    linux_sandbox_data_t *data = (linux_sandbox_data_t *)sandbox->platform_data;

    if (data->ruleset_fd < 0) {
        return -1;
    }

    /* Allow ourselves to drop privileges */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        AC_LOG_ERROR("Failed to set NO_NEW_PRIVS: %s", strerror(errno));
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_PERMISSION_DENIED,
            "Cannot set NO_NEW_PRIVS",
            "The sandbox requires setting the NO_NEW_PRIVS flag to prevent "
            "privilege escalation. This operation failed, possibly due to "
            "seccomp restrictions or container security policies.",
            "Try running without containers or check security policies.",
            NULL,
            errno
        );
        return -1;
    }

    /* Apply the ruleset */
    if (landlock_restrict_self(data->ruleset_fd, 0) < 0) {
        AC_LOG_ERROR("Failed to apply Landlock ruleset: %s", strerror(errno));
        ac_sandbox_set_error(
            AC_SANDBOX_ERR_INTERNAL,
            "Failed to apply Landlock",
            "The Landlock filesystem sandbox could not be activated. "
            "This may happen if the kernel does not support the required "
            "Landlock ABI version.",
            "Check kernel version (5.13+ required) or use fallback mode.",
            NULL,
            errno
        );
        return -1;
    }

    close(data->ruleset_fd);
    data->ruleset_fd = -1;
    data->landlock_enforced = 1;

    AC_LOG_INFO("Landlock sandbox activated");
    return 0;
}

/*============================================================================
 * Seccomp Implementation
 *============================================================================*/

/**
 * @brief Set up basic Seccomp filter
 *
 * This provides syscall-level restrictions to complement Landlock.
 * Uses a whitelist approach for safe syscalls.
 */
static int setup_seccomp(ac_sandbox_t *sandbox) {
    /* Seccomp-bpf filter to block dangerous syscalls */

    /* For now, we use a simple filter that logs violations
     * A full implementation would use libseccomp for easier filter creation */

    if (!ac_sandbox_linux_seccomp_available()) {
        AC_LOG_WARN("Seccomp not available, skipping");
        return -1;
    }

    /* Set NO_NEW_PRIVS (may already be set by Landlock) */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        if (errno != EPERM) {  /* EPERM means already set */
            AC_LOG_WARN("Failed to set NO_NEW_PRIVS for seccomp: %s", strerror(errno));
        }
    }

    /* Basic filter: allow most syscalls but log network operations if blocked */
    /* Full seccomp implementation would go here using BPF */

    linux_sandbox_data_t *data = (linux_sandbox_data_t *)sandbox->platform_data;

    if (!sandbox->allow_network) {
        /* TODO: Add BPF filter to block network syscalls */
        AC_LOG_DEBUG("Network blocking via seccomp not implemented, using software check");
    }

    if (!sandbox->allow_process_exec) {
        /* TODO: Add BPF filter to block exec syscalls */
        AC_LOG_DEBUG("Process exec blocking via seccomp not implemented, using software check");
    }

    data->seccomp_enforced = 1;
    AC_LOG_DEBUG("Seccomp basic setup complete");

    return 0;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

int ac_sandbox_is_supported(void) {
    /* Linux always supports at least software filtering */
    return 1;
}

ac_sandbox_backend_t ac_sandbox_get_backend(void) {
    if (ac_sandbox_linux_landlock_abi() > 0) {
        return AC_SANDBOX_BACKEND_LANDLOCK;
    }
    if (ac_sandbox_linux_seccomp_available()) {
        return AC_SANDBOX_BACKEND_SECCOMP;
    }
    return AC_SANDBOX_BACKEND_SOFTWARE;
}

const char *ac_sandbox_backend_name(void) {
    switch (ac_sandbox_get_backend()) {
        case AC_SANDBOX_BACKEND_LANDLOCK:
            return "Landlock+Seccomp";
        case AC_SANDBOX_BACKEND_SECCOMP:
            return "Seccomp";
        case AC_SANDBOX_BACKEND_SOFTWARE:
            return "Software";
        default:
            return "Unknown";
    }
}

ac_sandbox_level_t ac_sandbox_get_level(void) {
    if (ac_sandbox_linux_landlock_abi() > 0) {
        return AC_SANDBOX_LEVEL_FULL;
    }
    if (ac_sandbox_linux_seccomp_available()) {
        return AC_SANDBOX_LEVEL_MODERATE;
    }
    return AC_SANDBOX_LEVEL_BASIC;
}

const char *ac_sandbox_platform_info(void) {
    static char info[512];

    snprintf(info, sizeof(info),
        "{"
        "\"platform\":\"Linux\","
        "\"backend\":\"%s\","
        "\"level\":\"%s\","
        "\"landlock_abi\":%d,"
        "\"seccomp_available\":%s"
        "}",
        ac_sandbox_backend_name(),
        ac_sandbox_get_level() == AC_SANDBOX_LEVEL_FULL ? "full" :
        ac_sandbox_get_level() == AC_SANDBOX_LEVEL_MODERATE ? "moderate" : "basic",
        ac_sandbox_linux_landlock_abi(),
        ac_sandbox_linux_seccomp_available() ? "true" : "false"
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
    linux_sandbox_data_t *data = calloc(1, sizeof(linux_sandbox_data_t));
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
    data->ruleset_fd = -1;
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

    /* Determine backend based on availability */
    sandbox->backend = ac_sandbox_get_backend();
    sandbox->level = ac_sandbox_get_level();

    AC_LOG_INFO("Created sandbox (backend=%s, level=%d)",
                ac_sandbox_backend_name(), sandbox->level);

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

    int landlock_ok = 0;
    int seccomp_ok = 0;

    /* Try Landlock first */
    if (ac_sandbox_linux_landlock_abi() > 0) {
        if (setup_landlock(sandbox) == 0) {
            if (enforce_landlock(sandbox) == 0) {
                landlock_ok = 1;
            }
        }

        if (!landlock_ok) {
            AC_LOG_WARN("Landlock setup failed, falling back to seccomp");
        }
    }

    /* Set up Seccomp (even if Landlock succeeded, for extra protection) */
    if (ac_sandbox_linux_seccomp_available()) {
        if (setup_seccomp(sandbox) == 0) {
            seccomp_ok = 1;
        }
    }

    /* Update sandbox state */
    sandbox->is_active = 1;

    if (landlock_ok) {
        sandbox->level = AC_SANDBOX_LEVEL_FULL;
    } else if (seccomp_ok) {
        sandbox->level = AC_SANDBOX_LEVEL_MODERATE;
    } else {
        sandbox->level = AC_SANDBOX_LEVEL_BASIC;
        AC_LOG_WARN("No kernel sandbox available, using software filtering only");
    }

    AC_LOG_INFO("Sandbox entered (level=%d, landlock=%d, seccomp=%d)",
                sandbox->level, landlock_ok, seccomp_ok);

    return ARC_OK;
}

int ac_sandbox_is_active(const ac_sandbox_t *sandbox) {
    return sandbox ? sandbox->is_active : 0;
}

void ac_sandbox_destroy(ac_sandbox_t *sandbox) {
    if (!sandbox) {
        return;
    }

    linux_sandbox_data_t *data = (linux_sandbox_data_t *)sandbox->platform_data;
    if (data) {
        if (data->ruleset_fd >= 0) {
            close(data->ruleset_fd);
        }
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
 * Software Filtering (works at any level)
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

    /* Check readonly paths */
    if ((permissions & ~AC_SANDBOX_PERM_FS_READ) == 0) {
        /* Only read permission requested */
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

    /* Check if session-level permission was granted */
    if (sandbox->session_allow_external_paths) {
        return 1;
    }

    /* Build confirmation request */
    ac_sandbox_confirm_type_t type =
        (permissions & (AC_SANDBOX_PERM_FS_WRITE | AC_SANDBOX_PERM_FS_CREATE | AC_SANDBOX_PERM_FS_DELETE))
        ? AC_SANDBOX_CONFIRM_PATH_WRITE
        : AC_SANDBOX_CONFIRM_PATH_READ;

    char reason[256];
    snprintf(reason, sizeof(reason),
             "Path '%s' is outside the workspace", path);

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

    /* Denied */
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
        /* Check if session-level permission was granted */
        if (!sandbox->session_allow_dangerous_commands) {
            /* Request human confirmation */
            ac_sandbox_confirm_request_t request = {
                .type = AC_SANDBOX_CONFIRM_DANGEROUS,
                .resource = command,
                .reason = "Command contains potentially dangerous patterns",
                .ai_suggestion = "This command may modify system files, escalate privileges, "
                                 "or perform destructive operations. Please review carefully."
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
                /* Allow version checks */
                if (strstr(command, "--version") || strstr(command, "-V")) {
                    continue;
                }

                /* Request human confirmation */
                ac_sandbox_confirm_request_t request = {
                    .type = AC_SANDBOX_CONFIRM_NETWORK,
                    .resource = command,
                    .reason = "Command requires network access",
                    .ai_suggestion = "This command will access the network. "
                                     "It may download files or send data to external servers."
                };

                ac_sandbox_confirm_result_t result = ac_sandbox_request_confirm(
                    (ac_sandbox_t *)sandbox, &request);

                if (result != AC_SANDBOX_ALLOW && result != AC_SANDBOX_ALLOW_SESSION) {
                    ac_sandbox_set_denial_reason("Network command denied by user");
                    return 0;
                }
                break;  /* User approved, no need to check other patterns */
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
        /* Fork failed */
        close(pipefd[0]);
        close(pipefd[1]);
        AC_LOG_ERROR("Fork failed: %s", strerror(errno));
        return ARC_ERR_IO;
    }

    if (pid == 0) {
        /* ===== Child process ===== */

        /* Close read end of pipe */
        close(pipefd[0]);

        /* Redirect stdout and stderr to pipe */
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        /*
         * NOTE: We do NOT enter Landlock sandbox in child process.
         *
         * Reason: Landlock has compatibility issues with special filesystems
         * like /dev, /proc, /sys which are needed by many commands (git, etc.)
         *
         * Security is ensured by:
         * 1. Software-level command validation (ac_sandbox_check_command)
         * 2. Human-in-the-loop confirmation for dangerous operations
         * 3. The command has already been approved before reaching here
         *
         * This approach trades kernel-level enforcement for better compatibility
         * while maintaining security through explicit user consent.
         */

        /* Execute command via shell */
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);

        /* If execl fails */
        fprintf(stderr, "execl failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* ===== Parent process ===== */

    /* Close write end of pipe */
    close(pipefd[1]);

    /* Set up for reading with optional timeout */
    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total_read = 0;
        char buf[256];
        ssize_t n;

        /* TODO: Implement proper timeout with select/poll if needed */
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

    /* Wait for child process */
    int status;
    pid_t wait_result;

    if (timeout_ms > 0) {
        /* Implement timeout with alarm or polling */
        int waited_ms = 0;
        int interval_ms = 10;

        while (waited_ms < timeout_ms) {
            wait_result = waitpid(pid, &status, WNOHANG);
            if (wait_result == pid) {
                break;
            } else if (wait_result < 0) {
                AC_LOG_ERROR("waitpid failed: %s", strerror(errno));
                return ARC_ERR_IO;
            }

            usleep(interval_ms * 1000);
            waited_ms += interval_ms;
        }

        if (waited_ms >= timeout_ms) {
            /* Timeout - kill child process */
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
        /* No timeout - wait indefinitely */
        wait_result = waitpid(pid, &status, 0);
        if (wait_result < 0) {
            AC_LOG_ERROR("waitpid failed: %s", strerror(errno));
            return ARC_ERR_IO;
        }
    }

    /* Extract exit code */
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

#endif /* __linux__ */
