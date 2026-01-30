/**
 * @file skill_parser.c
 * @brief SKILL.md frontmatter parser
 *
 * Parses YAML-like frontmatter from SKILL.md files.
 * This is a simple parser that handles the basic format:
 *
 *   ---
 *   name: skill-name
 *   description: Skill description text
 *   license: MIT
 *   compatibility: Requirements text
 *   allowed-tools: tool1 tool2 tool3
 *   ---
 *
 * The parser can be replaced with a full YAML library later.
 */

#include "skills_internal.h"
#include <agentc/log.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Windows compatibility: S_ISREG may not be defined in sys/stat.h */
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Skip leading whitespace
 */
static const char *skip_whitespace(const char *s) {
    while (*s && (*s == ' ' || *s == '\t')) {
        s++;
    }
    return s;
}

/**
 * @brief Trim trailing whitespace from string (in-place)
 */
static void trim_trailing(char *s) {
    if (!s || !*s) return;
    
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end-- = '\0';
    }
}

/**
 * @brief Duplicate string with length limit
 */
static char *strndup_safe(const char *s, size_t n) {
    if (!s) return NULL;
    
    size_t len = strlen(s);
    if (len > n) len = n;
    
    char *dup = malloc(len + 1);
    if (!dup) return NULL;
    
    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
}

/**
 * @brief Find next line start
 */
static const char *next_line(const char *s) {
    while (*s && *s != '\n') s++;
    if (*s == '\n') s++;
    return s;
}

/**
 * @brief Check if line starts with "---"
 */
static bool is_fence_line(const char *line) {
    return line[0] == '-' && line[1] == '-' && line[2] == '-' &&
           (line[3] == '\n' || line[3] == '\r' || line[3] == '\0');
}

/**
 * @brief Parse space-separated list into string array
 */
static char **parse_tool_list(const char *value, size_t *count) {
    *count = 0;
    if (!value || !*value) return NULL;
    
    /* Count tokens */
    const char *p = value;
    size_t n = 0;
    while (*p) {
        p = skip_whitespace(p);
        if (!*p) break;
        n++;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    
    if (n == 0) return NULL;
    
    /* Allocate array */
    char **tools = calloc(n + 1, sizeof(char *));
    if (!tools) return NULL;
    
    /* Parse tokens */
    p = value;
    size_t i = 0;
    while (*p && i < n) {
        p = skip_whitespace(p);
        if (!*p) break;
        
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        
        tools[i] = strndup_safe(start, p - start);
        if (!tools[i]) {
            /* Cleanup on error */
            for (size_t j = 0; j < i; j++) free(tools[j]);
            free(tools);
            return NULL;
        }
        i++;
    }
    
    tools[i] = NULL;
    *count = i;
    return tools;
}

/**
 * @brief Parse a single key: value line
 */
static bool parse_kv_line(
    const char *line,
    const char *line_end,
    char **key,
    char **value
) {
    *key = NULL;
    *value = NULL;
    
    /* Skip leading whitespace */
    line = skip_whitespace(line);
    if (line >= line_end || *line == '#') return false;
    
    /* Find colon */
    const char *colon = memchr(line, ':', line_end - line);
    if (!colon) return false;
    
    /* Extract key */
    size_t key_len = colon - line;
    while (key_len > 0 && (line[key_len - 1] == ' ' || line[key_len - 1] == '\t')) {
        key_len--;
    }
    if (key_len == 0) return false;
    
    *key = strndup_safe(line, key_len);
    if (!*key) return false;
    
    /* Extract value */
    const char *val_start = skip_whitespace(colon + 1);
    size_t val_len = line_end - val_start;
    
    /* Trim trailing whitespace/newline */
    while (val_len > 0 && 
           (val_start[val_len - 1] == ' ' || 
            val_start[val_len - 1] == '\t' ||
            val_start[val_len - 1] == '\n' ||
            val_start[val_len - 1] == '\r')) {
        val_len--;
    }
    
    *value = strndup_safe(val_start, val_len);
    if (!*value) {
        free(*key);
        *key = NULL;
        return false;
    }
    
    return true;
}

/*============================================================================
 * Public Parser Functions
 *============================================================================*/

bool skill_validate_name(const char *name) {
    if (!name || !*name) return false;
    
    size_t len = strlen(name);
    
    /* Length check: 1-64 chars */
    if (len < 1 || len > 64) return false;
    
    /* Cannot start or end with hyphen */
    if (name[0] == '-' || name[len - 1] == '-') return false;
    
    bool prev_hyphen = false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        
        /* Must be lowercase alphanumeric or hyphen */
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
        
        /* No consecutive hyphens */
        if (c == '-') {
            if (prev_hyphen) return false;
            prev_hyphen = true;
        } else {
            prev_hyphen = false;
        }
    }
    
    return true;
}

agentc_err_t skill_parse_frontmatter(
    const char *content,
    ac_skill_meta_t *meta,
    const char **body_start
) {
    if (!content || !meta || !body_start) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Initialize output */
    memset(meta, 0, sizeof(*meta));
    *body_start = content;
    
    /* Skip leading whitespace/newlines */
    const char *p = content;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    
    /* Check for opening fence */
    if (!is_fence_line(p)) {
        AC_LOG_WARN("SKILL.md missing opening '---' fence");
        return AGENTC_ERR_PARSE;
    }
    
    /* Move past opening fence */
    p = next_line(p);
    const char *fm_start = p;
    
    /* Find closing fence */
    const char *fm_end = NULL;
    while (*p) {
        if (is_fence_line(p)) {
            fm_end = p;
            break;
        }
        p = next_line(p);
    }
    
    if (!fm_end) {
        AC_LOG_WARN("SKILL.md missing closing '---' fence");
        return AGENTC_ERR_PARSE;
    }
    
    /* Set body start to after closing fence */
    *body_start = next_line(fm_end);
    
    /* Parse frontmatter lines */
    p = fm_start;
    while (p < fm_end) {
        const char *line_end = p;
        while (line_end < fm_end && *line_end != '\n') line_end++;
        
        char *key = NULL;
        char *value = NULL;
        
        if (parse_kv_line(p, line_end, &key, &value)) {
            /* Match known keys */
            if (strcmp(key, "name") == 0) {
                free(meta->name);
                meta->name = value;
                value = NULL;
            } else if (strcmp(key, "description") == 0) {
                free(meta->description);
                meta->description = value;
                value = NULL;
            } else if (strcmp(key, "license") == 0) {
                free(meta->license);
                meta->license = value;
                value = NULL;
            } else if (strcmp(key, "compatibility") == 0) {
                free(meta->compatibility);
                meta->compatibility = value;
                value = NULL;
            } else if (strcmp(key, "allowed-tools") == 0) {
                /* Free existing if any */
                if (meta->allowed_tools) {
                    for (size_t i = 0; i < meta->allowed_tools_count; i++) {
                        free(meta->allowed_tools[i]);
                    }
                    free(meta->allowed_tools);
                }
                meta->allowed_tools = parse_tool_list(value, &meta->allowed_tools_count);
            }
            /* Ignore unknown keys */
            
            free(key);
            free(value);
        }
        
        p = line_end;
        if (*p == '\n') p++;
    }
    
    /* Validate required fields */
    if (!meta->name) {
        AC_LOG_WARN("SKILL.md missing required 'name' field");
        skill_meta_free(meta);
        return AGENTC_ERR_PARSE;
    }
    
    if (!skill_validate_name(meta->name)) {
        AC_LOG_WARN("SKILL.md has invalid name format: %s", meta->name);
        skill_meta_free(meta);
        return AGENTC_ERR_PARSE;
    }
    
    if (!meta->description) {
        AC_LOG_WARN("SKILL.md missing required 'description' field");
        skill_meta_free(meta);
        return AGENTC_ERR_PARSE;
    }
    
    /* Validate description length */
    size_t desc_len = strlen(meta->description);
    if (desc_len < 1 || desc_len > 1024) {
        AC_LOG_WARN("SKILL.md description length out of range: %zu", desc_len);
        skill_meta_free(meta);
        return AGENTC_ERR_PARSE;
    }
    
    return AGENTC_OK;
}

void skill_meta_free(ac_skill_meta_t *meta) {
    if (!meta) return;
    
    free(meta->name);
    free(meta->description);
    free(meta->license);
    free(meta->compatibility);
    
    if (meta->allowed_tools) {
        for (size_t i = 0; i < meta->allowed_tools_count; i++) {
            free(meta->allowed_tools[i]);
        }
        free(meta->allowed_tools);
    }
    
    memset(meta, 0, sizeof(*meta));
}

/*============================================================================
 * File Utilities
 *============================================================================*/

char *skill_read_file(const char *filepath) {
    if (!filepath) return NULL;
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        AC_LOG_DEBUG("Failed to open file: %s", filepath);
        return NULL;
    }
    
    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(fp);
        return NULL;
    }
    
    /* Allocate buffer */
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }
    
    /* Read content */
    size_t read = fread(content, 1, size, fp);
    content[read] = '\0';
    fclose(fp);
    
    return content;
}

bool skill_file_exists(const char *filepath) {
    if (!filepath) return false;
    
    struct stat st;
    return stat(filepath, &st) == 0 && S_ISREG(st.st_mode);
}
