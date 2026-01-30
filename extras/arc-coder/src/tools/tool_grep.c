/**
 * @file tool_grep.c
 * @brief Grep Tool Implementation
 *
 * Content search using regex patterns.
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
#include <regex.h>

/*============================================================================
 * External State
 *============================================================================*/

extern const char *code_tools_get_workspace(void);
extern struct ac_sandbox *code_tools_get_sandbox(void);

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char g_grep_result_buffer[131072];  /* 128KB */

static const char *json_result_grep(cJSON *json) {
    if (!json) {
        return "{\"error\": \"Failed to create response\"}";
    }

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!str) {
        return "{\"error\": \"Failed to serialize response\"}";
    }

    size_t len = strlen(str);
    if (len >= sizeof(g_grep_result_buffer)) {
        len = sizeof(g_grep_result_buffer) - 1;
    }
    memcpy(g_grep_result_buffer, str, len);
    g_grep_result_buffer[len] = '\0';

    free(str);
    return g_grep_result_buffer;
}

static const char *json_error_grep(const char *msg) {
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "error", msg);
    }
    return json_result_grep(json);
}

/* Check if file matches include pattern */
static int matches_include(const char *filename, const char *include) {
    if (!include || strlen(include) == 0) return 1;  /* No filter = match all */

    return fnmatch(include, filename, FNM_NOESCAPE) == 0;
}

/* Search a single file */
static void search_file(
    const char *filepath,
    regex_t *regex,
    cJSON *matches,
    int *match_count,
    int max_matches
) {
    if (*match_count >= max_matches) return;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return;

    char line[4096];
    int line_num = 0;
    int file_matches = 0;

    while (fgets(line, sizeof(line), fp) && *match_count < max_matches) {
        line_num++;

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        /* Check for match */
        if (regexec(regex, line, 0, NULL, 0) == 0) {
            cJSON *match = cJSON_CreateObject();
            cJSON_AddStringToObject(match, "file", filepath);
            cJSON_AddNumberToObject(match, "line", line_num);

            /* Truncate long lines */
            if (len > 200) {
                line[200] = '\0';
                strcat(line, "...");
            }
            cJSON_AddStringToObject(match, "content", line);

            cJSON_AddItemToArray(matches, match);
            (*match_count)++;
            file_matches++;
        }
    }

    fclose(fp);
}

/* Recursively search directory */
static void search_directory(
    const char *dir_path,
    regex_t *regex,
    const char *include,
    cJSON *matches,
    int *match_count,
    int max_matches,
    int depth
) {
    if (*match_count >= max_matches || depth > 20) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) && *match_count < max_matches) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Skip hidden files/dirs */
        if (entry->d_name[0] == '.') {
            continue;
        }

        /* Skip common non-code directories */
        if (strcmp(entry->d_name, "node_modules") == 0 ||
            strcmp(entry->d_name, "__pycache__") == 0 ||
            strcmp(entry->d_name, ".git") == 0 ||
            strcmp(entry->d_name, "build") == 0 ||
            strcmp(entry->d_name, "dist") == 0 ||
            strcmp(entry->d_name, "vendor") == 0) {
            continue;
        }

        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            search_directory(full_path, regex, include, matches, match_count, max_matches, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            /* Check include pattern */
            if (matches_include(entry->d_name, include)) {
                search_file(full_path, regex, matches, match_count, max_matches);
            }
        }
    }

    closedir(dir);
}

/*============================================================================
 * Grep Tool Implementation
 *============================================================================*/

const char *grep(
    const char *pattern,
    const char *path,
    const char *include
) {
    if (!pattern || strlen(pattern) == 0) {
        return json_error_grep("pattern parameter is required");
    }

    /* Default to workspace */
    const char *search_path = (path && strlen(path) > 0) ? path : code_tools_get_workspace();

    /* Sandbox check */
    ac_sandbox_t *sandbox = code_tools_get_sandbox();
    if (sandbox) {
        if (!ac_sandbox_check_path(sandbox, search_path, AC_SANDBOX_PERM_FS_READ)) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "Search path blocked by sandbox");
            cJSON_AddStringToObject(json, "path", search_path);
            cJSON_AddStringToObject(json, "reason", ac_sandbox_denial_reason());
            return json_result_grep(json);
        }
    }

    /* Compile regex */
    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED | REG_ICASE | REG_NEWLINE);
    if (ret != 0) {
        char error_buf[256];
        regerror(ret, &regex, error_buf, sizeof(error_buf));

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Invalid regex pattern");
        cJSON_AddStringToObject(json, "pattern", pattern);
        cJSON_AddStringToObject(json, "reason", error_buf);
        return json_result_grep(json);
    }

    /* Search */
    cJSON *matches = cJSON_CreateArray();
    int match_count = 0;
    const int MAX_MATCHES = 500;

    struct stat st;
    if (stat(search_path, &st) != 0) {
        regfree(&regex);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Path not found");
        cJSON_AddStringToObject(json, "path", search_path);
        return json_result_grep(json);
    }

    if (S_ISDIR(st.st_mode)) {
        search_directory(search_path, &regex, include, matches, &match_count, MAX_MATCHES, 0);
    } else {
        search_file(search_path, &regex, matches, &match_count, MAX_MATCHES);
    }

    regfree(&regex);

    /* Build response */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "pattern", pattern);
    cJSON_AddStringToObject(json, "path", search_path);
    if (include && strlen(include) > 0) {
        cJSON_AddStringToObject(json, "include", include);
    }
    cJSON_AddNumberToObject(json, "match_count", match_count);
    cJSON_AddItemToObject(json, "matches", matches);

    if (match_count >= MAX_MATCHES) {
        cJSON_AddBoolToObject(json, "truncated", 1);
        cJSON_AddStringToObject(json, "note", "Results truncated at 500 matches");
    }

    return json_result_grep(json);
}

/*============================================================================
 * Glob Tool Implementation
 *============================================================================*/

/* Helper to collect matching files */
static void glob_directory(
    const char *dir_path,
    const char *pattern,
    cJSON *files,
    int *count,
    int max_files,
    int depth
) {
    if (*count >= max_files || depth > 20) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) && *count < max_files) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (entry->d_name[0] == '.') continue;

        /* Skip common non-code directories */
        if (strcmp(entry->d_name, "node_modules") == 0 ||
            strcmp(entry->d_name, "__pycache__") == 0 ||
            strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            glob_directory(full_path, pattern, files, count, max_files, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            /* Check pattern match */
            if (fnmatch(pattern, entry->d_name, FNM_NOESCAPE) == 0 ||
                fnmatch(pattern, full_path, FNM_NOESCAPE | FNM_PATHNAME) == 0) {
                cJSON_AddItemToArray(files, cJSON_CreateString(full_path));
                (*count)++;
            }
        }
    }

    closedir(dir);
}

const char *glob_files(
    const char *pattern,
    const char *path
) {
    if (!pattern || strlen(pattern) == 0) {
        return json_error_grep("pattern parameter is required");
    }

    const char *search_path = (path && strlen(path) > 0) ? path : code_tools_get_workspace();

    /* Sandbox check */
    ac_sandbox_t *sandbox = code_tools_get_sandbox();
    if (sandbox) {
        if (!ac_sandbox_check_path(sandbox, search_path, AC_SANDBOX_PERM_FS_READ)) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "error", "Search path blocked by sandbox");
            cJSON_AddStringToObject(json, "path", search_path);
            return json_result_grep(json);
        }
    }

    cJSON *files = cJSON_CreateArray();
    int count = 0;
    const int MAX_FILES = 1000;

    glob_directory(search_path, pattern, files, &count, MAX_FILES, 0);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "pattern", pattern);
    cJSON_AddStringToObject(json, "path", search_path);
    cJSON_AddNumberToObject(json, "count", count);
    cJSON_AddItemToObject(json, "files", files);

    if (count >= MAX_FILES) {
        cJSON_AddBoolToObject(json, "truncated", 1);
    }

    return json_result_grep(json);
}
