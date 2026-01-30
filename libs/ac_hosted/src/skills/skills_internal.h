/**
 * @file skills_internal.h
 * @brief Skills system internal structures and functions
 */

#ifndef ARC_SKILLS_INTERNAL_H
#define ARC_SKILLS_INTERNAL_H

#include <arc/skills.h>

/*============================================================================
 * Internal Structures
 *============================================================================*/

/**
 * @brief Skills manager internal structure
 */
struct ac_skills {
    ac_skill_t *head;               /* Linked list of skills */
    size_t count;                   /* Total discovered skills */
    size_t enabled_count;           /* Currently enabled skills */

    /* Script executor (reserved for future use) */
    ac_skill_script_fn script_executor;
    void *script_user_data;
};

/*============================================================================
 * Parser Functions (skill_parser.c)
 *============================================================================*/

/**
 * @brief Parse SKILL.md file content
 *
 * Extracts YAML frontmatter and markdown body from SKILL.md content.
 * Frontmatter must be enclosed between "---" markers.
 *
 * @param content     Full file content
 * @param meta        Output metadata structure (caller allocates)
 * @param body_start  Output pointer to markdown body start (within content)
 * @return ARC_OK on success, ARC_ERR_PARSE on parse error
 */
arc_err_t skill_parse_frontmatter(
    const char *content,
    ac_skill_meta_t *meta,
    const char **body_start
);

/**
 * @brief Free skill metadata fields
 *
 * Frees all dynamically allocated fields in metadata structure.
 * Does not free the structure itself.
 *
 * @param meta  Metadata structure to free fields from
 */
void skill_meta_free(ac_skill_meta_t *meta);

/**
 * @brief Validate skill name format
 *
 * Validates according to agentskills.io specification:
 * - 1-64 characters
 * - Lowercase alphanumeric and hyphens only
 * - Cannot start or end with hyphen
 * - No consecutive hyphens
 *
 * @param name  Skill name to validate
 * @return true if valid, false otherwise
 */
bool skill_validate_name(const char *name);

/*============================================================================
 * Prompt Builder Functions (skill_prompt.c)
 *============================================================================*/

/**
 * @brief Format a single skill for discovery prompt
 *
 * Generates: "- name: description\n"
 *
 * @param skill  Skill to format
 * @return Formatted string (caller must free), NULL on error
 */
char *skill_format_discovery(const ac_skill_t *skill);

/**
 * @brief Format a single skill for active prompt
 *
 * Generates:
 *   <skill name="name">
 *   [markdown content]
 *   </skill>
 *
 * @param skill  Skill to format (must have content loaded)
 * @return Formatted string (caller must free), NULL on error
 */
char *skill_format_active(const ac_skill_t *skill);

/*============================================================================
 * File Utilities
 *============================================================================*/

/**
 * @brief Read entire file content
 *
 * @param filepath  Path to file
 * @return File content (caller must free), NULL on error
 */
char *skill_read_file(const char *filepath);

/**
 * @brief Check if file exists
 *
 * @param filepath  Path to file
 * @return true if file exists and is readable
 */
bool skill_file_exists(const char *filepath);

#endif /* ARC_SKILLS_INTERNAL_H */
