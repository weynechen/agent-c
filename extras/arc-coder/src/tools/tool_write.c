/**
 * @file tool_write.c
 * @brief Write Tool Implementation
 */

#include "code_tools.h"
#include <arc/sandbox.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>
#include <errno.h>

/*============================================================================
 * External State
 *============================================================================*/

extern struct ac_sandbox *code_tools_get_sandbox(void);

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char g_write_result_buffer[4096];

static const char *json_result_write(cJSON *json) {
    if (!json) {
        return "{\"error\": \"Failed to create response\"}";
    }

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!str) {
        return "{\"error\": \"Failed to serialize response\"}";
    }

    size_t len = strlen(str);
    if (len >= sizeof(g_write_result_buffer)) {
        len = sizeof(g_write_result_buffer) - 1;
    }
    memcpy(g_write_result_buffer, str, len);
    g_write_result_buffer[len] = '\0';

    free(str);
    return g_write_result_buffer;
}

static const char *json_error_write(const char *msg) {
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "error", msg);
    }
    return json_result_write(json);
}

/* Create directory recursively */
static int mkdir_recursive(const char *path) {
    char tmp[4096];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/*============================================================================
 * Write Tool Implementation
 *============================================================================*/

const char *write_file(
    const char *filePath,
    const char *content
) {
    if (!filePath || strlen(filePath) == 0) {
        return json_error_write("filePath parameter is required");
    }

    if (!content) {
        return json_error_write("content parameter is required");
    }

    /* Sandbox check */
    ac_sandbox_t *sandbox = code_tools_get_sandbox();
    if (sandbox) {
        unsigned int perms = AC_SANDBOX_PERM_FS_WRITE | AC_SANDBOX_PERM_FS_CREATE;
        if (!ac_sandbox_check_path(sandbox, filePath, perms)) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "File write blocked by sandbox");
            cJSON_AddStringToObject(json, "path", filePath);
            cJSON_AddStringToObject(json, "reason", ac_sandbox_denial_reason());
            return json_result_write(json);
        }
    }

    /* Check if file already exists */
    struct stat st;
    int exists = (stat(filePath, &st) == 0);

    /* Ensure parent directory exists */
    char *path_copy = strdup(filePath);
    if (path_copy) {
        char *dir = dirname(path_copy);
        if (strlen(dir) > 0 && strcmp(dir, ".") != 0) {
            mkdir_recursive(dir);
        }
        free(path_copy);
    }

    /* Write file */
    FILE *fp = fopen(filePath, "w");
    if (!fp) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Failed to open file for writing");
        cJSON_AddStringToObject(json, "path", filePath);
        cJSON_AddStringToObject(json, "reason", strerror(errno));
        return json_result_write(json);
    }

    size_t content_len = strlen(content);
    size_t written = fwrite(content, 1, content_len, fp);
    fclose(fp);

    if (written != content_len) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Failed to write complete content");
        cJSON_AddStringToObject(json, "path", filePath);
        cJSON_AddNumberToObject(json, "expected", (double)content_len);
        cJSON_AddNumberToObject(json, "written", (double)written);
        return json_result_write(json);
    }

    /* Count lines */
    int line_count = 1;
    for (size_t i = 0; i < content_len; i++) {
        if (content[i] == '\n') line_count++;
    }

    /* Build response */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", 1);
    cJSON_AddStringToObject(json, "path", filePath);
    cJSON_AddNumberToObject(json, "bytes_written", (double)written);
    cJSON_AddNumberToObject(json, "lines", line_count);
    cJSON_AddStringToObject(json, "action", exists ? "updated" : "created");

    return json_result_write(json);
}
