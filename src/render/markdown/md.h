/**
 * @file md.h
 * @brief Terminal Markdown rendering library - main header
 * 
 * This library provides parsing and rendering of Markdown content to the terminal
 * with ANSI styling. It supports:
 * 
 * Block elements:
 *   - Headings (h1-h6) with colored styling
 *   - Paragraphs
 *   - Block quotes
 *   - Ordered and unordered lists (with nesting)
 *   - Fenced code blocks with language labels
 *   - Horizontal rules
 *   - Tables with alignment
 * 
 * Inline elements:
 *   - Bold (**text** or __text__)
 *   - Italic (*text* or _text_)
 *   - Bold+Italic (***text***)
 *   - Inline code (`code`)
 *   - Links [text](url)
 * 
 * Features:
 *   - UTF-8 support with proper CJK character width handling
 *   - Streaming API for incremental rendering
 *   - OSC 8 hyperlink support detection
 * 
 * Example usage:
 * 
 *   // Simple one-shot rendering
 *   md_render("# Hello World\n\nThis is **bold** text.");
 * 
 *   // Streaming rendering
 *   md_stream_t* stream = md_stream_new();
 *   md_stream_feed_str(stream, "# Title\n");
 *   md_stream_feed_str(stream, "Some content...\n");
 *   md_stream_finish(stream);
 *   md_stream_free(stream);
 */

#ifndef MD_H
#define MD_H

/* Include all sub-headers */
#include "md_types.h"
#include "md_style.h"
#include "md_utils.h"
#include "md_parser.h"
#include "md_renderer.h"
#include "md_stream.h"

#endif /* MD_H */
