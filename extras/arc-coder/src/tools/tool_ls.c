/**
 * @file tool_ls.c
 * @brief LS Tool Implementation
 */

#include "code_tools.h"
#include <arc/sandbox.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fnmatch.h>

/*============================================================================
 * External State
 *============================================================================*/

extern const char *code_tools_get_workspace(void);
extern struct ac_sandbox *code_tools_get_sandbox(void);

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char g_ls_result_buffer[65536];

static const char *json_result_ls(cJSON *json) {
    if (!json) {
        return "{\"error\": \"Failed to create response\"}";
    }

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!str) {
        return "{\"error\": \"Failed to serialize response\"}";
    }

    size_t len = strlen(str);
    if (len >= sizeof(g_ls_result_buffer)) {
        len = sizeof(g_ls_result_buffer) - 1;
    }
    memcpy(g_ls_result_buffer, str, len);
    g_ls_result_buffer[len] = '\0';

    free(str);
    return g_ls_result_buffer;
}

// static const char *json_error_ls(const char *msg) {
//     cJSON *json = cJSON_CreateObject();
//     if (json) {
//         cJSON_AddStringToObject(json, "error", msg);
//     }
//     return json_result_ls(json);
// }

/* Check if name matches any ignore pattern */
static int should_ignore(const char *name, const char *ignore) {
    if (!ignore || strlen(ignore) == 0) return 0;

    /* Parse comma-separated patterns */
    char *patterns = strdup(ignore);
    if (!patterns) return 0;

    char *pattern = strtok(patterns, ",");
    while (pattern) {
        /* Trim whitespace */
        while (*pattern == ' ') pattern++;
        char *end = pattern + strlen(pattern) - 1;
        while (end > pattern && *end == ' ') *end-- = '\0';

        if (fnmatch(pattern, name, FNM_NOESCAPE) == 0) {
            free(patterns);
            return 1;
        }
        pattern = strtok(NULL, ",");
    }

    free(patterns);
    return 0;
}

/* Format file size for display */
static void format_size(off_t size, char *buf, size_t buf_len) {
    if (size < 1024) {
        snprintf(buf, buf_len, "%lld B", (long long)size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buf, buf_len, "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

/*============================================================================
 * LS Tool Implementation
 *============================================================================*/

const char *ls(
    const char *path,
    const char *ignore
) {
    /* Default to workspace if no path provided */
    const char *dir_path = (path && strlen(path) > 0) ? path : code_tools_get_workspace();

    /* Sandbox check */
    ac_sandbox_t *sandbox = code_tools_get_sandbox();
    if (sandbox) {
        if (!ac_sandbox_check_path(sandbox, dir_path, AC_SANDBOX_PERM_FS_READ)) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "Directory access blocked by sandbox");
            cJSON_AddStringToObject(json, "path", dir_path);
            cJSON_AddStringToObject(json, "reason", ac_sandbox_denial_reason());
            return json_result_ls(json);
        }
    }

    /* Open directory */
    DIR *dir = opendir(dir_path);
    if (!dir) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Failed to open directory");
        cJSON_AddStringToObject(json, "path", dir_path);
        return json_result_ls(json);
    }

    /* Build response */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "path", dir_path);

    cJSON *dirs = cJSON_CreateArray();
    cJSON *files = cJSON_CreateArray();

    int dir_count = 0;
    int file_count = 0;
    int total_count = 0;
    const int MAX_ENTRIES = 1000;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && total_count < MAX_ENTRIES) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Skip hidden files (starting with .) */
        if (entry->d_name[0] == '.') {
            continue;
        }

        /* Check ignore patterns */
        if (should_ignore(entry->d_name, ignore)) {
            continue;
        }

        /* Get full path for stat */
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            cJSON *dir_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(dir_obj, "name", entry->d_name);
            cJSON_AddStringToObject(dir_obj, "type", "directory");
            cJSON_AddItemToArray(dirs, dir_obj);
            dir_count++;
        } else if (S_ISREG(st.st_mode)) {
            cJSON *file_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(file_obj, "name", entry->d_name);
            cJSON_AddStringToObject(file_obj, "type", "file");
            cJSON_AddNumberToObject(file_obj, "size", (double)st.st_size);

            char size_str[32];
            format_size(st.st_size, size_str, sizeof(size_str));
            cJSON_AddStringToObject(file_obj, "size_formatted", size_str);

            cJSON_AddItemToArray(files, file_obj);
            file_count++;
        }

        total_count++;
    }

    closedir(dir);

    cJSON_AddItemToObject(json, "directories", dirs);
    cJSON_AddItemToObject(json, "files", files);
    cJSON_AddNumberToObject(json, "directory_count", dir_count);
    cJSON_AddNumberToObject(json, "file_count", file_count);
    cJSON_AddNumberToObject(json, "total", dir_count + file_count);

    if (total_count >= MAX_ENTRIES) {
        cJSON_AddBoolToObject(json, "truncated", 1);
        cJSON_AddStringToObject(json, "note", "Result truncated at 1000 entries");
    }

    return json_result_ls(json);
}
