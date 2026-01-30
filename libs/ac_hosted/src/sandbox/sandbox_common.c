/**
 * @file sandbox_common.c
 * @brief Sandbox Common Utilities
 *
 * Platform-independent helper functions for sandbox implementations.
 */

#include <arc/sandbox.h>
#include <arc/log.h>
#include "sandbox_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*============================================================================
 * Thread-Local Error Storage
 *============================================================================*/

#if defined(_WIN32)
    #define THREAD_LOCAL __declspec(thread)
#else
    #define THREAD_LOCAL __thread
#endif

static THREAD_LOCAL ac_sandbox_error_t g_last_error = {0};
static THREAD_LOCAL char g_error_message[256] = {0};
static THREAD_LOCAL char g_error_explanation[512] = {0};
static THREAD_LOCAL char g_error_suggestion[256] = {0};
static THREAD_LOCAL char g_blocked_resource[256] = {0};
static THREAD_LOCAL char g_denial_reason[256] = {0};

/*============================================================================
 * Error Handling Implementation
 *============================================================================*/

void ac_sandbox_set_error(
    ac_sandbox_error_code_t code,
    const char *message,
    const char *ai_explanation,
    const char *suggestion,
    const char *blocked_resource,
    int platform_errno
) {
    g_last_error.code = code;
    g_last_error.platform_errno = platform_errno;

    /* Copy strings to thread-local buffers */
    if (message) {
        strncpy(g_error_message, message, sizeof(g_error_message) - 1);
        g_error_message[sizeof(g_error_message) - 1] = '\0';
        g_last_error.message = g_error_message;
    } else {
        g_last_error.message = NULL;
    }

    if (ai_explanation) {
        strncpy(g_error_explanation, ai_explanation, sizeof(g_error_explanation) - 1);
        g_error_explanation[sizeof(g_error_explanation) - 1] = '\0';
        g_last_error.ai_explanation = g_error_explanation;
    } else {
        g_last_error.ai_explanation = NULL;
    }

    if (suggestion) {
        strncpy(g_error_suggestion, suggestion, sizeof(g_error_suggestion) - 1);
        g_error_suggestion[sizeof(g_error_suggestion) - 1] = '\0';
        g_last_error.suggestion = g_error_suggestion;
    } else {
        g_last_error.suggestion = NULL;
    }

    if (blocked_resource) {
        strncpy(g_blocked_resource, blocked_resource, sizeof(g_blocked_resource) - 1);
        g_blocked_resource[sizeof(g_blocked_resource) - 1] = '\0';
        g_last_error.blocked_resource = g_blocked_resource;
    } else {
        g_last_error.blocked_resource = NULL;
    }
}

const ac_sandbox_error_t *ac_sandbox_last_error(void) {
    if (g_last_error.code == AC_SANDBOX_ERR_NONE) {
        return NULL;
    }
    return &g_last_error;
}

void ac_sandbox_clear_error(void) {
    memset(&g_last_error, 0, sizeof(g_last_error));
    g_error_message[0] = '\0';
    g_error_explanation[0] = '\0';
    g_error_suggestion[0] = '\0';
    g_blocked_resource[0] = '\0';
}

size_t ac_sandbox_format_error_for_ai(
    const ac_sandbox_error_t *error,
    char *buffer,
    size_t size
) {
    if (!error || !buffer || size == 0) {
        return 0;
    }

    int written = snprintf(buffer, size,
        "Sandbox Error:\n"
        "  Code: %d\n"
        "  Message: %s\n"
        "  Explanation: %s\n"
        "  Suggestion: %s\n"
        "  Blocked Resource: %s\n",
        error->code,
        error->message ? error->message : "Unknown error",
        error->ai_explanation ? error->ai_explanation : "No details available",
        error->suggestion ? error->suggestion : "No suggestion available",
        error->blocked_resource ? error->blocked_resource : "N/A"
    );

    return (written > 0 && (size_t)written < size) ? (size_t)written : size - 1;
}

/*============================================================================
 * Denial Reason Storage
 *============================================================================*/

void ac_sandbox_set_denial_reason(const char *reason) {
    if (reason) {
        strncpy(g_denial_reason, reason, sizeof(g_denial_reason) - 1);
        g_denial_reason[sizeof(g_denial_reason) - 1] = '\0';
    } else {
        g_denial_reason[0] = '\0';
    }
}

const char *ac_sandbox_denial_reason(void) {
    return g_denial_reason[0] ? g_denial_reason : "Access denied by sandbox policy , tell user how to excute manual";
}

/*============================================================================
 * Path Utilities
 *============================================================================*/

/**
 * @brief Normalize a path (resolve . and .., remove trailing slashes)
 *
 * @param path     Input path
 * @param buffer   Output buffer
 * @param size     Buffer size
 * @return 0 on success, -1 on error
 */
int ac_sandbox_normalize_path(const char *path, char *buffer, size_t size) {
    if (!path || !buffer || size == 0) {
        return -1;
    }

#if defined(_WIN32)
    /* Windows: use _fullpath */
    if (_fullpath(buffer, path, size) == NULL) {
        return -1;
    }
#else
    /* POSIX: use realpath if file exists, otherwise manual normalization */
    char *resolved = realpath(path, NULL);
    if (resolved) {
        strncpy(buffer, resolved, size - 1);
        buffer[size - 1] = '\0';
        free(resolved);
    } else if (errno == ENOENT) {
        /* Path doesn't exist - do basic normalization */
        strncpy(buffer, path, size - 1);
        buffer[size - 1] = '\0';
    } else {
        return -1;
    }
#endif

    return 0;
}

/**
 * @brief Check if child path is under parent path
 *
 * @param parent  Parent directory path
 * @param child   Child path to check
 * @return 1 if child is under parent, 0 otherwise
 */
int ac_sandbox_path_is_under(const char *parent, const char *child) {
    if (!parent || !child) {
        return 0;
    }

    char norm_parent[4096];
    char norm_child[4096];

    if (ac_sandbox_normalize_path(parent, norm_parent, sizeof(norm_parent)) < 0) {
        return 0;
    }
    if (ac_sandbox_normalize_path(child, norm_child, sizeof(norm_child)) < 0) {
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

/*============================================================================
 * Dangerous Command Detection
 *============================================================================*/

/* Patterns that indicate dangerous commands */
static const char *g_dangerous_patterns[] = {
    /* Destructive file operations */
    "rm -rf /",
    "rm -rf /*",
    "rm -fr /",
    "rm -fr /*",
    "> /dev/sd",
    "> /dev/nv",
    "dd if=",
    "mkfs",

    /* Privilege escalation */
    "sudo ",
    "su -",
    "su root",
    "doas ",

    /* Permission changes */
    "chmod 777 /",
    "chmod -R 777 /",
    "chown -R",

    /* System modifications */
    "systemctl ",
    "service ",
    "/etc/init.d/",

    /* Network exfiltration */
    "curl ",
    "wget ",
    "nc -",
    "netcat ",

    /* Shell fork bomb */
    ":(){ :|:& };:",

    /* Dangerous redirections */
    "> /etc/",
    ">> /etc/",

    NULL
};

/* Commands that are safe even if they match dangerous patterns */
static const char *g_safe_overrides[] = {
    "curl --version",
    "wget --version",
    NULL
};

int ac_sandbox_is_command_dangerous(const char *command) {
    if (!command) {
        return 0;
    }

    /* Check safe overrides first */
    for (int i = 0; g_safe_overrides[i] != NULL; i++) {
        if (strstr(command, g_safe_overrides[i]) != NULL) {
            return 0;
        }
    }

    /* Check dangerous patterns */
    for (int i = 0; g_dangerous_patterns[i] != NULL; i++) {
        if (strstr(command, g_dangerous_patterns[i]) != NULL) {
            AC_LOG_WARN("Dangerous command pattern detected: %s", g_dangerous_patterns[i]);
            return 1;
        }
    }

    return 0;
}

/*============================================================================
 * Default Readonly Paths
 *============================================================================*/

/* System paths that should be readable in most sandboxes */
static const char *g_default_readonly_paths[] = {
#if defined(__linux__)
    /* Executable paths */
    "/bin",
    "/sbin",
    "/usr/bin",
    "/usr/sbin",
    "/usr/local/bin",

    /* Libraries */
    "/lib",
    "/lib64",
    "/lib32",
    "/usr/lib",
    "/usr/lib64",
    "/usr/lib32",
    "/usr/local/lib",

    /* Shared data */
    "/usr/share",
    "/usr/local/share",

    /* Dynamic linker config */
    "/etc/ld.so.cache",
    "/etc/ld.so.conf",
    "/etc/ld.so.conf.d",

    /* Timezone and locale */
    "/etc/localtime",
    "/etc/timezone",
    "/etc/locale.gen",
    "/etc/locale.conf",

    /* SSL certificates */
    "/etc/ssl/certs",
    "/etc/ca-certificates",
    "/etc/pki",

    /* Proc filesystem (for self info) */
    "/proc/self",

    /* Device files */
    "/dev/null",
    "/dev/zero",
    "/dev/urandom",
    "/dev/random",
    "/dev/tty",
    "/dev/pts",

    /* Tmp for temporary files */
    "/tmp",
    "/var/tmp",
#elif defined(__APPLE__)
    "/usr/lib",
    "/usr/share",
    "/System/Library",
    "/Library/Frameworks",
    "/etc/ssl/certs",
    "/dev/null",
    "/dev/zero",
    "/dev/urandom",
    "/dev/random",
#endif
    NULL
};

const char **ac_sandbox_get_default_readonly_paths(void) {
    return g_default_readonly_paths;
}

/*============================================================================
 * Human-in-the-Loop Confirmation
 *============================================================================*/

void ac_sandbox_set_confirm_callback(
    ac_sandbox_t *sandbox,
    ac_sandbox_confirm_fn callback,
    void *user_data
) {
    if (!sandbox) return;
    sandbox->confirm_callback = callback;
    sandbox->confirm_user_data = user_data;
}

ac_sandbox_confirm_result_t ac_sandbox_request_confirm(
    ac_sandbox_t *sandbox,
    const ac_sandbox_confirm_request_t *request
) {
    if (!sandbox || !request) {
        return AC_SANDBOX_DENY;
    }

    /* If no callback is set, deny by default */
    if (!sandbox->confirm_callback) {
        AC_LOG_WARN("Sandbox: no confirm callback, auto-deny: %s",
                    request->resource ? request->resource : "(null)");
        return AC_SANDBOX_DENY;
    }

    /* Call the user-provided callback */
    ac_sandbox_confirm_result_t result = sandbox->confirm_callback(
        request, sandbox->confirm_user_data);

    /* Handle session-level permissions */
    if (result == AC_SANDBOX_ALLOW_SESSION) {
        switch (request->type) {
            case AC_SANDBOX_CONFIRM_DANGEROUS:
                sandbox->session_allow_dangerous_commands = 1;
                break;
            case AC_SANDBOX_CONFIRM_PATH_READ:
            case AC_SANDBOX_CONFIRM_PATH_WRITE:
                sandbox->session_allow_external_paths = 1;
                break;
            case AC_SANDBOX_CONFIRM_NETWORK:
                sandbox->session_allow_network = 1;
                break;
            default:
                break;
        }
    }

    return result;
}

/**
 * @brief Get confirmation type as string
 */
const char *ac_sandbox_confirm_type_str(ac_sandbox_confirm_type_t type) {
    switch (type) {
        case AC_SANDBOX_CONFIRM_COMMAND: return "command";
        case AC_SANDBOX_CONFIRM_PATH_READ: return "path_read";
        case AC_SANDBOX_CONFIRM_PATH_WRITE: return "path_write";
        case AC_SANDBOX_CONFIRM_NETWORK: return "network";
        case AC_SANDBOX_CONFIRM_DANGEROUS: return "dangerous";
        default: return "unknown";
    }
}

/*============================================================================
 * Internal API Declaration (for platform implementations)
 *============================================================================*/

/* These are implemented here and used by platform-specific code */

void ac_sandbox_set_error(
    ac_sandbox_error_code_t code,
    const char *message,
    const char *ai_explanation,
    const char *suggestion,
    const char *blocked_resource,
    int platform_errno
);

void ac_sandbox_set_denial_reason(const char *reason);
int ac_sandbox_normalize_path(const char *path, char *buffer, size_t size);
int ac_sandbox_path_is_under(const char *parent, const char *child);
int ac_sandbox_is_command_dangerous(const char *command);
const char **ac_sandbox_get_default_readonly_paths(void);
const char *ac_sandbox_confirm_type_str(ac_sandbox_confirm_type_t type);
