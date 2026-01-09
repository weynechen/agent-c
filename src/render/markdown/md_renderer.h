/**
 * @file md_renderer.h
 * @brief Markdown renderer interface
 */

#ifndef MD_RENDERER_H
#define MD_RENDERER_H

#include "md_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Output callback function type
 * @param text Text to output
 * @param len Length of text
 * @param userdata User-provided data
 */
typedef void (*md_output_fn)(const char* text, size_t len, void* userdata);

/**
 * Renderer context
 */
typedef struct md_renderer {
    md_output_fn output;    /* Output callback */
    void* userdata;         /* User data for callback */
    int term_width;         /* Terminal width */
    int supports_hyperlink; /* OSC 8 hyperlink support */
} md_renderer_t;

/**
 * Initialize renderer with default settings (stdout output)
 * @param renderer Renderer context to initialize
 */
void md_renderer_init(md_renderer_t* renderer);

/**
 * Set output callback
 * @param renderer Renderer context
 * @param output Output callback function
 * @param userdata User data passed to callback
 */
void md_renderer_set_output(md_renderer_t* renderer, md_output_fn output, void* userdata);

/**
 * Render inline tokens
 * @param renderer Renderer context
 * @param tokens Inline token list
 */
void md_render_inline(md_renderer_t* renderer, const md_inline_token_t* tokens);

/**
 * Render block tokens
 * @param renderer Renderer context
 * @param tokens Block token list
 */
void md_render_blocks(md_renderer_t* renderer, const md_block_token_t* tokens);

/**
 * Render a single block token
 * @param renderer Renderer context
 * @param token Block token
 */
void md_render_block(md_renderer_t* renderer, const md_block_token_t* token);

/**
 * Simple render function - render Markdown to stdout
 * @param markdown Markdown string
 */
void md_render(const char* markdown);

/**
 * Render Markdown tokens to stdout
 * @param tokens Block token list
 */
void md_render_tokens(const md_block_token_t* tokens);

#ifdef __cplusplus
}
#endif

#endif /* MD_RENDERER_H */
