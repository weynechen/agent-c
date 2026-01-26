/**
 * Minimal tree-sitter C language parser example
 *
 * This example demonstrates how to use tree-sitter to parse C source code
 * and traverse the resulting syntax tree.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

/**
 * Print the syntax tree recursively with indentation
 */
static void print_tree(TSNode node, const char *source_code, int indent) {
    // Get node type and position
    const char *type = ts_node_type(node);
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);

    // Print indentation
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }

    // Check if this is a named node (structural) vs anonymous (punctuation, etc.)
    if (ts_node_is_named(node)) {
        printf("(%s [%u:%u - %u:%u]", type, start.row, start.column, end.row, end.column);
    } else {
        printf("\"%s\" [%u:%u - %u:%u]", type, start.row, start.column, end.row, end.column);
    }

    // Recursively print children
    uint32_t child_count = ts_node_child_count(node);
    if (child_count > 0) {
        printf("\n");
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            print_tree(child, source_code, indent + 1);
        }
        // Print closing indentation
        for (int i = 0; i < indent; i++) {
            printf("  ");
        }
        printf(")\n");
    } else {
        printf(")\n");
    }
}

/**
 * Extract and print the text content of a node
 */
static void print_node_text(TSNode node, const char *source_code) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    uint32_t len = end - start;

    char *text = malloc(len + 1);
    if (text) {
        memcpy(text, source_code + start, len);
        text[len] = '\0';
        printf("  Text: %s\n", text);
        free(text);
    }
}

int main(int argc, char *argv[]) {
    // Example C source code to parse
    const char *source_code =
        "#include <stdio.h>\n"
        "\n"
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    int result = add(1, 2);\n"
        "    printf(\"Result: %d\\n\", result);\n"
        "    return 0;\n"
        "}\n";

    printf("=== Source Code ===\n%s\n", source_code);

    // Create a parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "Error: Failed to create parser\n");
        return 1;
    }

    // Set the parser's language to C
    const TSLanguage *language = tree_sitter_c();
    if (!ts_parser_set_language(parser, language)) {
        fprintf(stderr, "Error: Failed to set language\n");
        ts_parser_delete(parser);
        return 1;
    }

    // Parse the source code
    TSTree *tree = ts_parser_parse_string(
        parser,
        NULL,                     // No previous tree
        source_code,
        strlen(source_code)
    );

    if (!tree) {
        fprintf(stderr, "Error: Failed to parse source code\n");
        ts_parser_delete(parser);
        return 1;
    }

    // Get the root node of the syntax tree
    TSNode root_node = ts_tree_root_node(tree);

    // Print basic info
    printf("=== Syntax Tree Info ===\n");
    printf("Root node type: %s\n", ts_node_type(root_node));
    printf("Child count: %u\n", ts_node_child_count(root_node));

    // Print the S-expression representation
    char *sexp = ts_node_string(root_node);
    printf("\n=== S-Expression ===\n%s\n", sexp);
    free(sexp);

    // Print detailed tree structure
    printf("\n=== Detailed Tree Structure ===\n");
    print_tree(root_node, source_code, 0);

    // Example: Find all function definitions
    printf("\n=== Function Definitions ===\n");
    uint32_t child_count = ts_node_child_count(root_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(root_node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "function_definition") == 0) {
            printf("Found function definition:\n");
            print_node_text(child, source_code);

            // Find the function declarator to get the function name
            for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
                TSNode grandchild = ts_node_child(child, j);
                if (strcmp(ts_node_type(grandchild), "function_declarator") == 0) {
                    // The first named child of function_declarator is usually the identifier
                    TSNode identifier = ts_node_named_child(grandchild, 0);
                    if (!ts_node_is_null(identifier)) {
                        printf("  Function name: ");
                        uint32_t start = ts_node_start_byte(identifier);
                        uint32_t end = ts_node_end_byte(identifier);
                        printf("%.*s\n", (int)(end - start), source_code + start);
                    }
                }
            }
            printf("\n");
        }
    }

    // Clean up
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    printf("=== Done ===\n");
    return 0;
}
