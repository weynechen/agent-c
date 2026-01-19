/**
 * @file md_renderer.c
 * @brief Markdown renderer implementation
 */

#include "md_renderer.h"
#include "md_parser.h"
#include "md_style.h"
#include "md_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== Output helpers ========== */

static void output(md_renderer_t* r, const char* text) {
    if (!text) return;
    if (r->output) {
        r->output(text, strlen(text), r->userdata);
    } else {
        fputs(text, stdout);
    }
}

static void output_n(md_renderer_t* r, const char* text, int n) {
    for (int i = 0; i < n; i++) {
        output(r, text);
    }
}

static void output_char(md_renderer_t* r, char c) {
    char buf[2] = {c, '\0'};
    output(r, buf);
}

static void output_int(md_renderer_t* r, int n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", n);
    output(r, buf);
}

/* ========== Initialization ========== */

void md_renderer_init(md_renderer_t* renderer) {
    if (!renderer) return;
    renderer->output = NULL;
    renderer->userdata = NULL;
    renderer->term_width = md_get_terminal_width();
    renderer->supports_hyperlink = md_supports_hyperlink();
}

void md_renderer_set_output(md_renderer_t* renderer, md_output_fn output_fn, void* userdata) {
    if (!renderer) return;
    renderer->output = output_fn;
    renderer->userdata = userdata;
}

/* ========== Inline rendering ========== */

void md_render_inline(md_renderer_t* r, const md_inline_token_t* tokens) {
    for (const md_inline_token_t* tok = tokens; tok; tok = tok->next) {
        switch (tok->type) {
            case MD_INLINE_PLAIN:
                output(r, tok->text);
                break;
                
            case MD_INLINE_BOLD:
                output(r, MD_STYLE_BOLD);
                output(r, tok->text);
                output(r, MD_STYLE_RESET);
                break;
                
            case MD_INLINE_ITALIC:
                output(r, MD_STYLE_ITALIC);
                output(r, tok->text);
                output(r, MD_STYLE_RESET);
                break;
                
            case MD_INLINE_BOLD_ITALIC:
                output(r, MD_STYLE_BOLD);
                output(r, MD_STYLE_ITALIC);
                output(r, tok->text);
                output(r, MD_STYLE_RESET);
                break;
                
            case MD_INLINE_CODE:
                output(r, MD_BG_DARK_GRAY);
                output(r, MD_COLOR_LIGHT_GRAY);
                output(r, tok->text);
                output(r, MD_STYLE_RESET);
                break;
                
            case MD_INLINE_LINK:
                if (r->supports_hyperlink) {
                    output(r, MD_COLOR_BRIGHT_BLUE);
                    output(r, MD_HYPERLINK_START);
                    output(r, tok->url);
                    output(r, MD_HYPERLINK_SEP);
                    output(r, MD_STYLE_UNDERLINE);
                    output(r, tok->text);
                    output(r, MD_STYLE_RESET);
                    output(r, MD_HYPERLINK_END);
                } else {
                    output(r, tok->text);
                    output(r, " (");
                    output(r, MD_STYLE_UNDERLINE);
                    output(r, tok->url);
                    output(r, MD_STYLE_RESET);
                    output(r, ")");
                }
                break;
        }
    }
}

/* ========== Get inline text for width calculation ========== */

static int get_inline_width(const md_inline_token_t* tokens) {
    int width = 0;
    for (const md_inline_token_t* tok = tokens; tok; tok = tok->next) {
        if (tok->text) {
            width += md_utf8_display_width(tok->text);
        }
        if (tok->type == MD_INLINE_LINK && tok->url) {
            /* For non-hyperlink terminals, we show " (url)" */
            width += 3 + md_utf8_display_width(tok->url);
        }
    }
    return width;
}

/* ========== Block rendering ========== */

static void render_heading(md_renderer_t* r, const md_block_token_t* tok) {
    const char* color;
    switch (tok->data.heading.level) {
        case 1: color = MD_HEADING1_COLOR; break;
        case 2: color = MD_HEADING2_COLOR; break;
        case 3: color = MD_HEADING3_COLOR; break;
        case 4: color = MD_HEADING4_COLOR; break;
        case 5: color = MD_HEADING5_COLOR; break;
        case 6: color = MD_HEADING6_COLOR; break;
        default: color = MD_STYLE_BOLD; break;
    }
    
    output(r, color);
    output(r, MD_STYLE_BOLD);
    md_render_inline(r, tok->data.heading.content);
    output(r, MD_STYLE_RESET);
    output(r, "\n\n");
}

static void render_paragraph(md_renderer_t* r, const md_block_token_t* tok) {
    md_render_inline(r, tok->data.paragraph.content);
    output(r, "\n\n");
}

static void render_quote(md_renderer_t* r, const md_block_token_t* tok) {
    output(r, MD_BG_DARK_GRAY);
    output(r, MD_COLOR_LIGHT_GRAY);
    output(r, "> ");
    output(r, MD_STYLE_ITALIC);
    md_render_inline(r, tok->data.quote.content);
    output(r, MD_STYLE_RESET);
    output(r, "\n\n");
}

static void render_hr(md_renderer_t* r) {
    output(r, MD_COLOR_DARK_GRAY);
    output_n(r, "_", r->term_width);
    output(r, MD_STYLE_RESET);
    output(r, "\n\n");
}

static void render_list_item(md_renderer_t* r, const md_list_item_t* item, 
                             md_list_type_t type, int number, int indent) {
    /* Indentation */
    output_n(r, "  ", indent);
    
    /* Bullet or number */
    if (type == MD_LIST_ORDERED) {
        output_int(r, number);
        output(r, ". ");
    } else {
        /* Different bullets for different indent levels */
        const char* bullet;
        switch (indent % 3) {
            case 0: bullet = MD_BULLET_LEVEL0; break;
            case 1: bullet = MD_BULLET_LEVEL1; break;
            default: bullet = MD_BULLET_LEVEL2; break;
        }
        output(r, bullet);
        output(r, " ");
    }
    
    md_render_inline(r, item->content);
    output(r, "\n");
}

static void render_list(md_renderer_t* r, const md_block_token_t* tok) {
    int number = 1;
    for (const md_list_item_t* item = tok->data.list.items; item; item = item->next) {
        render_list_item(r, item, tok->data.list.type, number, item->indent_level);
        number++;
    }
    output(r, "\n");
}

static void render_code_block(md_renderer_t* r, const md_block_token_t* tok) {
    const char* lang = tok->data.code.lang;
    const char* code = tok->data.code.code;
    
    if (!lang || !*lang) lang = "code";
    
    /* Calculate max line width */
    int max_width = 0;
    const char* p = code;
    int current_width = 0;
    while (*p) {
        if (*p == '\n') {
            if (current_width > max_width) max_width = current_width;
            current_width = 0;
        } else {
            int bytes;
            uint32_t cp = md_utf8_decode(p, &bytes);
            current_width += md_char_width(cp);
            p += bytes - 1;
        }
        p++;
    }
    if (current_width > max_width) max_width = current_width;
    
    /* Draw top border */
    int lang_len = md_utf8_display_width(lang);
    int content_width = max_width > lang_len ? max_width : lang_len;
    /* Box inner = 1 space + content_width + 1 space */
    int box_inner = content_width + 2;
    
    output(r, MD_STYLE_BOLD);
    output(r, MD_COLOR_BRIGHT_YELLOW);
    output(r, MD_BOX_TOP_LEFT);
    output(r, MD_BOX_HORIZONTAL);
    output(r, " ");
    output(r, lang);
    output(r, " ");
    /* Fill remaining width: box_inner - 1(first ─) - 1(space) - lang_len - 1(space) = box_inner - lang_len - 3 */
    int remaining = box_inner - lang_len - 3;
    if (remaining > 0) {
        output_n(r, MD_BOX_HORIZONTAL, remaining);
    }
    output(r, MD_BOX_TOP_RIGHT);
    output(r, MD_STYLE_RESET);
    output(r, "\n");
    
    /* Draw code lines */
    p = code;
    while (*p) {
        output(r, MD_COLOR_BRIGHT_YELLOW);
        output(r, MD_BOX_VERTICAL);
        output(r, " ");
        output(r, MD_STYLE_RESET);
        
        /* Output line content */
        const char* line_start = p;
        while (*p && *p != '\n') p++;
        
        int line_width = 0;
        if (p > line_start) {
            char* line = md_strndup(line_start, p - line_start);
            if (line) {
                output(r, line);
                line_width = md_utf8_display_width(line);
                free(line);
            }
        }
        
        /* Pad to content_width and add right border */
        int padding = content_width - line_width;
        if (padding > 0) {
            output_n(r, " ", padding);
        }
        output(r, " ");
        output(r, MD_COLOR_BRIGHT_YELLOW);
        output(r, MD_BOX_VERTICAL);
        output(r, MD_STYLE_RESET);
        output(r, "\n");
        if (*p == '\n') p++;
    }
    
    /* Draw bottom border */
    output(r, MD_COLOR_BRIGHT_YELLOW);
    output(r, MD_BOX_BOTTOM_LEFT);
    output_n(r, MD_BOX_HORIZONTAL, box_inner);
    output(r, MD_BOX_BOTTOM_RIGHT);
    output(r, MD_STYLE_RESET);
    output(r, "\n\n");
}

static void render_table(md_renderer_t* r, const md_block_token_t* tok) {
    const md_table_t* table = &tok->data.table;
    size_t col_count = table->col_count;
    
    if (col_count == 0) return;
    
    /* Calculate column widths */
    int* col_widths = (int*)calloc(col_count, sizeof(int));
    if (!col_widths) return;
    
    /* Headers */
    for (size_t i = 0; i < col_count; i++) {
        if (table->headers && table->headers[i]) {
            int w = get_inline_width(table->headers[i]);
            if (w > col_widths[i]) col_widths[i] = w;
        }
    }
    
    /* Rows */
    for (size_t row = 0; row < table->row_count; row++) {
        if (table->rows && table->rows[row]) {
            for (size_t col = 0; col < col_count; col++) {
                if (table->rows[row][col]) {
                    int w = get_inline_width(table->rows[row][col]);
                    if (w > col_widths[col]) col_widths[col] = w;
                }
            }
        }
    }
    
    /* Helper: print horizontal divider */
    #define PRINT_DIVIDER(left, mid, right) do { \
        output(r, MD_COLOR_BRIGHT_BLACK); \
        output(r, left); \
        for (size_t i = 0; i < col_count; i++) { \
            output_n(r, MD_BOX_HORIZONTAL, col_widths[i] + 2); \
            output(r, i == col_count - 1 ? right : mid); \
        } \
        output(r, MD_STYLE_RESET); \
        output(r, "\n"); \
    } while(0)
    
    /* Helper: print row */
    #define PRINT_ROW(cells, is_header) do { \
        output(r, MD_COLOR_BRIGHT_BLACK); \
        output(r, MD_BOX_VERTICAL); \
        output(r, MD_STYLE_RESET); \
        for (size_t i = 0; i < col_count; i++) { \
            output(r, " "); \
            if (is_header) output(r, MD_COLOR_BRIGHT_BLUE); \
            int content_width = 0; \
            if (cells && cells[i]) { \
                content_width = get_inline_width(cells[i]); \
                /* Apply alignment */ \
                md_align_t align = table->aligns ? table->aligns[i] : MD_ALIGN_LEFT; \
                int padding = col_widths[i] - content_width; \
                int left_pad = 0, right_pad = 0; \
                if (align == MD_ALIGN_CENTER) { \
                    left_pad = padding / 2; \
                    right_pad = padding - left_pad; \
                } else if (align == MD_ALIGN_RIGHT) { \
                    left_pad = padding; \
                } else { \
                    right_pad = padding; \
                } \
                output_n(r, " ", left_pad); \
                md_render_inline(r, cells[i]); \
                output_n(r, " ", right_pad); \
            } else { \
                output_n(r, " ", col_widths[i]); \
            } \
            output(r, MD_STYLE_RESET); \
            output(r, " "); \
            output(r, MD_COLOR_BRIGHT_BLACK); \
            output(r, MD_BOX_VERTICAL); \
            output(r, MD_STYLE_RESET); \
        } \
        output(r, "\n"); \
    } while(0)
    
    /* Top border */
    PRINT_DIVIDER(MD_BOX_TOP_LEFT, MD_BOX_T_DOWN, MD_BOX_TOP_RIGHT);
    
    /* Header row */
    PRINT_ROW(table->headers, 1);
    
    /* Separator */
    PRINT_DIVIDER(MD_BOX_T_RIGHT, MD_BOX_CROSS, MD_BOX_T_LEFT);
    
    /* Data rows */
    for (size_t row = 0; row < table->row_count; row++) {
        md_inline_token_t** row_cells = (table->rows && table->rows[row]) ? table->rows[row] : NULL;
        PRINT_ROW(row_cells, 0);
    }
    
    /* Bottom border */
    PRINT_DIVIDER(MD_BOX_BOTTOM_LEFT, MD_BOX_T_UP, MD_BOX_BOTTOM_RIGHT);
    
    output(r, "\n");
    
    #undef PRINT_DIVIDER
    #undef PRINT_ROW
    
    free(col_widths);
}

void md_render_block(md_renderer_t* r, const md_block_token_t* tok) {
    if (!r || !tok) return;
    
    switch (tok->type) {
        case MD_BLOCK_HEADING:
            render_heading(r, tok);
            break;
        case MD_BLOCK_PARAGRAPH:
            render_paragraph(r, tok);
            break;
        case MD_BLOCK_QUOTE:
            render_quote(r, tok);
            break;
        case MD_BLOCK_LIST:
            render_list(r, tok);
            break;
        case MD_BLOCK_CODE:
            render_code_block(r, tok);
            break;
        case MD_BLOCK_HR:
            render_hr(r);
            break;
        case MD_BLOCK_TABLE:
            render_table(r, tok);
            break;
    }
}

void md_render_blocks(md_renderer_t* r, const md_block_token_t* tokens) {
    for (const md_block_token_t* tok = tokens; tok; tok = tok->next) {
        md_render_block(r, tok);
    }
}

/* ========== Simple API ========== */

void md_render(const char* markdown) {
    if (!markdown) return;
    
    md_block_token_t* tokens = md_parse(markdown);
    if (tokens) {
        md_render_tokens(tokens);
        md_free_tokens(tokens);
    }
}

void md_render_tokens(const md_block_token_t* tokens) {
    md_renderer_t renderer;
    md_renderer_init(&renderer);
    md_render_blocks(&renderer, tokens);
}
