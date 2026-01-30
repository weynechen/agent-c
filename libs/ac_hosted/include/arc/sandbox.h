/**
 * @file sandbox.h
 * @brief Sandbox Abstraction Layer (Hosted Feature)
 *
 * Platform-independent sandbox API for secure command execution.
 * This is a hosted feature requiring OS-level sandboxing capabilities.
 *
 * Platform implementations:
 * - Linux: Landlock (5.13+) + Seccomp, with automatic fallback
 * - macOS: Seatbelt (sandbox-exec)
 * - Windows: Software-based rule filtering (no OS sandbox)
 *
 * Usage:
 *   ac_sandbox_config_t config = {
 *       .workspace_path = "/home/user/project",
 *       .allow_network = 0,
 *       .allow_process_exec = 1,
 *   };
 *   ac_sandbox_t *sb = ac_sandbox_create(&config);
 *   ac_sandbox_enter(sb);  // After this, process is sandboxed
 *   // ... execute untrusted code ...
 *   ac_sandbox_destroy(sb);
 */

#ifndef ARC_HOSTED_SANDBOX_H
#define ARC_HOSTED_SANDBOX_H

#include <arc/platform.h>
#include <arc/error.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Sandbox Permission Flags
 *============================================================================*/

/**
 * @brief File system permission flags
 */
typedef enum {
    AC_SANDBOX_PERM_NONE        = 0x00,
    AC_SANDBOX_PERM_FS_READ     = 0x01,   /* Allow reading files */
    AC_SANDBOX_PERM_FS_WRITE    = 0x02,   /* Allow writing files */
    AC_SANDBOX_PERM_FS_EXECUTE  = 0x04,   /* Allow executing files */
    AC_SANDBOX_PERM_FS_CREATE   = 0x08,   /* Allow creating new files/dirs */
    AC_SANDBOX_PERM_FS_DELETE   = 0x10,   /* Allow deleting files/dirs */
    AC_SANDBOX_PERM_FS_ALL      = 0x1F,   /* All filesystem permissions */
} ac_sandbox_fs_perm_t;

/**
 * @brief Network permission flags
 */
typedef enum {
    AC_SANDBOX_NET_NONE         = 0x00,
    AC_SANDBOX_NET_CONNECT      = 0x01,   /* Allow outbound connections */
    AC_SANDBOX_NET_BIND         = 0x02,   /* Allow binding to ports */
    AC_SANDBOX_NET_ALL          = 0x03,
} ac_sandbox_net_perm_t;

/*============================================================================
 * Sandbox Backend Information
 *============================================================================*/

/**
 * @brief Sandbox backend type
 */
typedef enum {
    AC_SANDBOX_BACKEND_NONE = 0,        /* No sandbox (unsupported platform) */
    AC_SANDBOX_BACKEND_LANDLOCK,        /* Linux Landlock (kernel 5.13+) */
    AC_SANDBOX_BACKEND_SECCOMP,         /* Linux Seccomp only (fallback) */
    AC_SANDBOX_BACKEND_SEATBELT,        /* macOS Seatbelt */
    AC_SANDBOX_BACKEND_SOFTWARE,        /* Software-based filtering */
} ac_sandbox_backend_t;

/**
 * @brief Sandbox capability level
 */
typedef enum {
    AC_SANDBOX_LEVEL_NONE = 0,          /* No sandboxing available */
    AC_SANDBOX_LEVEL_BASIC,             /* Basic path filtering only */
    AC_SANDBOX_LEVEL_MODERATE,          /* Syscall filtering (seccomp) */
    AC_SANDBOX_LEVEL_FULL,              /* Full filesystem isolation */
} ac_sandbox_level_t;

/*============================================================================
 * Path Rule Configuration
 *============================================================================*/

/**
 * @brief Single path access rule
 */
typedef struct {
    const char *path;           /* Path (file or directory) */
    unsigned int permissions;   /* Bitwise OR of ac_sandbox_fs_perm_t */
} ac_sandbox_path_rule_t;

/*============================================================================
 * Sandbox Configuration
 *============================================================================*/

/**
 * @brief Sandbox configuration
 */
typedef struct {
    /* Workspace configuration */
    const char *workspace_path;         /* Primary workspace (full access) */

    /* Additional path rules */
    ac_sandbox_path_rule_t *path_rules; /* Array of path rules */
    size_t path_rules_count;            /* Number of path rules */

    /* System paths (auto-configured if NULL) */
    const char **readonly_paths;        /* NULL-terminated array of read-only paths */

    /* Network permissions */
    int allow_network;                  /* Allow network access */

    /* Process permissions */
    int allow_process_exec;             /* Allow spawning child processes */

    /* Behavior flags */
    int strict_mode;                    /* Deny everything not explicitly allowed */
    int log_violations;                 /* Log access violations */
} ac_sandbox_config_t;

/*============================================================================
 * Sandbox Error Information (AI-Friendly)
 *============================================================================*/

/**
 * @brief Sandbox error categories
 */
typedef enum {
    AC_SANDBOX_ERR_NONE = 0,
    AC_SANDBOX_ERR_NOT_SUPPORTED,       /* Platform doesn't support sandboxing */
    AC_SANDBOX_ERR_KERNEL_VERSION,      /* Kernel version too old */
    AC_SANDBOX_ERR_PERMISSION_DENIED,   /* Cannot set up sandbox (need root?) */
    AC_SANDBOX_ERR_INVALID_CONFIG,      /* Invalid configuration */
    AC_SANDBOX_ERR_PATH_NOT_FOUND,      /* Specified path doesn't exist */
    AC_SANDBOX_ERR_ALREADY_ACTIVE,      /* Sandbox already entered */
    AC_SANDBOX_ERR_SYSCALL_BLOCKED,     /* System call was blocked */
    AC_SANDBOX_ERR_ACCESS_DENIED,       /* File access was denied */
    AC_SANDBOX_ERR_NETWORK_BLOCKED,     /* Network access was blocked */
    AC_SANDBOX_ERR_INTERNAL,            /* Internal error */
} ac_sandbox_error_code_t;

/**
 * @brief AI-friendly error information
 *
 * Provides detailed error context suitable for AI understanding and
 * generating helpful user-facing messages.
 */
typedef struct {
    ac_sandbox_error_code_t code;       /* Error code */
    const char *message;                /* Short error message */
    const char *ai_explanation;         /* Detailed explanation for AI */
    const char *suggestion;             /* Suggested resolution */
    const char *blocked_resource;       /* Resource that was blocked (path/syscall) */
    int platform_errno;                 /* Platform-specific error code */
} ac_sandbox_error_t;

/*============================================================================
 * Sandbox Handle
 *============================================================================*/

typedef struct ac_sandbox ac_sandbox_t;

/*============================================================================
 * Sandbox Lifecycle API
 *============================================================================*/

/**
 * @brief Create a sandbox instance
 *
 * Creates and configures a sandbox but does not activate it.
 * The sandbox is activated by calling ac_sandbox_enter().
 *
 * @param config  Sandbox configuration
 * @return Sandbox handle, or NULL on error (check ac_sandbox_last_error())
 */
ac_sandbox_t *ac_sandbox_create(const ac_sandbox_config_t *config);

/**
 * @brief Enter the sandbox
 *
 * Activates the sandbox restrictions. After this call returns successfully,
 * the process is sandboxed and cannot escape.
 *
 * WARNING: This is typically irreversible for the current process.
 *
 * @param sandbox  Sandbox handle
 * @return ARC_OK on success, error code otherwise
 */
arc_err_t ac_sandbox_enter(ac_sandbox_t *sandbox);

/**
 * @brief Check if sandbox is currently active
 *
 * @param sandbox  Sandbox handle
 * @return 1 if active, 0 if not
 */
int ac_sandbox_is_active(const ac_sandbox_t *sandbox);

/**
 * @brief Destroy sandbox handle
 *
 * Frees resources associated with the sandbox handle.
 * Note: This does NOT exit the sandbox if it has been entered.
 *
 * @param sandbox  Sandbox handle to destroy
 */
void ac_sandbox_destroy(ac_sandbox_t *sandbox);

/*============================================================================
 * Capability Query API
 *============================================================================*/

/**
 * @brief Check if sandboxing is supported on this platform
 *
 * @return 1 if supported, 0 if not
 */
int ac_sandbox_is_supported(void);

/**
 * @brief Get the sandbox backend type
 *
 * @return Backend type enum value
 */
ac_sandbox_backend_t ac_sandbox_get_backend(void);

/**
 * @brief Get the sandbox backend name as string
 *
 * @return Human-readable backend name (e.g., "Landlock", "Seatbelt")
 */
const char *ac_sandbox_backend_name(void);

/**
 * @brief Get the sandbox capability level
 *
 * @return Capability level enum value
 */
ac_sandbox_level_t ac_sandbox_get_level(void);

/**
 * @brief Get detailed platform capability information
 *
 * Returns a JSON-formatted string with platform-specific details.
 * Useful for debugging and AI context.
 *
 * @return JSON string (static, do not free)
 */
const char *ac_sandbox_platform_info(void);

/*============================================================================
 * Error Handling API
 *============================================================================*/

/**
 * @brief Get the last sandbox error
 *
 * Returns detailed error information suitable for AI consumption.
 * The returned pointer is thread-local and valid until the next
 * sandbox operation in the same thread.
 *
 * @return Pointer to error info, or NULL if no error
 */
const ac_sandbox_error_t *ac_sandbox_last_error(void);

/**
 * @brief Clear the last error
 */
void ac_sandbox_clear_error(void);

/**
 * @brief Format error for AI consumption
 *
 * Generates a formatted error message suitable for including in
 * AI prompts or responses.
 *
 * @param error   Error to format
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @return Number of bytes written (excluding null terminator)
 */
size_t ac_sandbox_format_error_for_ai(
    const ac_sandbox_error_t *error,
    char *buffer,
    size_t size
);

/*============================================================================
 * Software Filtering API (for fallback mode)
 *============================================================================*/

/**
 * @brief Check if a path is allowed by current sandbox rules
 *
 * This function works even when hardware sandboxing is not available.
 * It checks the path against configured rules.
 *
 * @param sandbox      Sandbox handle
 * @param path         Path to check
 * @param permissions  Required permissions (AC_SANDBOX_PERM_*)
 * @return 1 if allowed, 0 if denied
 */
int ac_sandbox_check_path(
    const ac_sandbox_t *sandbox,
    const char *path,
    unsigned int permissions
);

/**
 * @brief Check if a command is allowed
 *
 * Checks if a shell command is allowed based on sandbox rules.
 * Analyzes the command for potentially dangerous operations.
 *
 * @param sandbox  Sandbox handle
 * @param command  Command to check
 * @return 1 if allowed, 0 if denied
 */
int ac_sandbox_check_command(
    const ac_sandbox_t *sandbox,
    const char *command
);

/**
 * @brief Get the denial reason for the last check
 *
 * After ac_sandbox_check_path() or ac_sandbox_check_command() returns 0,
 * this function provides the reason.
 *
 * @return Denial reason (static string)
 */
const char *ac_sandbox_denial_reason(void);

/*============================================================================
 * Sandboxed Execution API (Recommended for CLI tools)
 *============================================================================*/

/**
 * @brief Execute a command in a sandboxed subprocess
 *
 * This is the RECOMMENDED way to use sandbox for CLI tools.
 * Instead of sandboxing the main process, this function:
 * 1. Forks a child process
 * 2. Applies sandbox restrictions in the child
 * 3. Executes the command
 * 4. Returns the result to the parent
 *
 * The parent process remains unrestricted (can access network, etc.)
 *
 * @param sandbox   Sandbox configuration (not entered yet)
 * @param command   Command to execute
 * @param output    Output buffer (caller allocated)
 * @param output_size  Size of output buffer
 * @param exit_code    Pointer to receive exit code (can be NULL)
 * @return ARC_OK on success, error code otherwise
 */
arc_err_t ac_sandbox_exec(
    ac_sandbox_t *sandbox,
    const char *command,
    char *output,
    size_t output_size,
    int *exit_code
);

/**
 * @brief Execute a command in sandbox with timeout
 *
 * Same as ac_sandbox_exec but with timeout support.
 *
 * @param sandbox      Sandbox configuration
 * @param command      Command to execute
 * @param output       Output buffer
 * @param output_size  Size of output buffer
 * @param exit_code    Pointer to receive exit code
 * @param timeout_ms   Timeout in milliseconds (0 = no timeout)
 * @return ARC_OK on success, ARC_ERR_TIMEOUT on timeout
 */
arc_err_t ac_sandbox_exec_timeout(
    ac_sandbox_t *sandbox,
    const char *command,
    char *output,
    size_t output_size,
    int *exit_code,
    int timeout_ms
);

/*============================================================================
 * Human-in-the-Loop Confirmation API
 *============================================================================*/

/**
 * @brief Confirmation request type
 */
typedef enum {
    AC_SANDBOX_CONFIRM_COMMAND,         /* Confirm command execution */
    AC_SANDBOX_CONFIRM_PATH_READ,       /* Confirm path read access */
    AC_SANDBOX_CONFIRM_PATH_WRITE,      /* Confirm path write access */
    AC_SANDBOX_CONFIRM_NETWORK,         /* Confirm network access */
    AC_SANDBOX_CONFIRM_DANGEROUS,       /* Confirm dangerous operation */
} ac_sandbox_confirm_type_t;

/**
 * @brief Confirmation request details
 */
typedef struct {
    ac_sandbox_confirm_type_t type;     /* Type of confirmation needed */
    const char *resource;               /* Resource (command, path, etc.) */
    const char *reason;                 /* Why confirmation is needed */
    const char *ai_suggestion;          /* AI's suggestion for the user */
} ac_sandbox_confirm_request_t;

/**
 * @brief Confirmation result
 */
typedef enum {
    AC_SANDBOX_DENY = 0,                /* Deny this request */
    AC_SANDBOX_ALLOW = 1,               /* Allow this request */
    AC_SANDBOX_ALLOW_SESSION = 2,       /* Allow all similar requests this session */
} ac_sandbox_confirm_result_t;

/**
 * @brief Confirmation callback function type
 *
 * This callback is invoked when sandbox needs user confirmation.
 * The implementation should prompt the user and return their decision.
 *
 * @param request   Details about what needs confirmation
 * @param user_data User-provided context
 * @return User's decision
 */
typedef ac_sandbox_confirm_result_t (*ac_sandbox_confirm_fn)(
    const ac_sandbox_confirm_request_t *request,
    void *user_data
);

/**
 * @brief Set confirmation callback for human-in-the-loop
 *
 * When set, the sandbox will call this function instead of automatically
 * denying operations that need review.
 *
 * @param sandbox    Sandbox handle
 * @param callback   Callback function (NULL to disable)
 * @param user_data  User context passed to callback
 */
void ac_sandbox_set_confirm_callback(
    ac_sandbox_t *sandbox,
    ac_sandbox_confirm_fn callback,
    void *user_data
);

/**
 * @brief Request confirmation from user
 *
 * This is called internally by sandbox check functions.
 * Can also be called directly by applications.
 *
 * @param sandbox  Sandbox handle
 * @param request  Confirmation request details
 * @return User's decision (DENY if no callback set)
 */
ac_sandbox_confirm_result_t ac_sandbox_request_confirm(
    ac_sandbox_t *sandbox,
    const ac_sandbox_confirm_request_t *request
);

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/**
 * @brief Create a default sandbox config for a workspace
 */
#define AC_SANDBOX_CONFIG_DEFAULT(workspace) \
    (ac_sandbox_config_t){ \
        .workspace_path = (workspace), \
        .path_rules = NULL, \
        .path_rules_count = 0, \
        .readonly_paths = NULL, \
        .allow_network = 0, \
        .allow_process_exec = 1, \
        .strict_mode = 0, \
        .log_violations = 1, \
    }

/**
 * @brief Create a strict sandbox config
 */
#define AC_SANDBOX_CONFIG_STRICT(workspace) \
    (ac_sandbox_config_t){ \
        .workspace_path = (workspace), \
        .path_rules = NULL, \
        .path_rules_count = 0, \
        .readonly_paths = NULL, \
        .allow_network = 0, \
        .allow_process_exec = 0, \
        .strict_mode = 1, \
        .log_violations = 1, \
    }

#ifdef __cplusplus
}
#endif

#endif /* ARC_HOSTED_SANDBOX_H */
