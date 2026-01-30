/**
 * @file rules.c
 * @brief Rules Management Implementation
 */

#include <arc/rules.h>
#include <arc/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/*============================================================================
 * Internal Structure
 *============================================================================*/

struct ac_rules {
    ac_rule_t *head;
    size_t count;
};

/*============================================================================
 * Helper Functions
 *============================================================================*/

static int is_rule_file(const char *filename) {
    size_t len = strlen(filename);
    return (len > 5 && strcmp(filename + len - 5, ".yaml") == 0) ||
           (len > 4 && strcmp(filename + len - 4, ".yml") == 0) ||
           (len > 4 && strcmp(filename + len - 4, ".txt") == 0);
}

static char *read_file_content(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        AC_LOG_ERROR("Failed to open file: %s", filepath);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return NULL;
    }

    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(content, 1, size, fp);
    content[read] = '\0';
    fclose(fp);

    return content;
}

static const char *extract_filename(const char *filepath) {
    const char *name = strrchr(filepath, '/');
    return name ? name + 1 : filepath;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

ac_rules_t *ac_rules_create(void) {
    ac_rules_t *rules = calloc(1, sizeof(ac_rules_t));
    if (!rules) {
        AC_LOG_ERROR("Failed to allocate rules manager");
    }
    return rules;
}

arc_err_t ac_rules_load_dir(ac_rules_t *rules, const char *rules_dir) {
    if (!rules || !rules_dir) {
        return ARC_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(rules_dir);
    if (!dir) {
        AC_LOG_WARN("Rules directory not found: %s", rules_dir);
        return ARC_OK;  /* Not an error if directory doesn't exist */
    }

    struct dirent *entry;
    int loaded = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) {
            continue;
        }

        if (!is_rule_file(entry->d_name)) {
            continue;
        }

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", rules_dir, entry->d_name);

        if (ac_rules_load_file(rules, filepath) == ARC_OK) {
            loaded++;
        }
    }

    closedir(dir);

    AC_LOG_INFO("Loaded %d rules from %s", loaded, rules_dir);
    return ARC_OK;
}

arc_err_t ac_rules_load_file(ac_rules_t *rules, const char *filepath) {
    if (!rules || !filepath) {
        return ARC_ERR_INVALID_ARG;
    }

    char *content = read_file_content(filepath);
    if (!content) {
        AC_LOG_WARN("Failed to read rule file: %s", filepath);
        return ARC_ERR_IO;
    }

    /* TODO: Parse YAML if it's a .yaml/.yml file
     * For now, treat all files as plain text
     */

    const char *name = extract_filename(filepath);
    arc_err_t err = ac_rules_add(rules, name, content, 0);

    free(content);
    return err;
}

arc_err_t ac_rules_add(
    ac_rules_t *rules,
    const char *name,
    const char *content,
    int priority
) {
    if (!rules || !name || !content) {
        return ARC_ERR_INVALID_ARG;
    }

    ac_rule_t *rule = calloc(1, sizeof(ac_rule_t));
    if (!rule) {
        AC_LOG_ERROR("Failed to allocate rule");
        return ARC_ERR_MEMORY;
    }

    rule->name = strdup(name);
    rule->content = strdup(content);
    rule->priority = priority;

    if (!rule->name || !rule->content) {
        free(rule->name);
        free(rule->content);
        free(rule);
        return ARC_ERR_MEMORY;
    }

    /* Insert in priority order (higher priority first) */
    if (!rules->head || rules->head->priority < priority) {
        rule->next = rules->head;
        rules->head = rule;
    } else {
        ac_rule_t *curr = rules->head;
        while (curr->next && curr->next->priority >= priority) {
            curr = curr->next;
        }
        rule->next = curr->next;
        curr->next = rule;
    }

    rules->count++;
    AC_LOG_DEBUG("Added rule: %s (priority=%d)", name, priority);

    return ARC_OK;
}

char *ac_rules_build_prompt(ac_rules_t *rules, const char *base_prompt) {
    if (!rules) {
        return base_prompt ? strdup(base_prompt) : NULL;
    }

    /* Calculate total size */
    size_t total_size = base_prompt ? strlen(base_prompt) : 0;
    ac_rule_t *rule = rules->head;
    while (rule) {
        total_size += strlen(rule->content) + 2;  /* +2 for newlines */
        rule = rule->next;
    }

    if (total_size == 0) {
        return NULL;
    }

    /* Allocate buffer */
    char *prompt = malloc(total_size + 1);
    if (!prompt) {
        AC_LOG_ERROR("Failed to allocate prompt buffer");
        return NULL;
    }

    /* Build prompt */
    char *ptr = prompt;
    if (base_prompt) {
        size_t len = strlen(base_prompt);
        memcpy(ptr, base_prompt, len);
        ptr += len;
    }

    rule = rules->head;
    while (rule) {
        *ptr++ = '\n';
        size_t len = strlen(rule->content);
        memcpy(ptr, rule->content, len);
        ptr += len;
        *ptr++ = '\n';
        rule = rule->next;
    }
    *ptr = '\0';

    AC_LOG_DEBUG("Built system prompt with %zu rules (%zu bytes)",
                 rules->count, total_size);

    return prompt;
}

size_t ac_rules_count(const ac_rules_t *rules) {
    return rules ? rules->count : 0;
}

const ac_rule_t *ac_rules_list(const ac_rules_t *rules) {
    return rules ? rules->head : NULL;
}

void ac_rules_clear(ac_rules_t *rules) {
    if (!rules) {
        return;
    }

    ac_rule_t *curr = rules->head;
    while (curr) {
        ac_rule_t *next = curr->next;
        free(curr->name);
        free(curr->content);
        free(curr);
        curr = next;
    }

    rules->head = NULL;
    rules->count = 0;
}

void ac_rules_destroy(ac_rules_t *rules) {
    if (!rules) {
        return;
    }

    ac_rules_clear(rules);
    free(rules);

    AC_LOG_DEBUG("Destroyed rules manager");
}
