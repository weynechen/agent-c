/**
 * @file prompt_loader.c
 * @brief Prompt Loading and Rendering Implementation
 */

#include "prompt_loader.h"
#include "prompts_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Prompt Access
 *============================================================================*/

const char *prompt_get_system(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < SYSTEM_PROMPTS_COUNT; i++) {
        if (SYSTEM_PROMPTS[i].name && strcmp(SYSTEM_PROMPTS[i].name, name) == 0) {
            return SYSTEM_PROMPTS[i].content;
        }
    }
    
    return NULL;
}

const char *prompt_get_tool(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < TOOL_PROMPTS_COUNT; i++) {
        if (TOOL_PROMPTS[i].name && strcmp(TOOL_PROMPTS[i].name, name) == 0) {
            return TOOL_PROMPTS[i].content;
        }
    }
    
    return NULL;
}

/*============================================================================
 * Variable Substitution Helper
 *============================================================================*/

/**
 * @brief Replace all occurrences of a pattern in a string
 */
static char *string_replace(const char *str, const char *pattern, const char *replacement) {
    if (!str || !pattern || !replacement) return NULL;
    
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);
    
    if (pattern_len == 0) {
        return strdup(str);
    }
    
    /* Count occurrences */
    int count = 0;
    const char *p = str;
    while ((p = strstr(p, pattern)) != NULL) {
        count++;
        p += pattern_len;
    }
    
    if (count == 0) {
        return strdup(str);
    }
    
    /* Allocate result */
    size_t str_len = strlen(str);
    size_t result_len = str_len + count * (replacement_len - pattern_len);
    char *result = malloc(result_len + 1);
    if (!result) return NULL;
    
    /* Build result */
    char *dst = result;
    const char *src = str;
    
    while (*src) {
        if (strncmp(src, pattern, pattern_len) == 0) {
            memcpy(dst, replacement, replacement_len);
            dst += replacement_len;
            src += pattern_len;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    return result;
}

/*============================================================================
 * Prompt Rendering
 *============================================================================*/

char *prompt_render_system(const char *name, const char *workspace) {
    const char *content = prompt_get_system(name);
    if (!content) return NULL;
    
    /* Replace ${workspace} */
    const char *ws = workspace ? workspace : ".";
    char *result = string_replace(content, "${workspace}", ws);
    
    return result;
}

char *prompt_render_tool(const char *name, const char *workspace) {
    const char *content = prompt_get_tool(name);
    if (!content) return NULL;
    
    /* Replace ${workspace} and ${directory} */
    const char *ws = workspace ? workspace : ".";
    
    char *temp = string_replace(content, "${workspace}", ws);
    if (!temp) return NULL;
    
    char *result = string_replace(temp, "${directory}", ws);
    free(temp);
    
    return result;
}

/*============================================================================
 * Prompt Enumeration
 *============================================================================*/

int prompt_system_count(void) {
    return SYSTEM_PROMPTS_COUNT;
}

int prompt_tool_count(void) {
    return TOOL_PROMPTS_COUNT;
}

const char *prompt_system_name(int index) {
    if (index < 0 || index >= SYSTEM_PROMPTS_COUNT) {
        return NULL;
    }
    return SYSTEM_PROMPTS[index].name;
}

const char *prompt_tool_name(int index) {
    if (index < 0 || index >= TOOL_PROMPTS_COUNT) {
        return NULL;
    }
    return TOOL_PROMPTS[index].name;
}
