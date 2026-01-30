/**
 * @file skill_prompt.c
 * @brief Skill prompt generation
 *
 * Generates prompts for skill discovery and activation.
 */

#include "skills_internal.h"
#include <agentc/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Constants
 *============================================================================*/

/*
 * Discovery prompt format following agentskills.io specification:
 * https://agentskills.io/integrate-skills
 *
 * For Claude models, use XML format:
 * <available_skills>
 *   <skill>
 *     <name>...</name>
 *     <description>...</description>
 *   </skill>
 * </available_skills>
 */
static const char *DISCOVERY_HEADER = "<available_skills>\n";
static const char *DISCOVERY_FOOTER = "</available_skills>\n";

static const char *ACTIVE_HEADER = "<active-skills>\n\n";
static const char *ACTIVE_FOOTER = "</active-skills>\n";

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Calculate buffer size for string concatenation
 */
static size_t calc_concat_size(const char *s1, const char *s2, const char *s3) {
    size_t size = 1; /* null terminator */
    if (s1) size += strlen(s1);
    if (s2) size += strlen(s2);
    if (s3) size += strlen(s3);
    return size;
}

/*============================================================================
 * Public Functions
 *============================================================================*/

char *skill_format_discovery(const ac_skill_t *skill) {
    if (!skill || !skill->meta.name || !skill->meta.description) {
        return NULL;
    }
    
    /* Format: "- name: description\n" */
    size_t name_len = strlen(skill->meta.name);
    size_t desc_len = strlen(skill->meta.description);
    size_t total = 4 + name_len + desc_len + 1; /* "- " + name + ": " + desc + "\n" + null */
    
    char *result = malloc(total);
    if (!result) return NULL;
    
    snprintf(result, total, "- %s: %s\n", skill->meta.name, skill->meta.description);
    return result;
}

char *skill_format_active(const ac_skill_t *skill) {
    if (!skill || !skill->meta.name) {
        return NULL;
    }
    
    const char *content = skill->content ? skill->content : "";
    
    /* Format:
     * <skill name="name">
     * content
     * </skill>
     */
    
    size_t name_len = strlen(skill->meta.name);
    size_t content_len = strlen(content);
    
    /* <skill name=""> + content + \n</skill>\n\n + null */
    size_t total = 14 + name_len + 2 + content_len + 12 + 1;
    
    char *result = malloc(total);
    if (!result) return NULL;
    
    /* Build the formatted string */
    char *p = result;
    
    /* Opening tag */
    p += sprintf(p, "<skill name=\"%s\">\n", skill->meta.name);
    
    /* Content */
    if (content_len > 0) {
        memcpy(p, content, content_len);
        p += content_len;
        /* Ensure newline before closing tag */
        if (content[content_len - 1] != '\n') {
            *p++ = '\n';
        }
    }
    
    /* Closing tag */
    p += sprintf(p, "</skill>\n\n");
    
    return result;
}

char *ac_skills_build_discovery_prompt(const ac_skills_t *skills) {
    if (!skills || !skills->head) {
        return NULL;
    }
    
    /* Calculate total size for XML format:
     * <available_skills>
     *   <skill>
     *     <name>...</name>
     *     <description>...</description>
     *   </skill>
     * </available_skills>
     */
    size_t total_size = strlen(DISCOVERY_HEADER) + strlen(DISCOVERY_FOOTER) + 1;
    
    const ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->meta.name && skill->meta.description) {
            /* XML tags overhead + content */
            total_size += 80; /* <skill>\n  <name></name>\n  <description></description>\n</skill>\n */
            total_size += strlen(skill->meta.name);
            total_size += strlen(skill->meta.description);
        }
        skill = skill->next;
    }
    
    /* Allocate buffer */
    char *prompt = malloc(total_size);
    if (!prompt) {
        AC_LOG_ERROR("Failed to allocate discovery prompt buffer");
        return NULL;
    }
    
    /* Build prompt */
    char *p = prompt;
    
    /* Header */
    size_t header_len = strlen(DISCOVERY_HEADER);
    memcpy(p, DISCOVERY_HEADER, header_len);
    p += header_len;
    
    /* Skill entries in XML format */
    skill = skills->head;
    while (skill) {
        if (skill->meta.name && skill->meta.description) {
            p += sprintf(p, 
                "  <skill>\n"
                "    <name>%s</name>\n"
                "    <description>%s</description>\n"
                "  </skill>\n",
                skill->meta.name, skill->meta.description);
        }
        skill = skill->next;
    }
    
    /* Footer */
    size_t footer_len = strlen(DISCOVERY_FOOTER);
    memcpy(p, DISCOVERY_FOOTER, footer_len);
    p += footer_len;
    
    *p = '\0';
    
    AC_LOG_DEBUG("Built discovery prompt (%zu bytes, %zu skills)", 
                 p - prompt, skills->count);
    
    return prompt;
}

char *ac_skills_build_active_prompt(const ac_skills_t *skills) {
    if (!skills || skills->enabled_count == 0) {
        return NULL;
    }
    
    /* First pass: calculate total size */
    size_t total_size = strlen(ACTIVE_HEADER) + strlen(ACTIVE_FOOTER) + 1;
    
    const ac_skill_t *skill = skills->head;
    while (skill) {
        if (skill->state == AC_SKILL_ENABLED && skill->meta.name) {
            const char *content = skill->content ? skill->content : "";
            /* <skill name="name">\ncontent\n</skill>\n\n */
            total_size += 14 + strlen(skill->meta.name) + 2 + strlen(content) + 12;
        }
        skill = skill->next;
    }
    
    /* Allocate buffer */
    char *prompt = malloc(total_size);
    if (!prompt) {
        AC_LOG_ERROR("Failed to allocate active prompt buffer");
        return NULL;
    }
    
    /* Build prompt */
    char *p = prompt;
    
    /* Header */
    size_t header_len = strlen(ACTIVE_HEADER);
    memcpy(p, ACTIVE_HEADER, header_len);
    p += header_len;
    
    /* Enabled skills */
    skill = skills->head;
    while (skill) {
        if (skill->state == AC_SKILL_ENABLED && skill->meta.name) {
            char *formatted = skill_format_active(skill);
            if (formatted) {
                size_t len = strlen(formatted);
                memcpy(p, formatted, len);
                p += len;
                free(formatted);
            }
        }
        skill = skill->next;
    }
    
    /* Footer */
    size_t footer_len = strlen(ACTIVE_FOOTER);
    memcpy(p, ACTIVE_FOOTER, footer_len);
    p += footer_len;
    
    *p = '\0';
    
    AC_LOG_DEBUG("Built active prompt (%zu bytes, %zu enabled skills)", 
                 p - prompt, skills->enabled_count);
    
    return prompt;
}
