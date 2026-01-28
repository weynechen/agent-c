/**
 * @file moc_parser.c
 * @brief Tree-sitter based parser for MOC
 *
 * Uses Tree-sitter Query API to find AC_TOOL_META marked functions
 * and extract their metadata.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "moc.h"
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

/*============================================================================
 * Tree-sitter Query Patterns
 *============================================================================*/

/**
 * Query patterns to find function declarations.
 *
 * Due to tree-sitter parsing quirks with AC_TOOL_META (which it interprets
 * as a type_identifier), we need to handle two cases:
 *
 * 1. Direct function declarator:
 *    AC_TOOL_META int foo(int x);
 *    -> declaration > function_declarator
 *
 * 2. Pointer return type:
 *    AC_TOOL_META const char* foo(const char* x);
 *    -> declaration > pointer_declarator > function_declarator
 */
static const char *TOOL_QUERY =
    /* Match function declarations (direct) */
    "(declaration"
    "  declarator: (function_declarator"
    "    declarator: (_) @func_name"
    "    parameters: (parameter_list) @params)"
    ") @decl"
    /* Match function declarations (pointer return type) */
    "(declaration"
    "  declarator: (pointer_declarator"
    "    declarator: (function_declarator"
    "      declarator: (_) @func_name2"
    "      parameters: (parameter_list) @params2))"
    ") @decl2";

/*============================================================================
 * Internal Types
 *============================================================================*/

typedef struct {
    TSParser *parser;
    TSTree *tree;
    TSQuery *query;
    TSQueryCursor *cursor;
    const TSLanguage *language;
} parser_state_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * Extract text from a TSNode
 */
static void extract_node_text(TSNode node, const char *source,
                              char *dest, size_t dest_sz) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;

    if (len >= dest_sz) {
        len = dest_sz - 1;
    }

    memcpy(dest, source + start, len);
    dest[len] = '\0';
}

/**
 * Check if a declaration has AC_TOOL_META marker
 *
 * Tree-sitter parses AC_TOOL_META (an empty macro) as a type_identifier,
 * not a storage_class_specifier. We need to check for this specific case.
 */
static bool has_tool_meta_marker(TSNode decl, const char *source) {
    uint32_t child_count = ts_node_child_count(decl);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(decl, i);
        const char *type = ts_node_type(child);

        /* Check for storage_class_specifier with AC_TOOL_META */
        if (strcmp(type, "storage_class_specifier") == 0) {
            char text[64];
            extract_node_text(child, source, text, sizeof(text));
            if (strcmp(text, "AC_TOOL_META") == 0) {
                return true;
            }
        }

        /* Tree-sitter may parse AC_TOOL_META as a type_identifier
         * when it's used as an attribute-like marker before the return type */
        if (strcmp(type, "type_identifier") == 0) {
            char text[64];
            extract_node_text(child, source, text, sizeof(text));
            if (strcmp(text, "AC_TOOL_META") == 0) {
                return true;
            }
        }
    }

    return false;
}

/**
 * Find the comment node preceding a declaration
 */
static TSNode find_preceding_comment(TSNode decl, TSNode root, const char *source) {
    TSNode prev = ts_node_prev_sibling(decl);

    /* Look for a comment node as the previous sibling */
    while (!ts_node_is_null(prev)) {
        const char *type = ts_node_type(prev);
        if (strcmp(type, "comment") == 0) {
            return prev;
        }
        /* Stop if we hit another declaration or non-whitespace */
        if (strcmp(type, "declaration") == 0 ||
            strcmp(type, "function_definition") == 0 ||
            strcmp(type, "preproc_include") == 0 ||
            strcmp(type, "preproc_define") == 0) {
            break;
        }
        prev = ts_node_prev_sibling(prev);
    }

    /* Return null node if not found */
    return ts_node_parent(root); /* This will be null if no parent */
}

/**
 * Parse parameter type and extract components
 */
static void parse_param_type(const char *type_str, moc_param_t *param) {
    param->is_const = (strstr(type_str, "const") != NULL);
    param->is_pointer = (strchr(type_str, '*') != NULL);

    /* Determine base type category */
    if (strstr(type_str, "char") && param->is_pointer) {
        param->type = MOC_TYPE_STRING;
    } else if (strstr(type_str, "int") ||
               strstr(type_str, "short") ||
               strstr(type_str, "long") ||
               strstr(type_str, "size_t") ||
               strstr(type_str, "uint") ||
               strstr(type_str, "int8") ||
               strstr(type_str, "int16") ||
               strstr(type_str, "int32") ||
               strstr(type_str, "int64")) {
        param->type = MOC_TYPE_INT;
    } else if (strstr(type_str, "float") ||
               strstr(type_str, "double")) {
        param->type = MOC_TYPE_FLOAT;
    } else if (strstr(type_str, "bool") ||
               strstr(type_str, "_Bool")) {
        param->type = MOC_TYPE_BOOL;
    } else if (strstr(type_str, "void")) {
        param->type = MOC_TYPE_VOID;
    } else {
        param->type = MOC_TYPE_UNKNOWN;
    }

    strncpy(param->type_str, type_str, MOC_MAX_NAME_LEN - 1);
    param->type_str[MOC_MAX_NAME_LEN - 1] = '\0';
}

/**
 * Parse function parameters from parameter_list node
 */
static int parse_parameters(TSNode params_node, const char *source, moc_tool_t *tool) {
    tool->param_count = 0;

    uint32_t child_count = ts_node_named_child_count(params_node);

    for (uint32_t i = 0; i < child_count && tool->param_count < MOC_MAX_PARAMS; i++) {
        TSNode param_node = ts_node_named_child(params_node, i);
        const char *type = ts_node_type(param_node);

        if (strcmp(type, "parameter_declaration") == 0) {
            /* Check if this is a void parameter (e.g., "void" in "func(void)") */
            char param_text[64];
            extract_node_text(param_node, source, param_text, sizeof(param_text));
            
            /* Skip if it's just "void" (no parameter name) */
            if (strcmp(param_text, "void") == 0) {
                continue;
            }

            moc_param_t *param = &tool->params[tool->param_count];
            memset(param, 0, sizeof(moc_param_t));

            /* Find the declarator (parameter name) */
            TSNode declarator = ts_node_child_by_field_name(param_node, "declarator", 10);
            if (!ts_node_is_null(declarator)) {
                /* Handle pointer declarator */
                const char *decl_type = ts_node_type(declarator);
                if (strcmp(decl_type, "pointer_declarator") == 0) {
                    /* Get the inner declarator (identifier) */
                    TSNode inner = ts_node_named_child(declarator, 0);
                    if (!ts_node_is_null(inner)) {
                        extract_node_text(inner, source, param->name, MOC_MAX_NAME_LEN);
                    }
                } else {
                    extract_node_text(declarator, source, param->name, MOC_MAX_NAME_LEN);
                }
            }

            /* Find the type specifier */
            TSNode type_node = ts_node_child_by_field_name(param_node, "type", 4);
            if (!ts_node_is_null(type_node)) {
                /* Build full type string including qualifiers */
                char type_str[MOC_MAX_NAME_LEN] = {0};

                /* Check for type qualifiers before the type */
                for (uint32_t j = 0; j < ts_node_child_count(param_node); j++) {
                    TSNode child = ts_node_child(param_node, j);
                    const char *child_type = ts_node_type(child);

                    if (strcmp(child_type, "type_qualifier") == 0) {
                        char qualifier[32];
                        extract_node_text(child, source, qualifier, sizeof(qualifier));
                        if (type_str[0] != '\0') strcat(type_str, " ");
                        strcat(type_str, qualifier);
                    }
                }

                /* Add the main type */
                char main_type[64];
                extract_node_text(type_node, source, main_type, sizeof(main_type));
                if (type_str[0] != '\0') strcat(type_str, " ");
                strcat(type_str, main_type);

                /* Check for pointer in declarator */
                if (!ts_node_is_null(declarator) &&
                    strcmp(ts_node_type(declarator), "pointer_declarator") == 0) {
                    strcat(type_str, "*");
                }

                parse_param_type(type_str, param);
            }

            tool->param_count++;
        }
    }

    return 0;
}

/**
 * Parse return type from declaration node
 *
 * Due to tree-sitter parsing quirks with AC_TOOL_META, we may get
 * unusual AST structures. This function handles various cases.
 */
static void parse_return_type(TSNode decl, const char *source, moc_tool_t *tool) {
    char type_str[MOC_MAX_NAME_LEN] = {0};
    bool has_pointer = false;
    bool skip_tool_meta = false;

    /* Collect type qualifiers and type specifiers */
    for (uint32_t i = 0; i < ts_node_child_count(decl); i++) {
        TSNode child = ts_node_child(decl, i);
        const char *child_type = ts_node_type(child);

        /* Skip AC_TOOL_META if it appears as type_identifier */
        if (strcmp(child_type, "type_identifier") == 0) {
            char text[64];
            extract_node_text(child, source, text, sizeof(text));
            if (strcmp(text, "AC_TOOL_META") == 0) {
                skip_tool_meta = true;
                continue;
            }
            /* Otherwise it might be a custom type */
            if (type_str[0] != '\0') strcat(type_str, " ");
            strcat(type_str, text);
            continue;
        }

        if (strcmp(child_type, "type_qualifier") == 0 ||
            strcmp(child_type, "primitive_type") == 0 ||
            strcmp(child_type, "sized_type_specifier") == 0) {
            char part[64];
            extract_node_text(child, source, part, sizeof(part));
            if (type_str[0] != '\0') strcat(type_str, " ");
            strcat(type_str, part);
        }

        /* Handle ERROR nodes that may contain the actual type */
        if (strcmp(child_type, "ERROR") == 0) {
            /* Look for identifier inside ERROR node */
            for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
                TSNode err_child = ts_node_child(child, j);
                const char *err_type = ts_node_type(err_child);
                if (strcmp(err_type, "identifier") == 0 ||
                    strcmp(err_type, "primitive_type") == 0) {
                    char part[64];
                    extract_node_text(err_child, source, part, sizeof(part));
                    /* Skip AC_TOOL_META */
                    if (strcmp(part, "AC_TOOL_META") != 0) {
                        if (type_str[0] != '\0') strcat(type_str, " ");
                        strcat(type_str, part);
                    }
                }
            }
        }
    }

    /* Check if declarator is a pointer_declarator */
    TSNode declarator = ts_node_child_by_field_name(decl, "declarator", 10);
    if (!ts_node_is_null(declarator)) {
        const char *decl_type = ts_node_type(declarator);
        if (strcmp(decl_type, "pointer_declarator") == 0) {
            has_pointer = true;
        }
    }

    if (has_pointer) {
        strcat(type_str, "*");
    }

    strncpy(tool->return_type, type_str, MOC_MAX_NAME_LEN - 1);
    tool->return_type[MOC_MAX_NAME_LEN - 1] = '\0';
    tool->return_type_cat = moc_map_type(type_str);
}

/*============================================================================
 * Main Parsing Functions
 *============================================================================*/

/**
 * Initialize the Tree-sitter parser
 */
static int init_parser_state(parser_state_t *state) {
    memset(state, 0, sizeof(parser_state_t));

    state->parser = ts_parser_new();
    if (!state->parser) {
        fprintf(stderr, "Error: Failed to create Tree-sitter parser\n");
        return -1;
    }

    state->language = tree_sitter_c();
    if (!ts_parser_set_language(state->parser, state->language)) {
        fprintf(stderr, "Error: Failed to set C language\n");
        ts_parser_delete(state->parser);
        return -1;
    }

    return 0;
}

/**
 * Clean up parser state
 */
static void cleanup_parser_state(parser_state_t *state) {
    if (state->cursor) ts_query_cursor_delete(state->cursor);
    if (state->query) ts_query_delete(state->query);
    if (state->tree) ts_tree_delete(state->tree);
    if (state->parser) ts_parser_delete(state->parser);
}

/**
 * Main parsing function
 */
int moc_parse(moc_ctx_t *ctx) {
    parser_state_t state;

    if (init_parser_state(&state) != 0) {
        return -1;
    }

    /* Parse the source code */
    state.tree = ts_parser_parse_string(
        state.parser,
        NULL,
        ctx->source_code,
        (uint32_t)ctx->source_len
    );

    if (!state.tree) {
        fprintf(stderr, "Error: Failed to parse source code\n");
        cleanup_parser_state(&state);
        return -1;
    }

    TSNode root = ts_tree_root_node(state.tree);

    /* Create query for finding declarations */
    uint32_t error_offset;
    TSQueryError error_type;
    state.query = ts_query_new(
        state.language,
        TOOL_QUERY,
        (uint32_t)strlen(TOOL_QUERY),
        &error_offset,
        &error_type
    );

    if (!state.query) {
        fprintf(stderr, "Error: Failed to create query (error at offset %u, type %d)\n",
                error_offset, error_type);
        cleanup_parser_state(&state);
        return -1;
    }

    /* Create query cursor and execute query */
    state.cursor = ts_query_cursor_new();
    ts_query_cursor_exec(state.cursor, state.query, root);

    /* Get capture indices for our named captures */
    uint32_t decl_idx = UINT32_MAX, func_name_idx = UINT32_MAX;
    uint32_t params_idx = UINT32_MAX;
    uint32_t decl2_idx = UINT32_MAX, func_name2_idx = UINT32_MAX;
    uint32_t params2_idx = UINT32_MAX;

    uint32_t capture_count = ts_query_capture_count(state.query);
    for (uint32_t i = 0; i < capture_count; i++) {
        uint32_t len;
        const char *name = ts_query_capture_name_for_id(state.query, i, &len);
        if (len == 4 && strncmp(name, "decl", 4) == 0) decl_idx = i;
        else if (len == 9 && strncmp(name, "func_name", 9) == 0) func_name_idx = i;
        else if (len == 6 && strncmp(name, "params", 6) == 0) params_idx = i;
        else if (len == 5 && strncmp(name, "decl2", 5) == 0) decl2_idx = i;
        else if (len == 10 && strncmp(name, "func_name2", 10) == 0) func_name2_idx = i;
        else if (len == 7 && strncmp(name, "params2", 7) == 0) params2_idx = i;
    }

    /* Iterate through matches */
    TSQueryMatch match;
    while (ts_query_cursor_next_match(state.cursor, &match)) {
        TSNode decl_node = {0}, func_name_node = {0}, params_node = {0};

        /* Extract captured nodes from either pattern */
        for (uint16_t i = 0; i < match.capture_count; i++) {
            TSQueryCapture capture = match.captures[i];
            if (capture.index == decl_idx || capture.index == decl2_idx) {
                decl_node = capture.node;
            } else if (capture.index == func_name_idx || capture.index == func_name2_idx) {
                func_name_node = capture.node;
            } else if (capture.index == params_idx || capture.index == params2_idx) {
                params_node = capture.node;
            }
        }

        /* Check if this declaration has AC_TOOL_META marker */
        if (ts_node_is_null(decl_node) || !has_tool_meta_marker(decl_node, ctx->source_code)) {
            continue;
        }

        if (ctx->tool_count >= MOC_MAX_TOOLS) {
            fprintf(stderr, "Warning: Maximum tool limit reached (%d)\n", MOC_MAX_TOOLS);
            break;
        }

        moc_tool_t *tool = &ctx->tools[ctx->tool_count];
        memset(tool, 0, sizeof(moc_tool_t));

        /* Extract function name */
        if (!ts_node_is_null(func_name_node)) {
            extract_node_text(func_name_node, ctx->source_code, tool->name, MOC_MAX_NAME_LEN);
        }

        /* Get line number */
        TSPoint start = ts_node_start_point(decl_node);
        tool->line_number = start.row + 1;

        /* Parse return type */
        parse_return_type(decl_node, ctx->source_code, tool);

        /* Parse parameters */
        if (!ts_node_is_null(params_node)) {
            parse_parameters(params_node, ctx->source_code, tool);
        }

        /* Find and parse preceding comment */
        TSNode comment_node = find_preceding_comment(decl_node, root, ctx->source_code);
        if (!ts_node_is_null(comment_node) &&
            strcmp(ts_node_type(comment_node), "comment") == 0) {
            uint32_t start_byte = ts_node_start_byte(comment_node);
            uint32_t end_byte = ts_node_end_byte(comment_node);
            moc_parse_comment(ctx->source_code + start_byte,
                             end_byte - start_byte,
                             tool);
        }

        if (ctx->verbose) {
            printf("Found tool: %s (line %d)\n", tool->name, tool->line_number);
            moc_print_tool(tool);
        }

        ctx->tool_count++;
    }

    cleanup_parser_state(&state);

    if (ctx->verbose) {
        printf("Total tools found: %d\n", ctx->tool_count);
    }

    return 0;
}

/*============================================================================
 * Type Mapping Implementation
 *============================================================================*/

moc_type_t moc_map_type(const char *type_str) {
    if (!type_str) return MOC_TYPE_UNKNOWN;

    bool is_pointer = (strchr(type_str, '*') != NULL);

    if (strstr(type_str, "char") && is_pointer) {
        return MOC_TYPE_STRING;
    }

    if (strstr(type_str, "void")) {
        return MOC_TYPE_VOID;
    }

    if (strstr(type_str, "bool") || strstr(type_str, "_Bool")) {
        return MOC_TYPE_BOOL;
    }

    if (strstr(type_str, "float") || strstr(type_str, "double")) {
        return MOC_TYPE_FLOAT;
    }

    if (strstr(type_str, "int") ||
        strstr(type_str, "short") ||
        strstr(type_str, "long") ||
        strstr(type_str, "size_t") ||
        strstr(type_str, "uint") ||
        strstr(type_str, "int8") ||
        strstr(type_str, "int16") ||
        strstr(type_str, "int32") ||
        strstr(type_str, "int64")) {
        return MOC_TYPE_INT;
    }

    return MOC_TYPE_UNKNOWN;
}

const char *moc_type_to_json_schema(moc_type_t type) {
    switch (type) {
        case MOC_TYPE_INT:     return "integer";
        case MOC_TYPE_FLOAT:   return "number";
        case MOC_TYPE_BOOL:    return "boolean";
        case MOC_TYPE_STRING:  return "string";
        case MOC_TYPE_VOID:    return "null";
        default:               return "string";  /* Default to string for unknown types */
    }
}
