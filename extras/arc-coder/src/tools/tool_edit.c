/**
 * @file tool_edit.c
 * @brief Edit Tool Implementation
 *
 * Implements string replacement editing following opencode's approach.
 */

#include "code_tools.h"
#include <arc/sandbox.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*============================================================================
 * External State
 *============================================================================*/

extern struct ac_sandbox *code_tools_get_sandbox(void);

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char g_edit_result_buffer[8192];

static const char *json_result_edit(cJSON *json) {
    if (!json) {
        return "{\"error\": \"Failed to create response\"}";
    }

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!str) {
        return "{\"error\": \"Failed to serialize response\"}";
    }

    size_t len = strlen(str);
    if (len >= sizeof(g_edit_result_buffer)) {
        len = sizeof(g_edit_result_buffer) - 1;
    }
    memcpy(g_edit_result_buffer, str, len);
    g_edit_result_buffer[len] = '\0';

    free(str);
    return g_edit_result_buffer;
}

static const char *json_error_edit(const char *msg) {
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "error", msg);
    }
    return json_result_edit(json);
}

/* Count occurrences of a substring */
static int count_occurrences(const char *haystack, const char *needle) {
    int count = 0;
    size_t needle_len = strlen(needle);

    if (needle_len == 0) return 0;

    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }

    return count;
}

/* Replace first occurrence */
static char *replace_first(const char *content, const char *old_str, const char *new_str) {
    const char *pos = strstr(content, old_str);
    if (!pos) return NULL;

    size_t content_len = strlen(content);
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t result_len = content_len - old_len + new_len;

    char *result = malloc(result_len + 1);
    if (!result) return NULL;

    /* Copy prefix */
    size_t prefix_len = pos - content;
    memcpy(result, content, prefix_len);

    /* Copy replacement */
    memcpy(result + prefix_len, new_str, new_len);

    /* Copy suffix */
    memcpy(result + prefix_len + new_len, pos + old_len, content_len - prefix_len - old_len + 1);

    return result;
}

/* Replace all occurrences */
static char *replace_all(const char *content, const char *old_str, const char *new_str) {
    int count = count_occurrences(content, old_str);
    if (count == 0) return NULL;

    size_t content_len = strlen(content);
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t result_len = content_len + count * ((int)new_len - (int)old_len);

    char *result = malloc(result_len + 1);
    if (!result) return NULL;

    char *dst = result;
    const char *src = content;

    while (*src) {
        if (strncmp(src, old_str, old_len) == 0) {
            memcpy(dst, new_str, new_len);
            dst += new_len;
            src += old_len;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    return result;
}

/*============================================================================
 * Edit Tool Implementation
 *============================================================================*/

const char *edit_file(
    const char *filePath,
    const char *oldString,
    const char *newString,
    bool replaceAll
) {
    if (!filePath || strlen(filePath) == 0) {
        return json_error_edit("filePath parameter is required");
    }

    if (!oldString) {
        return json_error_edit("oldString parameter is required");
    }

    if (!newString) {
        return json_error_edit("newString parameter is required");
    }

    if (strcmp(oldString, newString) == 0) {
        return json_error_edit("oldString and newString must be different");
    }

    /* Sandbox check */
    ac_sandbox_t *sandbox = code_tools_get_sandbox();
    if (sandbox) {
        if (!ac_sandbox_check_path(sandbox, filePath, AC_SANDBOX_PERM_FS_WRITE)) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "File edit blocked by sandbox");
            cJSON_AddStringToObject(json, "path", filePath);
            cJSON_AddStringToObject(json, "reason", ac_sandbox_denial_reason());
            return json_result_edit(json);
        }
    }

    /* Read file */
    FILE *fp = fopen(filePath, "r");
    if (!fp) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "File not found");
        cJSON_AddStringToObject(json, "path", filePath);
        return json_result_edit(json);
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(fp);
        return json_error_edit("Memory allocation failed");
    }

    size_t read_size = fread(content, 1, file_size, fp);
    content[read_size] = '\0';
    fclose(fp);

    /* Count occurrences */
    int occurrences = count_occurrences(content, oldString);

    if (occurrences == 0) {
        free(content);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "oldString not found in file");
        cJSON_AddStringToObject(json, "path", filePath);
        cJSON_AddStringToObject(json, "hint",
            "Make sure the oldString exactly matches the content including whitespace and indentation");
        return json_result_edit(json);
    }

    /* Check for multiple occurrences without replaceAll */
    if (occurrences > 1 && !replaceAll) {
        free(content);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error",
            "oldString found multiple times - provide more context or use replaceAll");
        cJSON_AddStringToObject(json, "path", filePath);
        cJSON_AddNumberToObject(json, "occurrences", occurrences);
        cJSON_AddStringToObject(json, "hint",
            "Include more surrounding lines in oldString to uniquely identify the match, "
            "or set replaceAll=true to replace all occurrences");
        return json_result_edit(json);
    }

    /* Perform replacement */
    char *new_content;
    int replacements;

    if (replaceAll) {
        new_content = replace_all(content, oldString, newString);
        replacements = occurrences;
    } else {
        new_content = replace_first(content, oldString, newString);
        replacements = 1;
    }

    free(content);

    if (!new_content) {
        return json_error_edit("Failed to perform replacement");
    }

    /* Write back */
    fp = fopen(filePath, "w");
    if (!fp) {
        free(new_content);
        return json_error_edit("Failed to open file for writing");
    }

    size_t new_len = strlen(new_content);
    size_t written = fwrite(new_content, 1, new_len, fp);
    fclose(fp);
    free(new_content);

    if (written != new_len) {
        return json_error_edit("Failed to write complete content");
    }

    /* Build response */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", 1);
    cJSON_AddStringToObject(json, "path", filePath);
    cJSON_AddNumberToObject(json, "replacements", replacements);

    /* Show brief diff info */
    int old_lines = 1, new_lines = 1;
    for (const char *p = oldString; *p; p++) if (*p == '\n') old_lines++;
    for (const char *p = newString; *p; p++) if (*p == '\n') new_lines++;

    cJSON_AddNumberToObject(json, "lines_removed", old_lines);
    cJSON_AddNumberToObject(json, "lines_added", new_lines);

    return json_result_edit(json);
}
