/**
 * @file skills.h
 * @brief Skills Management System (Hosted Feature)
 *
 * Manage reusable skill templates with tools and prompts.
 * This is a hosted feature requiring filesystem access.
 */

#ifndef AGENTC_HOSTED_SKILLS_H
#define AGENTC_HOSTED_SKILLS_H

#include <agentc/platform.h>
#include <agentc/tool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Skill Definition
 *============================================================================*/

typedef struct ac_skill {
    char *name;                        /* Skill name */
    char *description;                 /* Skill description */
    char *prompt_template;             /* Optional prompt template */
    char **tool_names;                 /* Array of tool names */
    size_t tool_count;                 /* Number of tools */
    int enabled;                       /* 1 = enabled, 0 = disabled */
    void *metadata;                    /* Optional metadata */
    struct ac_skill *next;
} ac_skill_t;

/*============================================================================
 * Skills Manager
 *============================================================================*/

typedef struct ac_skills ac_skills_t;

/**
 * @brief Create skills manager
 *
 * @return Skills manager handle, NULL on error
 */
ac_skills_t *ac_skills_create(void);

/**
 * @brief Load skills from directory
 *
 * Scans directory for .yaml/.yml files and loads skill definitions.
 *
 * @param skills     Skills manager
 * @param skills_dir Skills directory path
 * @return AGENTC_OK on success
 */
agentc_err_t ac_skills_load_dir(
    ac_skills_t *skills,
    const char *skills_dir
);

/**
 * @brief Load a single skill file
 *
 * @param skills    Skills manager
 * @param filepath  Skill file path
 * @return AGENTC_OK on success
 */
agentc_err_t ac_skills_load_file(
    ac_skills_t *skills,
    const char *filepath
);

/**
 * @brief Add a skill manually
 *
 * @param skills      Skills manager
 * @param name        Skill name
 * @param description Skill description
 * @param tool_names  Array of tool names
 * @param tool_count  Number of tools
 * @return AGENTC_OK on success
 */
agentc_err_t ac_skills_add(
    ac_skills_t *skills,
    const char *name,
    const char *description,
    const char **tool_names,
    size_t tool_count
);

/**
 * @brief Enable a skill by name
 *
 * @param skills  Skills manager
 * @param name    Skill name
 * @return AGENTC_OK on success, AGENTC_ERR_NOT_FOUND if not found
 */
agentc_err_t ac_skills_enable(
    ac_skills_t *skills,
    const char *name
);

/**
 * @brief Disable a skill by name
 *
 * @param skills  Skills manager
 * @param name    Skill name
 * @return AGENTC_OK on success
 */
agentc_err_t ac_skills_disable(
    ac_skills_t *skills,
    const char *name
);

/**
 * @brief Enable all skills
 *
 * @param skills  Skills manager
 */
void ac_skills_enable_all(ac_skills_t *skills);

/**
 * @brief Disable all skills
 *
 * @param skills  Skills manager
 */
void ac_skills_disable_all(ac_skills_t *skills);

/**
 * @brief Get enabled skills as linked list
 *
 * @param skills  Skills manager
 * @return Head of enabled skills list (do not free)
 */
const ac_skill_t *ac_skills_list_enabled(const ac_skills_t *skills);

/**
 * @brief Get all skills as linked list
 *
 * @param skills  Skills manager
 * @return Head of all skills list (do not free)
 */
const ac_skill_t *ac_skills_list_all(const ac_skills_t *skills);

/**
 * @brief Get skill count
 *
 * @param skills      Skills manager
 * @param enabled_only Count only enabled skills
 * @return Number of skills
 */
size_t ac_skills_count(const ac_skills_t *skills, int enabled_only);

/**
 * @brief Find skill by name
 *
 * @param skills  Skills manager
 * @param name    Skill name
 * @return Skill definition, NULL if not found
 */
const ac_skill_t *ac_skills_find(const ac_skills_t *skills, const char *name);

/**
 * @brief Register enabled skills' tools to tool registry
 *
 * This function assumes tools are already registered in the tool registry
 * by name. It validates that the tools referenced by enabled skills exist.
 *
 * @param skills        Skills manager
 * @param tool_registry AgentC tool registry
 * @return AGENTC_OK on success
 */
agentc_err_t ac_skills_validate_tools(
    const ac_skills_t *skills,
    const ac_tools_t *tool_registry
);

/**
 * @brief Build combined prompt template from enabled skills
 *
 * @param skills       Skills manager
 * @param base_prompt  Base prompt (optional)
 * @return Combined prompt string (caller must free), NULL on error
 */
char *ac_skills_build_prompt(
    const ac_skills_t *skills,
    const char *base_prompt
);

/**
 * @brief Clear all skills
 *
 * @param skills  Skills manager
 */
void ac_skills_clear(ac_skills_t *skills);

/**
 * @brief Destroy skills manager
 *
 * @param skills  Skills manager to destroy
 */
void ac_skills_destroy(ac_skills_t *skills);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_HOSTED_SKILLS_H */
