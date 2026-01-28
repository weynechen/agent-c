/**
 * @file moc_comment.c
 * @brief Doxygen-style comment parser for MOC
 *
 * Parses @description and @param tags from C comment blocks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "moc.h"

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * Skip whitespace characters
 */
static const char *skip_whitespace(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/**
 * Skip to end of current line
 */
static const char *skip_to_eol(const char *p, const char *end) {
    while (p < end && *p != '\n' && *p != '\r') {
        p++;
    }
    return p;
}

/**
 * Skip line prefix (whitespace, asterisks)
 */
static const char *skip_line_prefix(const char *p, const char *end) {
    p = skip_whitespace(p, end);
    if (p < end && *p == '*') {
        p++;
        if (p < end && *p == ' ') p++;
    }
    return p;
}

/**
 * Extract content after a tag until end of line or next tag
 */
static void extract_tag_content(const char *start, const char *end, 
                                char *dest, size_t dest_sz) {
    /* Skip leading whitespace */
    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }

    /* Find end of content (EOL or next @ tag) */
    const char *content_end = start;
    while (content_end < end) {
        if (*content_end == '\n' || *content_end == '\r') {
            /* Check if next line continues with more content */
            const char *next_line = content_end;
            while (next_line < end && (*next_line == '\n' || *next_line == '\r')) {
                next_line++;
            }
            /* Skip line prefix */
            const char *next_content = skip_line_prefix(next_line, end);
            /* If next line starts with @ or end of comment, stop */
            if (next_content >= end || *next_content == '@' || 
                (*next_content == '*' && next_content + 1 < end && *(next_content + 1) == '/')) {
                break;
            }
            /* Continue to next line */
            content_end = next_line;
            continue;
        }
        content_end++;
    }

    /* Copy content, collapsing whitespace */
    size_t len = 0;
    const char *p = start;
    bool prev_space = false;
    bool in_newline_seq = false;

    while (p < content_end && len < dest_sz - 1) {
        if (*p == '\n' || *p == '\r') {
            in_newline_seq = true;
            p++;
            continue;
        }

        if (in_newline_seq) {
            /* Skip line prefix after newline */
            while (p < content_end && (isspace((unsigned char)*p) || *p == '*')) {
                p++;
            }
            in_newline_seq = false;
            if (!prev_space && len > 0) {
                dest[len++] = ' ';
                prev_space = true;
            }
            continue;
        }

        if (isspace((unsigned char)*p)) {
            if (!prev_space && len > 0) {
                dest[len++] = ' ';
                prev_space = true;
            }
            p++;
        } else {
            dest[len++] = *p++;
            prev_space = false;
        }
    }

    /* Trim trailing space */
    if (len > 0 && dest[len - 1] == ' ') {
        len--;
    }

    dest[len] = '\0';
}

/**
 * Parse a @param tag
 *
 * Format: @param[:] name description
 */
static int parse_param_tag(const char *start, const char *end, moc_tool_t *tool) {
    /* Skip @param */
    start += 6; /* strlen("@param") */

    /* Skip optional colon */
    if (start < end && *start == ':') {
        start++;
    }

    /* Skip whitespace */
    start = skip_whitespace(start, end);
    if (start >= end) return -1;

    /* Extract parameter name */
    const char *name_start = start;
    while (start < end && !isspace((unsigned char)*start)) {
        start++;
    }

    if (start == name_start) return -1;

    char param_name[MOC_MAX_NAME_LEN];
    size_t name_len = start - name_start;
    if (name_len >= MOC_MAX_NAME_LEN) {
        name_len = MOC_MAX_NAME_LEN - 1;
    }
    memcpy(param_name, name_start, name_len);
    param_name[name_len] = '\0';

    /* Find matching parameter in tool */
    for (int i = 0; i < tool->param_count; i++) {
        if (strcmp(tool->params[i].name, param_name) == 0) {
            /* Extract description */
            extract_tag_content(start, end, 
                               tool->params[i].description,
                               MOC_MAX_DESC_LEN);
            return 0;
        }
    }

    /* Parameter not found - might be documentation for a param we haven't parsed yet */
    /* Store it anyway if we have room in the params array */
    if (tool->param_count < MOC_MAX_PARAMS) {
        moc_param_t *param = &tool->params[tool->param_count];
        strncpy(param->name, param_name, MOC_MAX_NAME_LEN - 1);
        param->name[MOC_MAX_NAME_LEN - 1] = '\0';
        extract_tag_content(start, end, param->description, MOC_MAX_DESC_LEN);
        /* Don't increment param_count - let the parser do that */
    }

    return 0;
}

/*============================================================================
 * Main Comment Parsing Function
 *============================================================================*/

/**
 * Parse Doxygen-style comment block
 *
 * Supports:
 * - @description: Description text
 * - @param name Description
 * - @param: name Description (with colon)
 *
 * Both C-style and C++-style comments are supported:
 * - Block comments: / * ... * /
 * - Line comments: //
 */
int moc_parse_comment(const char *comment_text, size_t comment_len, moc_tool_t *tool) {
    if (!comment_text || comment_len == 0 || !tool) {
        return -1;
    }

    const char *p = comment_text;
    const char *end = comment_text + comment_len;

    /* Detect comment type and skip delimiters */
    if (comment_len >= 2 && p[0] == '/' && p[1] == '*') {
        /* Block comment - skip opening delimiter */
        p += 2;
        /* Adjust end to skip closing delimiter if present */
        if (comment_len >= 4 && end[-1] == '/' && end[-2] == '*') {
            end -= 2;
        }
    } else if (comment_len >= 2 && p[0] == '/' && p[1] == '/') {
        /* Line comment - skip // */
        p += 2;
    }

    /* Process comment content */
    while (p < end) {
        p = skip_line_prefix(p, end);
        if (p >= end) break;

        /* Look for @ tags */
        if (*p == '@') {
            /* Check for @description tag */
            if (strncmp(p, "@description", 12) == 0) {
                const char *content_start = p + 12;
                /* Skip optional colon */
                if (content_start < end && *content_start == ':') {
                    content_start++;
                }
                /* Find end of tag content */
                const char *tag_end = content_start;
                while (tag_end < end) {
                    if (*tag_end == '@' && tag_end > content_start) break;
                    if (*tag_end == '*' && tag_end + 1 < end && *(tag_end + 1) == '/') break;
                    tag_end++;
                }
                extract_tag_content(content_start, tag_end, 
                                   tool->description, MOC_MAX_DESC_LEN);
                p = tag_end;
                continue;
            }
            /* Check for @param tag */
            else if (strncmp(p, "@param", 6) == 0) {
                const char *tag_end = p + 6;
                /* Skip optional colon */
                if (tag_end < end && *tag_end == ':') {
                    tag_end++;
                }
                /* Find end of this tag */
                tag_end = skip_whitespace(tag_end, end);
                while (tag_end < end) {
                    if (*tag_end == '@' && tag_end > p + 6) break;
                    if (*tag_end == '*' && tag_end + 1 < end && *(tag_end + 1) == '/') break;
                    tag_end++;
                }
                parse_param_tag(p, tag_end, tool);
                p = tag_end;
                continue;
            }
        }

        /* Skip to next line */
        p = skip_to_eol(p, end);
        if (p < end) p++;
    }

    return 0;
}
