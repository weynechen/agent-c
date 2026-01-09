/**
 * @file md_types.h
 * @brief Markdown parser and renderer type definitions
 */

#ifndef MD_TYPES_H
#define MD_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Block-level element types ========== */
typedef enum {
    MD_BLOCK_HEADING,       /* Heading h1-h6 */
    MD_BLOCK_PARAGRAPH,     /* Paragraph */
    MD_BLOCK_QUOTE,         /* Block quote */
    MD_BLOCK_LIST,          /* List (ordered/unordered, supports nesting) */
    MD_BLOCK_CODE,          /* Fenced code block */
    MD_BLOCK_HR,            /* Horizontal rule */
    MD_BLOCK_TABLE          /* Table */
} md_block_type_t;

/* ========== Inline element types ========== */
typedef enum {
    MD_INLINE_PLAIN,        /* Plain text */
    MD_INLINE_BOLD,         /* Bold text */
    MD_INLINE_ITALIC,       /* Italic text */
    MD_INLINE_BOLD_ITALIC,  /* Bold + Italic */
    MD_INLINE_CODE,         /* Inline code */
    MD_INLINE_LINK          /* Hyperlink */
} md_inline_type_t;

/* ========== List types ========== */
typedef enum {
    MD_LIST_UNORDERED,      /* Unordered list (-, *, +) */
    MD_LIST_ORDERED         /* Ordered list (1., 2., ...) */
} md_list_type_t;

/* ========== Table column alignment ========== */
typedef enum {
    MD_ALIGN_LEFT,          /* :--- or --- */
    MD_ALIGN_CENTER,        /* :---: */
    MD_ALIGN_RIGHT          /* ---: */
} md_align_t;

/* ========== Inline token (linked list) ========== */
typedef struct md_inline_token {
    md_inline_type_t type;
    char* text;                         /* Text content */
    char* url;                          /* URL for links only */
    struct md_inline_token* next;       /* Next inline token */
} md_inline_token_t;

/* ========== List item (supports nesting) ========== */
typedef struct md_list_item {
    md_inline_token_t* content;         /* Item content */
    struct md_list_item* children;      /* Nested list items */
    struct md_list_item* next;          /* Next sibling item */
    int indent_level;                   /* Indentation level (0-based) */
    md_list_type_t child_type;          /* Type of nested list */
} md_list_item_t;

/* ========== Table data ========== */
typedef struct {
    md_inline_token_t** headers;        /* Array of header cells */
    md_inline_token_t*** rows;          /* 2D array: rows[row][col] */
    md_align_t* aligns;                 /* Alignment for each column */
    size_t col_count;                   /* Number of columns */
    size_t row_count;                   /* Number of data rows */
} md_table_t;

/* ========== Block token (linked list) ========== */
typedef struct md_block_token {
    md_block_type_t type;
    union {
        struct {
            int level;                  /* 1-6 */
            md_inline_token_t* content;
        } heading;
        
        struct {
            md_inline_token_t* content;
        } paragraph;
        
        struct {
            md_inline_token_t* content;
        } quote;
        
        struct {
            md_list_type_t type;
            md_list_item_t* items;
        } list;
        
        struct {
            char* lang;                 /* Language identifier */
            char* code;                 /* Code content */
        } code;
        
        md_table_t table;
    } data;
    
    struct md_block_token* next;        /* Next block token */
} md_block_token_t;

/* ========== Stream parser state ========== */
typedef enum {
    MD_STATE_NORMAL,        /* Normal parsing */
    MD_STATE_CODE_BLOCK,    /* Inside fenced code block */
    MD_STATE_TABLE,         /* Parsing table rows */
    MD_STATE_LIST           /* Parsing list items */
} md_stream_state_t;

#ifdef __cplusplus
}
#endif

#endif /* MD_TYPES_H */
