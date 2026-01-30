/**
 * @file skills.h
 * @brief Agent Skills System (Hosted Feature)
 *
 * Progressive skill loading following agentskills.io specification.
 * Skills are discovered from directories containing SKILL.md files.
 *
 * Key concepts:
 * - Discovery: Load only metadata (name, description) for efficiency
 * - Activation: Load full content when skill is enabled
 * - Execution: Run associated scripts (reserved for future)
 */

#ifndef ARC_HOSTED_SKILLS_H
#define ARC_HOSTED_SKILLS_H

#include <arc/error.h>
#include <arc/tool.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Skill State
 *============================================================================*/

typedef enum {
    AC_SKILL_DISCOVERED,    /* Only metadata loaded */
    AC_SKILL_ENABLED,       /* Full content loaded, active */
    AC_SKILL_DISABLED,      /* Explicitly disabled */
} ac_skill_state_t;

/*============================================================================
 * Skill Metadata (Frontmatter)
 *============================================================================*/

/**
 * @brief Skill metadata parsed from YAML frontmatter
 *
 * Required fields:
 * - name: 1-64 chars, lowercase alphanumeric + hyphen
 * - description: 1-1024 chars, when to use this skill
 *
 * Optional fields:
 * - license: License identifier or file reference
 * - compatibility: Environment requirements
 * - allowed_tools: Pre-approved tool names
 */
typedef struct {
    char *name;                     /* Required: skill identifier */
    char *description;              /* Required: when to use this skill */
    char *license;                  /* Optional: license identifier */
    char *compatibility;            /* Optional: environment requirements */
    char **allowed_tools;           /* Optional: pre-approved tool names */
    size_t allowed_tools_count;     /* Number of allowed tools */
} ac_skill_meta_t;

/*============================================================================
 * Skill Definition
 *============================================================================*/

/**
 * @brief Complete skill definition
 */
typedef struct ac_skill {
    ac_skill_meta_t meta;           /* Parsed frontmatter */
    char *content;                  /* Markdown body (NULL if not loaded) */
    char *dir_path;                 /* Skill directory path */
    ac_skill_state_t state;         /* Current state */
    struct ac_skill *next;          /* Linked list pointer */
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
 * @brief Destroy skills manager and free all resources
 *
 * @param skills  Skills manager to destroy
 */
void ac_skills_destroy(ac_skills_t *skills);

/*============================================================================
 * Discovery & Loading
 *============================================================================*/

/**
 * @brief Discover skills from directory (metadata only)
 *
 * Scans directory for subdirectories containing SKILL.md files.
 * Only loads name and description for efficient discovery.
 *
 * Directory structure expected:
 *   skills_dir/
 *     skill-name/
 *       SKILL.md
 *       scripts/      (optional)
 *       references/   (optional)
 *       assets/       (optional)
 *
 * @param skills     Skills manager
 * @param skills_dir Base directory containing skill folders
 * @return ARC_OK on success
 */
arc_err_t ac_skills_discover_dir(
    ac_skills_t *skills,
    const char *skills_dir
);

/**
 * @brief Discover a single skill from directory
 *
 * Loads metadata from SKILL.md in the specified directory.
 *
 * @param skills    Skills manager
 * @param skill_dir Directory containing SKILL.md
 * @return ARC_OK on success, ARC_ERR_NOT_FOUND if no SKILL.md
 */
arc_err_t ac_skills_discover(
    ac_skills_t *skills,
    const char *skill_dir
);

/*============================================================================
 * Skill Activation
 *============================================================================*/

/**
 * @brief Enable a skill (loads full content if not already loaded)
 *
 * When enabled, the skill's full markdown content is loaded and
 * will be included in the active prompt.
 *
 * @param skills  Skills manager
 * @param name    Skill name
 * @return ARC_OK on success, ARC_ERR_NOT_FOUND if not discovered
 */
arc_err_t ac_skills_enable(
    ac_skills_t *skills,
    const char *name
);

/**
 * @brief Disable a skill
 *
 * Disabled skills are not included in the active prompt.
 * Content remains loaded for quick re-enable.
 *
 * @param skills  Skills manager
 * @param name    Skill name
 * @return ARC_OK on success
 */
arc_err_t ac_skills_disable(
    ac_skills_t *skills,
    const char *name
);

/**
 * @brief Enable all discovered skills
 *
 * @param skills  Skills manager
 * @return Number of skills enabled
 */
size_t ac_skills_enable_all(ac_skills_t *skills);

/**
 * @brief Disable all skills
 *
 * @param skills  Skills manager
 */
void ac_skills_disable_all(ac_skills_t *skills);

/*============================================================================
 * Query Functions
 *============================================================================*/

/**
 * @brief Find skill by name
 *
 * @param skills  Skills manager
 * @param name    Skill name
 * @return Skill pointer (do not free), NULL if not found
 */
const ac_skill_t *ac_skills_find(
    const ac_skills_t *skills,
    const char *name
);

/**
 * @brief Get total discovered skill count
 *
 * @param skills  Skills manager
 * @return Number of discovered skills
 */
size_t ac_skills_count(const ac_skills_t *skills);

/**
 * @brief Get enabled skill count
 *
 * @param skills  Skills manager
 * @return Number of enabled skills
 */
size_t ac_skills_enabled_count(const ac_skills_t *skills);

/**
 * @brief Get all skills as linked list
 *
 * @param skills  Skills manager
 * @return Head of skill list (do not free)
 */
const ac_skill_t *ac_skills_list(const ac_skills_t *skills);

/*============================================================================
 * Prompt Generation
 *============================================================================*/

/**
 * @brief Build discovery prompt (list of all available skills)
 *
 * Generates a compact list of skill names and descriptions.
 * Use this in system prompt for skill awareness.
 *
 * Format:
 *   <available-skills>
 *   - skill-name: description
 *   - another-skill: description
 *   </available-skills>
 *
 * @param skills  Skills manager
 * @return Prompt string (caller must free), NULL if empty
 */
char *ac_skills_build_discovery_prompt(const ac_skills_t *skills);

/**
 * @brief Build active prompt (full instructions for enabled skills)
 *
 * Generates detailed instructions from all enabled skills.
 * Includes full SKILL.md content.
 *
 * Format:
 *   <active-skills>
 *   <skill name="skill-name">
 *   [full markdown content]
 *   </skill>
 *   </active-skills>
 *
 * @param skills  Skills manager
 * @return Prompt string (caller must free), NULL if no enabled skills
 */
char *ac_skills_build_active_prompt(const ac_skills_t *skills);

/*============================================================================
 * Tool Validation
 *============================================================================*/

/**
 * @brief Validate skill's allowed_tools against registry
 *
 * Checks if all tools listed in allowed_tools exist in the registry.
 *
 * @param skills    Skills manager
 * @param name      Skill name
 * @param registry  Tool registry to validate against
 * @return ARC_OK if all tools exist, ARC_ERR_NOT_FOUND otherwise
 */
arc_err_t ac_skills_validate_tools(
    const ac_skills_t *skills,
    const char *name,
    const ac_tool_registry_t *registry
);

/*============================================================================
 * Skill Tool (for Agent to load skills dynamically)
 *============================================================================*/

/**
 * @brief Build tool description with available skills
 *
 * Generates a description string that includes the list of available skills.
 * Used as the description for the skill loading tool.
 *
 * @param skills  Skills manager
 * @return Description string (caller must free), NULL on error
 */
char *ac_skills_build_tool_description(const ac_skills_t *skills);

/**
 * @brief Create the skill loading tool
 *
 * Creates a tool that allows the Agent to load skill content dynamically.
 * The tool's description includes available skills, and when called with
 * a skill name, returns the full skill instructions.
 *
 * Usage:
 * @code
 * ac_skills_t *skills = ac_skills_create();
 * ac_skills_discover_dir(skills, ".skills");
 *
 * // Create skill tool
 * ac_tool_t *skill_tool = ac_skills_create_tool(skills);
 *
 * // Register with agent's tool registry
 * ac_tool_registry_add(registry, skill_tool);
 *
 * // ... use agent ...
 *
 * // Cleanup
 * ac_skills_destroy_tool(skill_tool);
 * ac_skills_destroy(skills);
 * @endcode
 *
 * @param skills  Skills manager (must remain valid while tool is in use)
 * @return Tool definition (caller must destroy with ac_skills_destroy_tool)
 */
ac_tool_t *ac_skills_create_tool(ac_skills_t *skills);

/**
 * @brief Destroy skill tool
 *
 * @param tool  Tool created by ac_skills_create_tool
 */
void ac_skills_destroy_tool(ac_tool_t *tool);

/*============================================================================
 * Script Execution Interface (Reserved - Not Implemented)
 *============================================================================*/

/**
 * @brief Script execution callback type
 *
 * Reserved for future implementation of skill script execution.
 *
 * @param script_path  Absolute path to script file
 * @param args_json    JSON arguments string
 * @param user_data    User context from ac_skills_set_script_executor
 * @return Result JSON (caller must free), NULL on error
 */
typedef char* (*ac_skill_script_fn)(
    const char *script_path,
    const char *args_json,
    void *user_data
);

/**
 * @brief Set script executor callback
 *
 * Reserved for future implementation.
 *
 * @param skills    Skills manager
 * @param executor  Script execution callback
 * @param user_data User context passed to callback
 * @return ARC_ERR_NOT_IMPLEMENTED (currently not implemented)
 */
arc_err_t ac_skills_set_script_executor(
    ac_skills_t *skills,
    ac_skill_script_fn executor,
    void *user_data
);

#ifdef __cplusplus
}
#endif

#endif /* ARC_HOSTED_SKILLS_H */
