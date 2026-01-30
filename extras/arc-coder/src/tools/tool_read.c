/**
 * @file tool_read.c
 * @brief Read Tool Implementation
 */

#include "code_tools.h"
#include <arc/sandbox.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*============================================================================
 * External State (from tool_bash.c)
 *============================================================================*/

extern const char *code_tools_get_workspace(void);
extern struct ac_sandbox *code_tools_get_sandbox(void);

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char g_read_result_buffer[131072];  /* 128KB */

static const char *json_result_read(cJSON *json) {
    if (!json) {
        return "{\"error\": \"Failed to create response\"}";
    }

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!str) {
        return "{\"error\": \"Failed to serialize response\"}";
    }

    size_t len = strlen(str);
    if (len >= sizeof(g_read_result_buffer)) {
        len = sizeof(g_read_result_buffer) - 1;
    }
    memcpy(g_read_result_buffer, str, len);
    g_read_result_buffer[len] = '\0';

    free(str);
    return g_read_result_buffer;
}

static const char *json_error_read(const char *msg) {
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "error", msg);
    }
    return json_result_read(json);
}

/* Check if file is binary */
static int is_binary_file(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    const char *binary_exts[] = {
        ".zip", ".tar", ".gz", ".exe", ".dll", ".so", ".o", ".a",
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".ico", ".webp",
        ".mp3", ".wav", ".mp4", ".avi", ".mov",
        ".pdf", ".doc", ".docx", ".xls", ".xlsx",
        ".wasm", ".pyc", ".class", ".jar",
        NULL
    };

    for (int i = 0; binary_exts[i]; i++) {
        if (strcasecmp(ext, binary_exts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*============================================================================
 * Read Tool Implementation
 *============================================================================*/

const char *read_file(
    const char *filePath,
    int offset,
    int limit
) {
    if (!filePath || strlen(filePath) == 0) {
        return json_error_read("filePath parameter is required");
    }

    /* Default values */
    int line_offset = offset > 0 ? offset : 0;
    int line_limit = limit > 0 ? limit : 2000;
    const int MAX_LINE_LENGTH = 2000;

    /* Sandbox check */
    ac_sandbox_t *sandbox = code_tools_get_sandbox();
    if (sandbox) {
        if (!ac_sandbox_check_path(sandbox, filePath, AC_SANDBOX_PERM_FS_READ)) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "File access blocked by sandbox");
            cJSON_AddStringToObject(json, "path", filePath);
            cJSON_AddStringToObject(json, "reason", ac_sandbox_denial_reason());
            return json_result_read(json);
        }
    }

    /* Check if binary */
    if (is_binary_file(filePath)) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Cannot read binary file");
        cJSON_AddStringToObject(json, "path", filePath);
        return json_result_read(json);
    }

    /* Open file */
    FILE *fp = fopen(filePath, "r");
    if (!fp) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "File not found");
        cJSON_AddStringToObject(json, "path", filePath);
        return json_result_read(json);
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Check if empty */
    if (file_size == 0) {
        fclose(fp);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "path", filePath);
        cJSON_AddStringToObject(json, "content", "<file is empty>");
        cJSON_AddNumberToObject(json, "total_lines", 0);
        return json_result_read(json);
    }

    /* Read lines */
    char line_buf[4096];
    int current_line = 0;
    int lines_read = 0;
    int total_lines = 0;

    /* Build content with line numbers */
    size_t content_cap = 65536;
    size_t content_len = 0;
    char *content = malloc(content_cap);
    if (!content) {
        fclose(fp);
        return json_error_read("Memory allocation failed");
    }
    content[0] = '\0';

    /* First pass: count total lines and read requested range */
    while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
        total_lines++;

        /* Skip lines before offset */
        if (current_line < line_offset) {
            current_line++;
            continue;
        }

        /* Stop if we've read enough */
        if (lines_read >= line_limit) {
            current_line++;
            continue;
        }

        /* Format line with line number (1-based) */
        char formatted_line[4200];
        int line_len = strlen(line_buf);

        /* Remove trailing newline for processing */
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--;
        }

        /* Truncate if too long */
        if (line_len > MAX_LINE_LENGTH) {
            line_buf[MAX_LINE_LENGTH] = '\0';
            snprintf(formatted_line, sizeof(formatted_line),
                     "%05d| %s...\n", current_line + 1, line_buf);
        } else {
            snprintf(formatted_line, sizeof(formatted_line),
                     "%05d| %s\n", current_line + 1, line_buf);
        }

        /* Append to content */
        size_t formatted_len = strlen(formatted_line);
        if (content_len + formatted_len + 1 > content_cap) {
            content_cap *= 2;
            char *new_content = realloc(content, content_cap);
            if (!new_content) {
                free(content);
                fclose(fp);
                return json_error_read("Memory allocation failed");
            }
            content = new_content;
        }

        strcpy(content + content_len, formatted_line);
        content_len += formatted_len;

        current_line++;
        lines_read++;
    }

    fclose(fp);

    /* Build response */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "path", filePath);
    cJSON_AddNumberToObject(json, "total_lines", total_lines);
    cJSON_AddNumberToObject(json, "offset", line_offset);
    cJSON_AddNumberToObject(json, "lines_read", lines_read);

    /* Add file content */
    char *file_content = malloc(content_len + 50);
    if (file_content) {
        snprintf(file_content, content_len + 50, "<file>\n%s</file>", content);
        cJSON_AddStringToObject(json, "content", file_content);
        free(file_content);
    } else {
        cJSON_AddStringToObject(json, "content", content);
    }

    /* Add note if there are more lines */
    if (line_offset + lines_read < total_lines) {
        char note[256];
        snprintf(note, sizeof(note),
                 "File has more lines. Use offset=%d to read beyond line %d",
                 line_offset + lines_read, line_offset + lines_read);
        cJSON_AddStringToObject(json, "note", note);
    }

    free(content);
    return json_result_read(json);
}
