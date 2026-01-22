/**
 * @file skills.c
 * @brief Skills Management Implementation
 */

#include <agentc/skills.h>
#include <agentc/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/*============================================================================
 * Internal Structure
 *============================================================================*/

struct ac_skills {
    ac_skill_t *head;
    size_t count;
};

/*============================================================================
 * Helper Functions
 *============================================================================*/

static int is_skill_file(const char *filename) {
    size_t len = strlen(filename);
    return (len > 5 && strcmp(filename + len - 5, ".yaml") == 0) ||
           (len > 4 && strcmp(filename + len - 4, ".yml") == 0);
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

ac_skills_t *ac_skills_create(void) {
    ac_skills_t *skills = calloc(1, sizeof(ac_skills_t));
    if (!skills) {
        AC_LOG_ERROR("Failed to allocate skills manager");
    }
    return skills;
}

agentc_err_t ac_skills_load_dir(ac_skills_t *skills, const char *skills_dir) {
    if (!skills || !skills_dir) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    DIR *dir = opendir(skills_dir);
    if (!dir) {
        AC_LOG_WARN("Skills directory not found: %s", skills_dir);
        return AGENTC_OK;  /* Not an error */
    }
    
    struct dirent *entry;
    int loaded = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) {
            continue;
        }
        
        if (!is_skill_file(entry->d_name)) {
            continue;
        }
        
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", skills_dir, entry->d_name);
        
        if (ac_skills_load_file(skills, filepath) == AGENTC_OK) {
            loaded++;
        }
    }
    
    closedir(dir);
    
    AC_LOG_INFO("Loaded %d skills from %s", loaded, skills_dir);
    return AGENTC_OK;
}

agentc_err_t ac_skills_load_file(ac_skills_t *skills, const char *filepath) {
    if (!skills || !filepath) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* TODO: Implement YAML parsing for skill files
     * Expected format:
     * name: Skill Name
     * description: Skill description
     * tools:
     *   - tool1
     *   - tool2
     * prompt_template: Optional prompt
     */
    
    AC_LOG_WARN("Skill YAML parsing not yet implemented: %s", filepath);
    return AGENTC_ERR_NOT_IMPLEMENTED;
}

agentc_err_t ac_skills_add(
    ac_skills_t *skills,
    const char *name,
    const char *description,
    const char **tool_names,
    size_t tool_count
) {
    if (!skills || !name) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    ac_skill_t *skill = calloc(1, sizeof(ac_skill_t));
    if (!skill) {
        AC_LOG_ERROR("Failed to allocate skill");
        return AGENTC_ERR_MEMORY;
    }
    
    skill->name = strdup(name);
    skill->description = description ? strdup(description) : NULL;
    skill->enabled = 1;  /* Enabled by default */
    
    /* Copy tool names */
    if (tool_count > 0 && tool_names) {
        skill->tool_names = calloc(tool_count, sizeof(char*));
        if (!skill->tool_names) {
            free(skill->name);
            free(skill->description);
            free(skill);
            return AGENTC_ERR_MEMORY;
        }
        
        for (size_t i = 0; i < tool_count; i++) {
            skill->tool_names[i] = strdup(tool_names[i]);
            if (!skill->tool_names[i]) {
                /* Cleanup on error */
                for (size_t j = 0; j < i; j++) {
                    free(skill->tool_names[j]);
                }
                free(skill->tool_names);
                free(skill->name);
                free(skill->description);
                free(skill);
                return AGENTC_ERR_MEMORY;
            }
        }
        skill->tool_count = tool_count;
    }
    
    /* Add to list */
    skill->next = skills->head;
    skills->head = skill;
    skills->count++;
    
    AC_LOG_DEBUG("Added skill: %s (%zu tools)", name, tool_count);
    
    return AGENTC_OK;
}

agentc_err_t ac_skills_enable(ac_skills_t *skills, const char *name) {
    if (!skills || !name) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (strcmp(skill->name, name) == 0) {
            skill->enabled = 1;
            AC_LOG_DEBUG("Enabled skill: %s", name);
            return AGENTC_OK;
        }
        skill = skill->next;
    }
    
    AC_LOG_WARN("Skill not found: %s", name);
    return AGENTC_ERR_NOT_FOUND;
}

agentc_err_t ac_skills_disable(ac_skills_t *skills, const char *name) {
    if (!skills || !name) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (strcmp(skill->name, name) == 0) {
            skill->enabled = 0;
            AC_LOG_DEBUG("Disabled skill: %s", name);
            return AGENTC_OK;
        }
        skill = skill->next;
    }
    
    return AGENTC_ERR_NOT_FOUND;
}

void ac_skills_enable_all(ac_skills_t *skills) {
    if (!skills) {
        return;
    }
    
    ac_skill_t *skill = skills->head;
    while (skill) {
        skill->enabled = 1;
        skill = skill->next;
    }
    
    AC_LOG_DEBUG("Enabled all skills");
}

void ac_skills_disable_all(ac_skills_t *skills) {
    if (!skills) {
        return;
    }
    
    ac_skill_t *skill = skills->head;
    while (skill) {
        skill->enabled = 0;
        skill = skill->next;
    }
    
    AC_LOG_DEBUG("Disabled all skills");
}

const ac_skill_t *ac_skills_list_enabled(const ac_skills_t *skills) {
    /* TODO: Build filtered list of enabled skills */
    return skills ? skills->head : NULL;
}

const ac_skill_t *ac_skills_list_all(const ac_skills_t *skills) {
    return skills ? skills->head : NULL;
}

size_t ac_skills_count(const ac_skills_t *skills, int enabled_only) {
    if (!skills) {
        return 0;
    }
    
    if (!enabled_only) {
        return skills->count;
    }
    
    size_t count = 0;
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->enabled) {
            count++;
        }
        skill = skill->next;
    }
    
    return count;
}

const ac_skill_t *ac_skills_find(const ac_skills_t *skills, const char *name) {
    if (!skills || !name) {
        return NULL;
    }
    
    ac_skill_t *skill = skills->head;
    while (skill) {
        if (strcmp(skill->name, name) == 0) {
            return skill;
        }
        skill = skill->next;
    }
    
    return NULL;
}

agentc_err_t ac_skills_validate_tools(
    const ac_skills_t *skills,
    const ac_tool_group_t *tool_registry
) {
    if (!skills || !tool_registry) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* TODO: Validate that tools referenced by skills exist in registry */
    
    AC_LOG_WARN("Skill tool validation not yet implemented");
    return AGENTC_OK;
}

char *ac_skills_build_prompt(const ac_skills_t *skills, const char *base_prompt) {
    if (!skills) {
        return base_prompt ? strdup(base_prompt) : NULL;
    }
    
    /* TODO: Build combined prompt from enabled skills */
    
    return base_prompt ? strdup(base_prompt) : NULL;
}

void ac_skills_clear(ac_skills_t *skills) {
    if (!skills) {
        return;
    }
    
    ac_skill_t *curr = skills->head;
    while (curr) {
        ac_skill_t *next = curr->next;
        free(curr->name);
        free(curr->description);
        free(curr->prompt_template);
        
        if (curr->tool_names) {
            for (size_t i = 0; i < curr->tool_count; i++) {
                free(curr->tool_names[i]);
            }
            free(curr->tool_names);
        }
        
        free(curr);
        curr = next;
    }
    
    skills->head = NULL;
    skills->count = 0;
}

void ac_skills_destroy(ac_skills_t *skills) {
    if (!skills) {
        return;
    }
    
    ac_skills_clear(skills);
    free(skills);
    
    AC_LOG_DEBUG("Destroyed skills manager");
}
