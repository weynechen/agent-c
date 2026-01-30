/**
 * @file skills.c
 * @brief Skills management system implementation
 *
 * Implements progressive skill loading following agentskills.io specification.
 */

#include "skills_internal.h"
#include <agentc/log.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Windows compatibility: S_ISDIR may not be defined in sys/stat.h */
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define SKILL_MD_FILENAME "SKILL.md"
#define MAX_PATH_LEN 1024

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Free a single skill and its resources
 */
static void skill_free(ac_skill_t *skill) {
    if (!skill) return;
    
    skill_meta_free(&skill->meta);
    free(skill->content);
    free(skill->dir_path);
    free(skill);
}

/**
 * @brief Check if path is a directory
 */
static bool is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @brief Build path by joining directory and filename
 */
static char *build_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    
    /* Remove trailing slash from dir if present */
    if (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\')) {
        dir_len--;
    }
    
    char *path = malloc(dir_len + 1 + name_len + 1);
    if (!path) return NULL;
    
    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, name, name_len);
    path[dir_len + 1 + name_len] = '\0';
    
    return path;
}

/**
 * @brief Load full content for a skill
 */
static agentc_err_t skill_load_content(ac_skill_t *skill) {
    if (!skill || !skill->dir_path) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Already loaded */
    if (skill->content) {
        return AGENTC_OK;
    }
    
    /* Build SKILL.md path */
    char *skill_md_path = build_path(skill->dir_path, SKILL_MD_FILENAME);
    if (!skill_md_path) {
        return AGENTC_ERR_MEMORY;
    }
    
    /* Read file */
    char *file_content = skill_read_file(skill_md_path);
    free(skill_md_path);
    
    if (!file_content) {
        AC_LOG_ERROR("Failed to read SKILL.md for skill: %s", skill->meta.name);
        return AGENTC_ERR_IO;
    }
    
    /* Parse to get body */
    ac_skill_meta_t temp_meta;
    const char *body_start = NULL;
    
    agentc_err_t err = skill_parse_frontmatter(file_content, &temp_meta, &body_start);
    if (err != AGENTC_OK) {
        free(file_content);
        return err;
    }
    
    /* Copy body content */
    if (body_start && *body_start) {
        skill->content = strdup(body_start);
    } else {
        skill->content = strdup("");
    }
    
    skill_meta_free(&temp_meta);
    free(file_content);
    
    if (!skill->content) {
        return AGENTC_ERR_MEMORY;
    }
    
    AC_LOG_DEBUG("Loaded content for skill: %s (%zu bytes)", 
                 skill->meta.name, strlen(skill->content));
    
    return AGENTC_OK;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

ac_skills_t *ac_skills_create(void) {
    ac_skills_t *skills = calloc(1, sizeof(ac_skills_t));
    if (!skills) {
        AC_LOG_ERROR("Failed to allocate skills manager");
        return NULL;
    }
    
    AC_LOG_DEBUG("Created skills manager");
    return skills;
}

void ac_skills_destroy(ac_skills_t *skills) {
    if (!skills) return;
    
    /* Free all skills */
    ac_skill_t *curr = skills->head;
    while (curr) {
        ac_skill_t *next = curr->next;
        skill_free(curr);
        curr = next;
    }
    
    free(skills);
    AC_LOG_DEBUG("Destroyed skills manager");
}

agentc_err_t ac_skills_discover(ac_skills_t *skills, const char *skill_dir) {
    if (!skills || !skill_dir) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Check directory exists */
    if (!is_directory(skill_dir)) {
        AC_LOG_WARN("Skill directory not found: %s", skill_dir);
        return AGENTC_ERR_NOT_FOUND;
    }
    
    /* Build SKILL.md path */
    char *skill_md_path = build_path(skill_dir, SKILL_MD_FILENAME);
    if (!skill_md_path) {
        return AGENTC_ERR_MEMORY;
    }
    
    /* Check SKILL.md exists */
    if (!skill_file_exists(skill_md_path)) {
        AC_LOG_DEBUG("No SKILL.md in: %s", skill_dir);
        free(skill_md_path);
        return AGENTC_ERR_NOT_FOUND;
    }
    
    /* Read file */
    char *file_content = skill_read_file(skill_md_path);
    free(skill_md_path);
    
    if (!file_content) {
        return AGENTC_ERR_IO;
    }
    
    /* Parse frontmatter */
    ac_skill_meta_t meta;
    const char *body_start = NULL;
    
    agentc_err_t err = skill_parse_frontmatter(file_content, &meta, &body_start);
    if (err != AGENTC_OK) {
        free(file_content);
        return err;
    }
    
    /* Check for duplicate */
    if (ac_skills_find(skills, meta.name)) {
        AC_LOG_WARN("Skill already discovered: %s", meta.name);
        skill_meta_free(&meta);
        free(file_content);
        return AGENTC_OK; /* Not an error, just skip */
    }
    
    /* Create skill entry */
    ac_skill_t *skill = calloc(1, sizeof(ac_skill_t));
    if (!skill) {
        skill_meta_free(&meta);
        free(file_content);
        return AGENTC_ERR_MEMORY;
    }
    
    skill->meta = meta;
    skill->dir_path = strdup(skill_dir);
    skill->state = AC_SKILL_DISCOVERED;
    skill->content = NULL; /* Loaded on enable */
    
    if (!skill->dir_path) {
        skill_free(skill);
        free(file_content);
        return AGENTC_ERR_MEMORY;
    }
    
    /* Add to list (prepend) */
    skill->next = skills->head;
    skills->head = skill;
    skills->count++;
    
    free(file_content);
    
    AC_LOG_INFO("Discovered skill: %s", skill->meta.name);
    return AGENTC_OK;
}

agentc_err_t ac_skills_discover_dir(ac_skills_t *skills, const char *skills_dir) {
    if (!skills || !skills_dir) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Check directory exists */
    DIR *dir = opendir(skills_dir);
    if (!dir) {
        AC_LOG_WARN("Skills directory not found: %s", skills_dir);
        return AGENTC_OK; /* Not an error if directory doesn't exist */
    }
    
    struct dirent *entry;
    int discovered = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (entry->d_name[0] == '.') continue;
        
        /* Build full path */
        char *subdir_path = build_path(skills_dir, entry->d_name);
        if (!subdir_path) continue;
        
        /* Check if it's a directory */
        if (!is_directory(subdir_path)) {
            free(subdir_path);
            continue;
        }
        
        /* Try to discover skill */
        if (ac_skills_discover(skills, subdir_path) == AGENTC_OK) {
            discovered++;
        }
        
        free(subdir_path);
    }
    
    closedir(dir);
    
    AC_LOG_INFO("Discovered %d skills from %s", discovered, skills_dir);
    return AGENTC_OK;
}

agentc_err_t ac_skills_enable(ac_skills_t *skills, const char *name) {
    if (!skills || !name) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Find skill */
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->meta.name && strcmp(skill->meta.name, name) == 0) {
            break;
        }
        skill = skill->next;
    }
    
    if (!skill) {
        AC_LOG_WARN("Skill not found: %s", name);
        return AGENTC_ERR_NOT_FOUND;
    }
    
    /* Already enabled? */
    if (skill->state == AC_SKILL_ENABLED) {
        return AGENTC_OK;
    }
    
    /* Load content if not already loaded */
    agentc_err_t err = skill_load_content(skill);
    if (err != AGENTC_OK) {
        return err;
    }
    
    /* Update state */
    if (skill->state != AC_SKILL_ENABLED) {
        skill->state = AC_SKILL_ENABLED;
        skills->enabled_count++;
    }
    
    AC_LOG_INFO("Enabled skill: %s", name);
    return AGENTC_OK;
}

agentc_err_t ac_skills_disable(ac_skills_t *skills, const char *name) {
    if (!skills || !name) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Find skill */
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->meta.name && strcmp(skill->meta.name, name) == 0) {
            break;
        }
        skill = skill->next;
    }
    
    if (!skill) {
        return AGENTC_ERR_NOT_FOUND;
    }
    
    /* Update state */
    if (skill->state == AC_SKILL_ENABLED) {
        skill->state = AC_SKILL_DISABLED;
        skills->enabled_count--;
    } else {
        skill->state = AC_SKILL_DISABLED;
    }
    
    AC_LOG_DEBUG("Disabled skill: %s", name);
    return AGENTC_OK;
}

size_t ac_skills_enable_all(ac_skills_t *skills) {
    if (!skills) return 0;
    
    size_t count = 0;
    ac_skill_t *skill = skills->head;
    
    while (skill) {
        if (ac_skills_enable(skills, skill->meta.name) == AGENTC_OK) {
            count++;
        }
        skill = skill->next;
    }
    
    return count;
}

void ac_skills_disable_all(ac_skills_t *skills) {
    if (!skills) return;
    
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->state == AC_SKILL_ENABLED) {
            skill->state = AC_SKILL_DISABLED;
        }
        skill = skill->next;
    }
    
    skills->enabled_count = 0;
    AC_LOG_DEBUG("Disabled all skills");
}

const ac_skill_t *ac_skills_find(const ac_skills_t *skills, const char *name) {
    if (!skills || !name) return NULL;
    
    const ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->meta.name && strcmp(skill->meta.name, name) == 0) {
            return skill;
        }
        skill = skill->next;
    }
    
    return NULL;
}

size_t ac_skills_count(const ac_skills_t *skills) {
    return skills ? skills->count : 0;
}

size_t ac_skills_enabled_count(const ac_skills_t *skills) {
    return skills ? skills->enabled_count : 0;
}

const ac_skill_t *ac_skills_list(const ac_skills_t *skills) {
    return skills ? skills->head : NULL;
}

agentc_err_t ac_skills_validate_tools(
    const ac_skills_t *skills,
    const char *name,
    const ac_tool_registry_t *registry
) {
    if (!skills || !name || !registry) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    const ac_skill_t *skill = ac_skills_find(skills, name);
    if (!skill) {
        return AGENTC_ERR_NOT_FOUND;
    }
    
    /* No allowed_tools specified means all tools are allowed */
    if (!skill->meta.allowed_tools || skill->meta.allowed_tools_count == 0) {
        return AGENTC_OK;
    }
    
    /* Check each tool exists in registry */
    for (size_t i = 0; i < skill->meta.allowed_tools_count; i++) {
        const char *tool_name = skill->meta.allowed_tools[i];
        if (!ac_tool_registry_find(registry, tool_name)) {
            AC_LOG_WARN("Skill %s references missing tool: %s", name, tool_name);
            return AGENTC_ERR_NOT_FOUND;
        }
    }
    
    return AGENTC_OK;
}

agentc_err_t ac_skills_set_script_executor(
    ac_skills_t *skills,
    ac_skill_script_fn executor,
    void *user_data
) {
    if (!skills) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* Store for future use */
    skills->script_executor = executor;
    skills->script_user_data = user_data;
    
    /* Currently not implemented */
    AC_LOG_WARN("Script executor set but execution not yet implemented");
    return AGENTC_ERR_NOT_IMPLEMENTED;
}
