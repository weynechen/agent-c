/**
 * @file md_parser.h
 * @brief Markdown parser interface
 */

#ifndef MD_PARSER_H
#define MD_PARSER_H

#include "md_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse inline Markdown content (bold, italic, code, links)
 * @param text Input text
 * @return Linked list of inline tokens, or NULL on empty/error
 */
md_inline_token_t* md_parse_inline(const char* text);

/**
 * Parse full Markdown document
 * @param markdown Input Markdown string
 * @return Linked list of block tokens, or NULL on empty/error
 */
md_block_token_t* md_parse(const char* markdown);

/**
 * Free inline token list
 * @param token Head of inline token list
 */
void md_free_inline_tokens(md_inline_token_t* token);

/**
 * Free list item tree
 * @param item Head of list item tree
 */
void md_free_list_items(md_list_item_t* item);

/**
 * Free block token list
 * @param token Head of block token list
 */
void md_free_tokens(md_block_token_t* token);

#ifdef __cplusplus
}
#endif

#endif /* MD_PARSER_H */
