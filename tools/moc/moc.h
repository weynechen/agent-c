/**
 * @file moc.h
 * @brief Meta-Object Compiler for ArC Tool Generation
 *
 * MOC parses C header files with AC_TOOL_META markers and Doxygen-style
 * comments, generating wrapper functions and tool registration code.
 */

#ifndef MOC_H
#define MOC_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define MOC_MAX_PARAMS      16    /* Maximum number of parameters per function */
#define MOC_MAX_NAME_LEN    128   /* Maximum length of identifiers */
#define MOC_MAX_DESC_LEN    512   /* Maximum length of descriptions */
#define MOC_MAX_TOOLS       64    /* Maximum number of tools to process */

/*============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief C type categories for JSON Schema mapping
 */
typedef enum {
    MOC_TYPE_UNKNOWN = 0,
    MOC_TYPE_INT,           /* int, short, long, etc. */
    MOC_TYPE_FLOAT,         /* float, double */
    MOC_TYPE_BOOL,          /* bool, _Bool */
    MOC_TYPE_STRING,        /* char*, const char* */
    MOC_TYPE_VOID,          /* void (for return types) */
} moc_type_t;

/**
 * @brief Parameter information extracted from function declaration
 */
typedef struct {
    char name[MOC_MAX_NAME_LEN];         /* Parameter name */
    char type_str[MOC_MAX_NAME_LEN];     /* Original type string (e.g., "const char*") */
    char description[MOC_MAX_DESC_LEN];  /* Description from @param tag */
    moc_type_t type;                     /* Mapped type category */
    bool is_const;                       /* Whether the type is const-qualified */
    bool is_pointer;                     /* Whether the type is a pointer */
} moc_param_t;

/**
 * @brief Tool function metadata extracted from header file
 */
typedef struct {
    char name[MOC_MAX_NAME_LEN];         /* Function name */
    char description[MOC_MAX_DESC_LEN];  /* Description from @description tag */
    char return_type[MOC_MAX_NAME_LEN];  /* Return type string */
    moc_type_t return_type_cat;          /* Return type category */
    moc_param_t params[MOC_MAX_PARAMS];  /* Parameter list */
    int param_count;                     /* Number of parameters */
    int line_number;                     /* Line number in source file */
} moc_tool_t;

/**
 * @brief MOC context for parsing and code generation
 */
typedef struct {
    const char *source_code;             /* Source code buffer */
    size_t source_len;                   /* Source code length */
    const char *input_file;              /* Input file path */
    const char *output_base;             /* Output file base name (without extension) */
    moc_tool_t tools[MOC_MAX_TOOLS];     /* Extracted tool functions */
    int tool_count;                      /* Number of tools found */
    bool verbose;                        /* Verbose output mode */
} moc_ctx_t;

/*============================================================================
 * Initialization and Cleanup
 *============================================================================*/

/**
 * @brief Initialize MOC context
 *
 * @param ctx           Context to initialize
 * @param input_file    Path to input header file
 * @param output_base   Base name for output files (NULL for stdout)
 * @return 0 on success, -1 on error
 */
int moc_init(moc_ctx_t *ctx, const char *input_file, const char *output_base);

/**
 * @brief Free resources associated with MOC context
 *
 * @param ctx  Context to cleanup
 */
void moc_cleanup(moc_ctx_t *ctx);

/*============================================================================
 * Parsing Functions
 *============================================================================*/

/**
 * @brief Parse the input header file and extract tool metadata
 *
 * Uses Tree-sitter to parse the C header file and extract functions
 * marked with AC_TOOL_META macro along with their Doxygen comments.
 *
 * @param ctx  MOC context with loaded source code
 * @return 0 on success, -1 on error
 */
int moc_parse(moc_ctx_t *ctx);

/**
 * @brief Parse Doxygen-style comment block
 *
 * Extracts @description and @param tags from a comment block.
 *
 * @param comment_text  Raw comment text (including delimiters)
 * @param comment_len   Length of comment text
 * @param tool          Tool structure to populate with parsed info
 * @return 0 on success, -1 on error
 */
int moc_parse_comment(const char *comment_text, size_t comment_len, moc_tool_t *tool);

/*============================================================================
 * Type Mapping
 *============================================================================*/

/**
 * @brief Map a C type string to a type category
 *
 * @param type_str  C type string (e.g., "const char*", "int")
 * @return Type category
 */
moc_type_t moc_map_type(const char *type_str);

/**
 * @brief Get JSON Schema type string for a type category
 *
 * @param type  Type category
 * @return JSON Schema type string ("string", "integer", "number", "boolean")
 */
const char *moc_type_to_json_schema(moc_type_t type);

/*============================================================================
 * Code Generation
 *============================================================================*/

/**
 * @brief Generate all output files
 *
 * Generates tools_gen.h and tools_gen.c files.
 *
 * @param ctx  MOC context with parsed tool metadata
 * @return 0 on success, -1 on error
 */
int moc_generate(moc_ctx_t *ctx);

/**
 * @brief Generate header file content
 *
 * @param ctx  MOC context
 * @param out  Output file handle (NULL for stdout)
 * @return 0 on success, -1 on error
 */
int moc_generate_header(moc_ctx_t *ctx, FILE *out);

/**
 * @brief Generate source file content
 *
 * @param ctx  MOC context
 * @param out  Output file handle (NULL for stdout)
 * @return 0 on success, -1 on error
 */
int moc_generate_source(moc_ctx_t *ctx, FILE *out);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Extract text from source code given byte offsets
 *
 * @param source  Source code buffer
 * @param start   Start byte offset
 * @param end     End byte offset
 * @param dest    Destination buffer
 * @param dest_sz Destination buffer size
 */
void moc_extract_text(const char *source, size_t start, size_t end,
                      char *dest, size_t dest_sz);

/**
 * @brief Print tool information (for debugging)
 *
 * @param tool  Tool to print
 */
void moc_print_tool(const moc_tool_t *tool);

#ifdef __cplusplus
}
#endif

#endif /* MOC_H */
