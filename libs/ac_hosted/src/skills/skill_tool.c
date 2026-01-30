/**
 * @file skill_tool.c
 * @brief Skill loading tool for Agent
 *
 * Provides a tool that allows the Agent to dynamically load skill content.
 * The tool description includes available skills, and when called,
 * returns the full skill instructions.
 */

#include "skills_internal.h"
#include <arc/log.h>
#include <arc/tool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Tool Implementation
 *============================================================================*/

/**
 * @brief Execute skill tool - load a skill by name
 */
static char *skill_tool_execute(
    const ac_tool_ctx_t *ctx,
    const char *args_json,
    void *priv
) {
    (void)ctx;
    ac_skills_t *skills = (ac_skills_t *)priv;

    if (!skills || !args_json) {
        return strdup("{\"error\": \"Invalid arguments\"}");
    }

    /* Parse skill name from JSON: {"name": "skill-name"} */
    /* Simple parsing - find "name": "value" */
    const char *name_key = strstr(args_json, "\"name\"");
    if (!name_key) {
        return strdup("{\"error\": \"Missing 'name' parameter\"}");
    }

    const char *colon = strchr(name_key, ':');
    if (!colon) {
        return strdup("{\"error\": \"Invalid JSON format\"}");
    }

    /* Find opening quote of value */
    const char *quote_start = strchr(colon, '"');
    if (!quote_start) {
        return strdup("{\"error\": \"Invalid JSON format\"}");
    }
    quote_start++; /* Skip the quote */

    /* Find closing quote */
    const char *quote_end = strchr(quote_start, '"');
    if (!quote_end) {
        return strdup("{\"error\": \"Invalid JSON format\"}");
    }

    /* Extract skill name */
    size_t name_len = quote_end - quote_start;
    char *skill_name = malloc(name_len + 1);
    if (!skill_name) {
        return strdup("{\"error\": \"Memory allocation failed\"}");
    }
    memcpy(skill_name, quote_start, name_len);
    skill_name[name_len] = '\0';

    AC_LOG_INFO("Skill tool: loading skill '%s'", skill_name);

    /* Find the skill */
    const ac_skill_t *skill = ac_skills_find(skills, skill_name);
    if (!skill) {
        /* Build error with available skills */
        char *result = malloc(1024);
        if (!result) {
            free(skill_name);
            return strdup("{\"error\": \"Memory allocation failed\"}");
        }

        char *p = result;
        p += sprintf(p, "{\"error\": \"Skill '%s' not found\", \"available_skills\": [", skill_name);

        const ac_skill_t *s = ac_skills_list(skills);
        bool first = true;
        while (s) {
            if (!first) p += sprintf(p, ", ");
            p += sprintf(p, "\"%s\"", s->meta.name);
            first = false;
            s = s->next;
        }
        p += sprintf(p, "]}");

        free(skill_name);
        return result;
    }

    /* Enable the skill to load content if not already loaded */
    arc_err_t err = ac_skills_enable(skills, skill_name);
    if (err != ARC_OK) {
        free(skill_name);
        return strdup("{\"error\": \"Failed to load skill content\"}");
    }

    /* Re-fetch to get updated content */
    skill = ac_skills_find(skills, skill_name);
    if (!skill || !skill->content) {
        free(skill_name);
        return strdup("{\"error\": \"Skill content not available\"}");
    }

    /* Build result with skill content */
    size_t content_len = strlen(skill->content);
    size_t dir_len = skill->dir_path ? strlen(skill->dir_path) : 0;
    size_t result_size = content_len + dir_len + name_len + 256;

    char *result = malloc(result_size);
    if (!result) {
        free(skill_name);
        return strdup("{\"error\": \"Memory allocation failed\"}");
    }

    /* Format output similar to the TypeScript version */
    snprintf(result, result_size,
        "## Skill: %s\n\n"
        "**Base directory**: %s\n\n"
        "%s",
        skill_name,
        skill->dir_path ? skill->dir_path : ".",
        skill->content
    );

    free(skill_name);

    AC_LOG_DEBUG("Skill tool: loaded %zu bytes of content", strlen(result));
    return result;
}

/*============================================================================
 * Public API
 *============================================================================*/

char *ac_skills_build_tool_description(const ac_skills_t *skills) {
    if (!skills) {
        return strdup("Load a skill to get detailed instructions for a specific task. No skills are currently available.");
    }

    size_t count = ac_skills_count(skills);
    if (count == 0) {
        return strdup("Load a skill to get detailed instructions for a specific task. No skills are currently available.");
    }

    /* Calculate size */
    size_t total_size = 512; /* Header + footer */
    const ac_skill_t *skill = ac_skills_list(skills);
    while (skill) {
        total_size += 100; /* Tags overhead */
        total_size += strlen(skill->meta.name);
        total_size += strlen(skill->meta.description);
        skill = skill->next;
    }

    char *desc = malloc(total_size);
    if (!desc) return NULL;

    char *p = desc;
    p += sprintf(p,
        "Load a skill to get detailed instructions for a specific task. "
        "Skills provide specialized knowledge and step-by-step guidance. "
        "Use this when a task matches an available skill's description.\n"
        "<available_skills>\n"
    );

    skill = ac_skills_list(skills);
    while (skill) {
        p += sprintf(p,
            "  <skill>\n"
            "    <name>%s</name>\n"
            "    <description>%s</description>\n"
            "  </skill>\n",
            skill->meta.name,
            skill->meta.description
        );
        skill = skill->next;
    }

    p += sprintf(p, "</available_skills>");

    return desc;
}

ac_tool_t *ac_skills_create_tool(ac_skills_t *skills) {
    if (!skills) return NULL;

    ac_tool_t *tool = calloc(1, sizeof(ac_tool_t));
    if (!tool) return NULL;

    tool->name = "skill";
    tool->description = ac_skills_build_tool_description(skills);
    tool->parameters =
        "{"
        "\"type\": \"object\","
        "\"properties\": {"
        "  \"name\": {"
        "    \"type\": \"string\","
        "    \"description\": \"The skill identifier from available_skills (e.g., 'code-review' or 'debugging')\""
        "  }"
        "},"
        "\"required\": [\"name\"]"
        "}";
    tool->execute = skill_tool_execute;
    tool->priv = skills;

    AC_LOG_INFO("Created skill tool with %zu available skills", ac_skills_count(skills));

    return tool;
}

void ac_skills_destroy_tool(ac_tool_t *tool) {
    if (!tool) return;

    /* Free the dynamically allocated description */
    free((void *)tool->description);
    free(tool);
}
