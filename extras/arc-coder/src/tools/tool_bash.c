/**
 * @file tool_bash.c
 * @brief Bash Tool Implementation
 */

#include "code_tools.h"
#include <arc/sandbox.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*============================================================================
 * Static State
 *============================================================================*/

static char g_workspace[4096] = ".";
static int g_safe_mode = 0;
static ac_sandbox_t *g_sandbox = NULL;

/*============================================================================
 * Configuration Functions
 *============================================================================*/

void code_tools_set_workspace(const char *path) {
    if (path) {
        strncpy(g_workspace, path, sizeof(g_workspace) - 1);
        g_workspace[sizeof(g_workspace) - 1] = '\0';
    }
}

const char *code_tools_get_workspace(void) {
    return g_workspace;
}

void code_tools_set_safe_mode(int enabled) {
    g_safe_mode = enabled;
}

void code_tools_set_sandbox(struct ac_sandbox *sandbox) {
    g_sandbox = sandbox;
}

struct ac_sandbox *code_tools_get_sandbox(void) {
    return g_sandbox;
}

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char g_result_buffer[65536];

static const char *json_result(cJSON *json) {
    if (!json) {
        return "{\"error\": \"Failed to create response\"}";
    }

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!str) {
        return "{\"error\": \"Failed to serialize response\"}";
    }

    size_t len = strlen(str);
    if (len >= sizeof(g_result_buffer)) {
        len = sizeof(g_result_buffer) - 1;
    }
    memcpy(g_result_buffer, str, len);
    g_result_buffer[len] = '\0';

    free(str);
    return g_result_buffer;
}

static const char *json_error(const char *msg) {
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "error", msg);
    }
    return json_result(json);
}

/* Check if command is dangerous */
static int is_dangerous_command(const char *cmd) {
    const char *dangerous[] = {
        "rm -rf /", "rm -fr /", "rm -rf /*", "rm -fr /*",
        "sudo rm -rf", "sudo rm -fr",
        "chmod 777 /", "chmod -R 777 /",
        "> /dev/sda", "> /dev/hda",
        "mkfs.", "dd if=/dev/zero",
        ":(){ :|:& };:",  /* Fork bomb */
        "mv /* ", "mv / ",
        "chmod 000 /",
        NULL
    };

    for (int i = 0; dangerous[i] != NULL; i++) {
        if (strstr(cmd, dangerous[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/*============================================================================
 * Bash Tool Implementation
 *============================================================================*/

const char *bash(
    const char *command,
    const char *workdir,
    int timeout,
    const char *description
) {
    if (!command || strlen(command) == 0) {
        return json_error("command parameter is required");
    }

    /* Default values */
    const char *cwd = workdir && strlen(workdir) > 0 ? workdir : g_workspace;
    int timeout_ms = timeout > 0 ? timeout : 120000;

    /* Safety check */
    if (g_safe_mode && is_dangerous_command(command)) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Dangerous command blocked in safe mode");
        cJSON_AddStringToObject(json, "command", command);
        cJSON_AddStringToObject(json, "hint",
            "This command was blocked because it matches a dangerous pattern. "
            "Disable safe mode if you need to run this command.");
        return json_result(json);
    }

    char *result = NULL;
    int exit_code = 0;

    /* Sandbox execution if available */
    if (g_sandbox) {
        size_t result_cap = 65536;
        result = malloc(result_cap);
        if (!result) {
            return json_error("Memory allocation failed");
        }
        result[0] = '\0';

        arc_err_t err = ac_sandbox_exec(g_sandbox, command, result, result_cap, &exit_code);

        if (err == ARC_ERR_INVALID_ARG) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "Command blocked by sandbox");
            cJSON_AddStringToObject(json, "command", command);
            cJSON_AddStringToObject(json, "reason", ac_sandbox_denial_reason());
            free(result);
            return json_result(json);
        } else if (err == ARC_ERR_TIMEOUT) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "Command timed out");
            cJSON_AddStringToObject(json, "command", command);
            cJSON_AddNumberToObject(json, "timeout_ms", timeout_ms);
            free(result);
            return json_result(json);
        } else if (err != ARC_OK) {
            free(result);
            return json_error("Failed to execute command in sandbox");
        }
    } else {
        /* Non-sandbox execution */
        /* Build command with cd */
        char full_cmd[8192];
        snprintf(full_cmd, sizeof(full_cmd), "cd \"%s\" && %s", cwd, command);

        /* Allocate output buffer */
        size_t result_cap = 65536;
        size_t result_len = 0;
        result = malloc(result_cap);
        if (!result) {
            return json_error("Memory allocation failed");
        }
        result[0] = '\0';

        /* Execute */
        FILE *fp = popen(full_cmd, "r");
        if (!fp) {
            free(result);
            return json_error("Failed to execute command");
        }

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            size_t len = strlen(buffer);
            if (result_len + len + 1 > result_cap) {
                result_cap *= 2;
                char *new_result = realloc(result, result_cap);
                if (!new_result) {
                    free(result);
                    pclose(fp);
                    return json_error("Memory allocation failed");
                }
                result = new_result;
            }
            strcpy(result + result_len, buffer);
            result_len += len;
        }

        int status = pclose(fp);
        exit_code = WEXITSTATUS(status);
    }

    /* Truncate if too long */
    const long unsigned int MAX_OUTPUT = 30000;
    int truncated = 0;
    if (strlen(result) > MAX_OUTPUT) {
        result[MAX_OUTPUT] = '\0';
        truncated = 1;
    }

    /* Build response */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "command", command);
    cJSON_AddNumberToObject(json, "exit_code", exit_code);
    cJSON_AddStringToObject(json, "output", result);
    if (description && strlen(description) > 0) {
        cJSON_AddStringToObject(json, "description", description);
    }
    if (truncated) {
        cJSON_AddBoolToObject(json, "truncated", 1);
        cJSON_AddStringToObject(json, "truncation_note",
            "Output exceeded 30000 characters and was truncated");
    }

    free(result);
    return json_result(json);
}
