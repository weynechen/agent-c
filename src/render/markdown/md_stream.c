/**
 * @file md_stream.c
 * @brief Streaming Markdown parser and renderer implementation
 */

#include "md_stream.h"
#include "md_parser.h"
#include "md_renderer.h"
#include "md_style.h"
#include "md_utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========== Stream state ========== */

struct md_stream {
    /* Renderer */
    md_renderer_t renderer;
    
    /* Line buffer for incomplete lines */
    char* line_buffer;
    size_t line_buf_size;
    size_t line_buf_len;
    
    /* Current parsing state */
    md_stream_state_t state;
    
    /* Code block state */
    char* code_lang;
    char* code_buffer;
    size_t code_buf_size;
    size_t code_buf_len;
    
    /* Table state */
    md_block_token_t* pending_table;
    int table_header_seen;
    
    /* List state */
    int in_list;
    md_list_type_t list_type;
    int list_item_number;
};

/* ========== Creation / Destruction ========== */

md_stream_t* md_stream_new(void) {
    md_stream_t* stream = (md_stream_t*)calloc(1, sizeof(md_stream_t));
    if (!stream) return NULL;
    
    md_renderer_init(&stream->renderer);
    stream->state = MD_STATE_NORMAL;
    
    return stream;
}

void md_stream_set_output(md_stream_t* stream, md_output_fn output, void* userdata) {
    if (!stream) return;
    md_renderer_set_output(&stream->renderer, output, userdata);
}

void md_stream_reset(md_stream_t* stream) {
    if (!stream) return;
    
    /* Clear line buffer */
    stream->line_buf_len = 0;
    if (stream->line_buffer) stream->line_buffer[0] = '\0';
    
    /* Reset state */
    stream->state = MD_STATE_NORMAL;
    
    /* Clear code buffer */
    free(stream->code_lang);
    stream->code_lang = NULL;
    stream->code_buf_len = 0;
    if (stream->code_buffer) stream->code_buffer[0] = '\0';
    
    /* Clear table state */
    if (stream->pending_table) {
        md_free_tokens(stream->pending_table);
        stream->pending_table = NULL;
    }
    stream->table_header_seen = 0;
    
    /* Reset list state */
    stream->in_list = 0;
    stream->list_item_number = 0;
}

void md_stream_free(md_stream_t* stream) {
    if (!stream) return;
    
    free(stream->line_buffer);
    free(stream->code_lang);
    free(stream->code_buffer);
    
    if (stream->pending_table) {
        md_free_tokens(stream->pending_table);
    }
    
    free(stream);
}

/* ========== Line processing ========== */

/* Forward declaration */
static void process_line(md_stream_t* stream, const char* line);

/* Helper to output directly */
static void output(md_stream_t* stream, const char* text) {
    if (!text) return;
    if (stream->renderer.output) {
        stream->renderer.output(text, strlen(text), stream->renderer.userdata);
    } else {
        fputs(text, stdout);
        fflush(stdout);
    }
}

static void output_n(md_stream_t* stream, const char* text, int n) {
    for (int i = 0; i < n; i++) {
        output(stream, text);
    }
}

/* Render inline tokens and free them */
static void render_and_free_inline(md_stream_t* stream, md_inline_token_t* tokens) {
    md_render_inline(&stream->renderer, tokens);
    md_free_inline_tokens(tokens);
}

/* Process a complete line */
static void process_line(md_stream_t* stream, const char* line) {
    if (!stream || !line) return;
    
    size_t len = strlen(line);
    
    /* ---- Code block handling ---- */
    if (strncmp(line, "```", 3) == 0) {
        if (stream->state != MD_STATE_CODE_BLOCK) {
            /* Start code block */
            stream->state = MD_STATE_CODE_BLOCK;
            stream->in_list = 0;
            
            /* Extract language */
            free(stream->code_lang);
            stream->code_lang = md_strdup(line + 3);
            if (stream->code_lang) md_rtrim(stream->code_lang);
            
            /* Reset code buffer */
            stream->code_buf_len = 0;
            if (!stream->code_buffer) {
                stream->code_buf_size = 256;
                stream->code_buffer = (char*)malloc(stream->code_buf_size);
            }
            if (stream->code_buffer) stream->code_buffer[0] = '\0';
            
            /* We'll render the code block when it ends */
        } else {
            /* End code block - render it now */
            stream->state = MD_STATE_NORMAL;
            
            const char* lang = stream->code_lang && stream->code_lang[0] ? stream->code_lang : "code";
            const char* code = stream->code_buffer ? stream->code_buffer : "";
            
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
            
            /* Draw code block */
            int lang_len = md_utf8_display_width(lang);
            int content_width = max_width > lang_len ? max_width : lang_len;
            int box_inner = content_width + 2;
            
            output(stream, MD_STYLE_BOLD);
            output(stream, MD_COLOR_BRIGHT_YELLOW);
            output(stream, MD_BOX_TOP_LEFT);
            output(stream, MD_BOX_HORIZONTAL);
            output(stream, " ");
            output(stream, lang);
            output(stream, " ");
            int remaining = box_inner - lang_len - 3;
            if (remaining > 0) {
                output_n(stream, MD_BOX_HORIZONTAL, remaining);
            }
            output(stream, MD_BOX_TOP_RIGHT);
            output(stream, MD_STYLE_RESET);
            output(stream, "\n");
            
            p = code;
            while (*p) {
                output(stream, MD_COLOR_BRIGHT_YELLOW);
                output(stream, MD_BOX_VERTICAL);
                output(stream, " ");
                output(stream, MD_STYLE_RESET);
                
                const char* line_start = p;
                while (*p && *p != '\n') p++;
                
                int line_width = 0;
                if (p > line_start) {
                    char* code_line = md_strndup(line_start, p - line_start);
                    if (code_line) {
                        output(stream, code_line);
                        line_width = md_utf8_display_width(code_line);
                        free(code_line);
                    }
                }
                
                /* Pad and add right border */
                int padding = content_width - line_width;
                if (padding > 0) {
                    output_n(stream, " ", padding);
                }
                output(stream, " ");
                output(stream, MD_COLOR_BRIGHT_YELLOW);
                output(stream, MD_BOX_VERTICAL);
                output(stream, MD_STYLE_RESET);
                output(stream, "\n");
                if (*p == '\n') p++;
            }
            
            output(stream, MD_COLOR_BRIGHT_YELLOW);
            output(stream, MD_BOX_BOTTOM_LEFT);
            output_n(stream, MD_BOX_HORIZONTAL, box_inner);
            output(stream, MD_BOX_BOTTOM_RIGHT);
            output(stream, MD_STYLE_RESET);
            output(stream, "\n\n");
            
            free(stream->code_lang);
            stream->code_lang = NULL;
        }
        return;
    }
    
    if (stream->state == MD_STATE_CODE_BLOCK) {
        /* Accumulate code */
        md_buffer_append(&stream->code_buffer, &stream->code_buf_size, &stream->code_buf_len, line);
        md_buffer_append(&stream->code_buffer, &stream->code_buf_size, &stream->code_buf_len, "\n");
        return;
    }
    
    /* ---- Empty line ---- */
    if (len == 0) {
        stream->in_list = 0;
        return;
    }
    
    /* ---- Heading ---- */
    if (line[0] == '#') {
        stream->in_list = 0;
        int level = 0;
        const char* p = line;
        while (*p == '#' && level < 6) {
            level++;
            p++;
        }
        if (*p == ' ') {
            p++;
            const char* color;
            switch (level) {
                case 1: color = MD_HEADING1_COLOR; break;
                case 2: color = MD_HEADING2_COLOR; break;
                case 3: color = MD_HEADING3_COLOR; break;
                case 4: color = MD_HEADING4_COLOR; break;
                case 5: color = MD_HEADING5_COLOR; break;
                case 6: color = MD_HEADING6_COLOR; break;
                default: color = MD_STYLE_BOLD; break;
            }
            output(stream, color);
            output(stream, MD_STYLE_BOLD);
            md_inline_token_t* content = md_parse_inline(p);
            render_and_free_inline(stream, content);
            output(stream, MD_STYLE_RESET);
            output(stream, "\n\n");
            return;
        }
    }
    
    /* ---- Horizontal rule ---- */
    if ((strncmp(line, "---", 3) == 0 || strncmp(line, "***", 3) == 0 || strncmp(line, "___", 3) == 0)) {
        /* Check if it's all the same character (possibly with spaces) */
        char c = line[0];
        int is_hr = 1;
        int count = 0;
        for (const char* p = line; *p; p++) {
            if (*p == c) count++;
            else if (*p != ' ' && *p != '\t') { is_hr = 0; break; }
        }
        if (is_hr && count >= 3) {
            stream->in_list = 0;
            output(stream, MD_COLOR_DARK_GRAY);
            output_n(stream, "_", stream->renderer.term_width);
            output(stream, MD_STYLE_RESET);
            output(stream, "\n\n");
            return;
        }
    }
    
    /* ---- Block quote ---- */
    if (line[0] == '>') {
        stream->in_list = 0;
        const char* content = line + 1;
        if (*content == ' ') content++;
        
        output(stream, MD_BG_DARK_GRAY);
        output(stream, MD_COLOR_LIGHT_GRAY);
        output(stream, "> ");
        output(stream, MD_STYLE_ITALIC);
        md_inline_token_t* tokens = md_parse_inline(content);
        render_and_free_inline(stream, tokens);
        output(stream, MD_STYLE_RESET);
        output(stream, "\n\n");
        return;
    }
    
    /* ---- Unordered list ---- */
    const char* trimmed = md_ltrim(line);
    if ((*trimmed == '-' || *trimmed == '*' || *trimmed == '+') && 
        trimmed[1] == ' ') {
        int indent = md_count_indent(line) / 2;
        const char* content = trimmed + 2;
        
        if (!stream->in_list) {
            stream->in_list = 1;
            stream->list_type = MD_LIST_UNORDERED;
        }
        
        /* Indentation */
        output_n(stream, "  ", indent);
        
        /* Bullet */
        const char* bullet;
        switch (indent % 3) {
            case 0: bullet = MD_BULLET_LEVEL0; break;
            case 1: bullet = MD_BULLET_LEVEL1; break;
            default: bullet = MD_BULLET_LEVEL2; break;
        }
        output(stream, bullet);
        output(stream, " ");
        
        md_inline_token_t* tokens = md_parse_inline(content);
        render_and_free_inline(stream, tokens);
        output(stream, "\n");
        return;
    }
    
    /* ---- Ordered list ---- */
    if (*trimmed >= '0' && *trimmed <= '9') {
        const char* p = trimmed;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.' && *(p+1) == ' ') {
            int indent = md_count_indent(line) / 3;
            const char* content = p + 2;
            
            if (!stream->in_list || stream->list_type != MD_LIST_ORDERED) {
                stream->in_list = 1;
                stream->list_type = MD_LIST_ORDERED;
                stream->list_item_number = 1;
            }
            
            /* Indentation */
            output_n(stream, "  ", indent);
            
            /* Number */
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "%d. ", stream->list_item_number);
            output(stream, num_buf);
            stream->list_item_number++;
            
            md_inline_token_t* tokens = md_parse_inline(content);
            render_and_free_inline(stream, tokens);
            output(stream, "\n");
            return;
        }
    }
    
    /* ---- Default: Paragraph ---- */
    stream->in_list = 0;
    md_inline_token_t* tokens = md_parse_inline(line);
    render_and_free_inline(stream, tokens);
    output(stream, "\n\n");
}

/* ========== Streaming interface ========== */

void md_stream_feed(md_stream_t* stream, const char* data, size_t len) {
    if (!stream || !data || len == 0) return;
    
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        
        if (c == '\n') {
            /* Complete line - process it */
            if (stream->line_buffer) {
                stream->line_buffer[stream->line_buf_len] = '\0';
            }
            process_line(stream, stream->line_buffer ? stream->line_buffer : "");
            
            /* Reset line buffer */
            stream->line_buf_len = 0;
            if (stream->line_buffer) stream->line_buffer[0] = '\0';
        } else if (c != '\r') {
            /* Add to line buffer */
            md_buffer_append_char(&stream->line_buffer, &stream->line_buf_size, 
                                  &stream->line_buf_len, c);
        }
    }
}

void md_stream_feed_str(md_stream_t* stream, const char* str) {
    if (!str) return;
    md_stream_feed(stream, str, strlen(str));
}

void md_stream_finish(md_stream_t* stream) {
    if (!stream) return;
    
    /* Process any remaining content in line buffer */
    if (stream->line_buf_len > 0) {
        if (stream->line_buffer) {
            stream->line_buffer[stream->line_buf_len] = '\0';
        }
        process_line(stream, stream->line_buffer ? stream->line_buffer : "");
        stream->line_buf_len = 0;
    }
    
    /* If we're in an unclosed code block, render what we have */
    if (stream->state == MD_STATE_CODE_BLOCK && stream->code_buffer) {
        output(stream, stream->code_buffer);
    }
    
    stream->state = MD_STATE_NORMAL;
}
