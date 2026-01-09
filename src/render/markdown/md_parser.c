/**
 * @file md_parser.c
 * @brief Markdown parser implementation using PCRE2
 */

#include "md_parser.h"
#include "md_utils.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ========== Regex patterns ========== */
static pcre2_code* re_heading = NULL;
static pcre2_code* re_quote = NULL;
static pcre2_code* re_bullet = NULL;
static pcre2_code* re_ordered = NULL;
static pcre2_code* re_hr = NULL;
static pcre2_code* re_table_sep = NULL;

static int regex_initialized = 0;

/* ========== Helper macros ========== */
#define STARTS_WITH(s, prefix) (strncmp((s), (prefix), strlen(prefix)) == 0)

/* ========== Regex compilation ========== */

static pcre2_code* compile_regex(const char* pattern) {
    int errornumber;
    PCRE2_SIZE erroroffset;
    pcre2_code* re = pcre2_compile(
        (PCRE2_SPTR)pattern,
        PCRE2_ZERO_TERMINATED,
        0,
        &errornumber,
        &erroroffset,
        NULL
    );
    if (re == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(errornumber, buffer, sizeof(buffer));
        fprintf(stderr, "PCRE2 compile error at offset %d: %s\n", (int)erroroffset, buffer);
    }
    return re;
}

static void init_regex(void) {
    if (regex_initialized) return;
    
    re_heading = compile_regex("^(#{1,6})\\s+(.*)$");
    re_quote = compile_regex("^\\s*>\\s?(.*)$");
    re_bullet = compile_regex("^(\\s*)([-*+])\\s+(.*)$");
    re_ordered = compile_regex("^(\\s*)(\\d+)\\.\\s+(.*)$");
    re_hr = compile_regex("^\\s*([-*_])\\s*\\1\\s*\\1\\s*$");
    re_table_sep = compile_regex("^\\|?\\s*(:?-+:?)\\s*(\\|\\s*:?-+:?\\s*)*\\|?\\s*$");
    
    regex_initialized = 1;
}

/* ========== Inline parser ========== */

/* Parse until a delimiter is found, return content before delimiter */
static char* parse_until(const char* text, size_t* pos, const char* delim) {
    size_t start = *pos;
    size_t delim_len = strlen(delim);
    
    while (text[*pos] != '\0') {
        if (strncmp(text + *pos, delim, delim_len) == 0) {
            break;
        }
        (*pos)++;
    }
    
    return md_strndup(text + start, *pos - start);
}

/* Create a new inline token */
static md_inline_token_t* new_inline_token(md_inline_type_t type, const char* text, const char* url) {
    md_inline_token_t* tok = (md_inline_token_t*)calloc(1, sizeof(md_inline_token_t));
    if (!tok) return NULL;
    tok->type = type;
    tok->text = text ? md_strdup(text) : NULL;
    tok->url = url ? md_strdup(url) : NULL;
    tok->next = NULL;
    return tok;
}

/* Append token to list */
static void append_inline_token(md_inline_token_t** head, md_inline_token_t** tail, md_inline_token_t* tok) {
    if (!tok) return;
    if (*tail) {
        (*tail)->next = tok;
        *tail = tok;
    } else {
        *head = *tail = tok;
    }
}

md_inline_token_t* md_parse_inline(const char* text) {
    if (!text || !*text) return NULL;
    
    md_inline_token_t* head = NULL;
    md_inline_token_t* tail = NULL;
    size_t pos = 0;
    size_t len = strlen(text);
    
    char* plain_buf = NULL;
    size_t plain_size = 0;
    size_t plain_len = 0;
    
    /* Flush accumulated plain text */
    #define FLUSH_PLAIN() do { \
        if (plain_len > 0) { \
            md_inline_token_t* tok = new_inline_token(MD_INLINE_PLAIN, plain_buf, NULL); \
            append_inline_token(&head, &tail, tok); \
            plain_len = 0; \
            if (plain_buf) plain_buf[0] = '\0'; \
        } \
    } while(0)
    
    while (pos < len) {
        /* Bold+Italic: *** */
        if (pos + 2 < len && text[pos] == '*' && text[pos+1] == '*' && text[pos+2] == '*') {
            FLUSH_PLAIN();
            pos += 3;
            char* content = parse_until(text, &pos, "***");
            if (pos + 3 <= len && strncmp(text + pos, "***", 3) == 0) {
                pos += 3;
                md_inline_token_t* tok = new_inline_token(MD_INLINE_BOLD_ITALIC, content, NULL);
                append_inline_token(&head, &tail, tok);
            } else {
                /* No closing, treat as plain */
                md_buffer_append(&plain_buf, &plain_size, &plain_len, "***");
                md_buffer_append(&plain_buf, &plain_size, &plain_len, content);
            }
            free(content);
            continue;
        }
        
        /* Bold: ** or __ */
        if (pos + 1 < len && ((text[pos] == '*' && text[pos+1] == '*') ||
                              (text[pos] == '_' && text[pos+1] == '_'))) {
            char delim[3] = {text[pos], text[pos+1], '\0'};
            FLUSH_PLAIN();
            pos += 2;
            char* content = parse_until(text, &pos, delim);
            if (pos + 2 <= len && strncmp(text + pos, delim, 2) == 0) {
                pos += 2;
                md_inline_token_t* tok = new_inline_token(MD_INLINE_BOLD, content, NULL);
                append_inline_token(&head, &tail, tok);
            } else {
                md_buffer_append(&plain_buf, &plain_size, &plain_len, delim);
                md_buffer_append(&plain_buf, &plain_size, &plain_len, content);
            }
            free(content);
            continue;
        }
        
        /* Italic: * or _ (single) */
        if ((text[pos] == '*' || text[pos] == '_') && 
            (pos + 1 >= len || text[pos+1] != text[pos])) {
            char delim[2] = {text[pos], '\0'};
            FLUSH_PLAIN();
            pos += 1;
            char* content = parse_until(text, &pos, delim);
            if (pos < len && text[pos] == delim[0]) {
                pos += 1;
                md_inline_token_t* tok = new_inline_token(MD_INLINE_ITALIC, content, NULL);
                append_inline_token(&head, &tail, tok);
            } else {
                md_buffer_append(&plain_buf, &plain_size, &plain_len, delim);
                md_buffer_append(&plain_buf, &plain_size, &plain_len, content);
            }
            free(content);
            continue;
        }
        
        /* Inline code: ` */
        if (text[pos] == '`') {
            FLUSH_PLAIN();
            pos += 1;
            char* content = parse_until(text, &pos, "`");
            if (pos < len && text[pos] == '`') {
                pos += 1;
                md_inline_token_t* tok = new_inline_token(MD_INLINE_CODE, content, NULL);
                append_inline_token(&head, &tail, tok);
            } else {
                md_buffer_append(&plain_buf, &plain_size, &plain_len, "`");
                md_buffer_append(&plain_buf, &plain_size, &plain_len, content);
            }
            free(content);
            continue;
        }
        
        /* Link: [text](url) */
        if (text[pos] == '[') {
            size_t bracket_start = pos + 1;
            size_t bracket_end = bracket_start;
            
            /* Find closing ] */
            while (bracket_end < len && text[bracket_end] != ']') {
                bracket_end++;
            }
            
            if (bracket_end < len && bracket_end + 1 < len && text[bracket_end + 1] == '(') {
                size_t paren_start = bracket_end + 2;
                size_t paren_end = paren_start;
                
                /* Find closing ) */
                while (paren_end < len && text[paren_end] != ')') {
                    paren_end++;
                }
                
                if (paren_end < len) {
                    FLUSH_PLAIN();
                    char* link_text = md_strndup(text + bracket_start, bracket_end - bracket_start);
                    char* link_url = md_strndup(text + paren_start, paren_end - paren_start);
                    md_inline_token_t* tok = new_inline_token(MD_INLINE_LINK, link_text, link_url);
                    append_inline_token(&head, &tail, tok);
                    free(link_text);
                    free(link_url);
                    pos = paren_end + 1;
                    continue;
                }
            }
            /* Not a valid link, treat [ as plain text */
            md_buffer_append_char(&plain_buf, &plain_size, &plain_len, '[');
            pos++;
            continue;
        }
        
        /* Plain text */
        md_buffer_append_char(&plain_buf, &plain_size, &plain_len, text[pos]);
        pos++;
    }
    
    FLUSH_PLAIN();
    free(plain_buf);
    
    #undef FLUSH_PLAIN
    
    return head;
}

/* ========== Block parser helpers ========== */

/* Match regex and get captured groups */
static int match_regex(pcre2_code* re, const char* subject, 
                       PCRE2_SIZE* ovector, int ovector_count) {
    if (!re) return 0;
    
    pcre2_match_data* match_data = pcre2_match_data_create(ovector_count, NULL);
    if (!match_data) return 0;
    
    int rc = pcre2_match(re, (PCRE2_SPTR)subject, PCRE2_ZERO_TERMINATED, 
                         0, 0, match_data, NULL);
    
    if (rc > 0) {
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer(match_data);
        for (int i = 0; i < rc * 2 && i < ovector_count * 2; i++) {
            ovector[i] = ov[i];
        }
    }
    
    pcre2_match_data_free(match_data);
    return rc > 0 ? rc : 0;
}

/* Extract substring from match */
static char* extract_match(const char* subject, PCRE2_SIZE start, PCRE2_SIZE end) {
    if (start == PCRE2_UNSET || end == PCRE2_UNSET) return NULL;
    return md_strndup(subject + start, end - start);
}

/* Create a new block token */
static md_block_token_t* new_block_token(md_block_type_t type) {
    md_block_token_t* tok = (md_block_token_t*)calloc(1, sizeof(md_block_token_t));
    if (tok) {
        tok->type = type;
    }
    return tok;
}

/* Append block token to list */
static void append_block_token(md_block_token_t** head, md_block_token_t** tail, md_block_token_t* tok) {
    if (!tok) return;
    if (*tail) {
        (*tail)->next = tok;
        *tail = tok;
    } else {
        *head = *tail = tok;
    }
}

/* ========== Table parser ========== */

/* Parse table alignment from separator row */
static md_align_t parse_align(const char* cell) {
    const char* trimmed = md_ltrim(cell);
    size_t len = strlen(trimmed);
    
    /* Trim trailing whitespace */
    while (len > 0 && isspace((unsigned char)trimmed[len-1])) {
        len--;
    }
    
    if (len == 0) return MD_ALIGN_LEFT;
    
    int left_colon = (trimmed[0] == ':');
    int right_colon = (len > 0 && trimmed[len-1] == ':');
    
    if (left_colon && right_colon) return MD_ALIGN_CENTER;
    if (right_colon) return MD_ALIGN_RIGHT;
    return MD_ALIGN_LEFT;
}

/* Split a table row into cells */
static md_inline_token_t** split_table_row(const char* line, size_t* out_count) {
    /* Skip leading | */
    const char* p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '|') p++;
    
    /* Count cells */
    size_t count = 0;
    const char* tmp = p;
    while (*tmp) {
        if (*tmp == '|') count++;
        tmp++;
    }
    count++; /* Last cell after final content */
    
    md_inline_token_t** cells = (md_inline_token_t**)calloc(count + 1, sizeof(md_inline_token_t*));
    if (!cells) {
        *out_count = 0;
        return NULL;
    }
    
    size_t idx = 0;
    while (*p) {
        /* Find cell content */
        const char* start = p;
        while (*p && *p != '|') p++;
        
        /* Trim and parse cell */
        char* cell = md_strndup(start, p - start);
        if (cell) {
            md_rtrim(cell);
            const char* trimmed = md_ltrim(cell);
            if (*trimmed) {
                cells[idx] = md_parse_inline(trimmed);
            }
            free(cell);
        }
        idx++;
        
        if (*p == '|') p++;
    }
    
    /* Remove trailing empty cells */
    while (idx > 0 && cells[idx-1] == NULL) {
        idx--;
    }
    
    *out_count = idx;
    return cells;
}

/* ========== List parser ========== */

static md_list_item_t* new_list_item(void) {
    return (md_list_item_t*)calloc(1, sizeof(md_list_item_t));
}

/* ========== Main parser ========== */

md_block_token_t* md_parse(const char* markdown) {
    if (!markdown || !*markdown) return NULL;
    
    init_regex();
    
    md_block_token_t* head = NULL;
    md_block_token_t* tail = NULL;
    
    /* Split into lines */
    char* input = md_strdup(markdown);
    if (!input) return NULL;
    
    char* line = input;
    char* next_line;
    
    /* State tracking */
    int in_code_block = 0;
    char* code_lang = NULL;
    char* code_buffer = NULL;
    size_t code_buf_size = 0;
    size_t code_buf_len = 0;
    
    int in_table = 0;
    md_block_token_t* current_table = NULL;
    
    int in_list = 0;
    md_list_type_t current_list_type = MD_LIST_UNORDERED;
    md_block_token_t* current_list = NULL;
    md_list_item_t* list_tail = NULL;
    
    while (line) {
        /* Find end of line */
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }
        
        /* Remove trailing \r */
        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len-1] == '\r') {
            line[line_len-1] = '\0';
            line_len--;
        }
        
        PCRE2_SIZE ov[20];
        
        /* ---- Code block handling ---- */
        if (STARTS_WITH(line, "```")) {
            if (!in_code_block) {
                /* Start code block */
                in_code_block = 1;
                in_list = 0;
                in_table = 0;
                code_lang = md_strdup(line + 3);
                if (code_lang) md_rtrim(code_lang);
                code_buf_len = 0;
                if (!code_buffer) {
                    code_buf_size = 256;
                    code_buffer = (char*)malloc(code_buf_size);
                    if (code_buffer) code_buffer[0] = '\0';
                }
            } else {
                /* End code block */
                in_code_block = 0;
                md_block_token_t* tok = new_block_token(MD_BLOCK_CODE);
                if (tok) {
                    tok->data.code.lang = code_lang;
                    tok->data.code.code = md_strdup(code_buffer ? code_buffer : "");
                    append_block_token(&head, &tail, tok);
                }
                code_lang = NULL;
                code_buf_len = 0;
            }
            line = next_line;
            continue;
        }
        
        if (in_code_block) {
            md_buffer_append(&code_buffer, &code_buf_size, &code_buf_len, line);
            md_buffer_append(&code_buffer, &code_buf_size, &code_buf_len, "\n");
            line = next_line;
            continue;
        }
        
        /* ---- Empty line ---- */
        if (line_len == 0 || (line_len == 1 && isspace((unsigned char)line[0]))) {
            in_list = 0;
            in_table = 0;
            line = next_line;
            continue;
        }
        
        /* ---- Heading ---- */
        if (match_regex(re_heading, line, ov, 10)) {
            in_list = 0;
            in_table = 0;
            char* hashes = extract_match(line, ov[2], ov[3]);
            char* content = extract_match(line, ov[4], ov[5]);
            
            md_block_token_t* tok = new_block_token(MD_BLOCK_HEADING);
            if (tok && hashes && content) {
                tok->data.heading.level = (int)strlen(hashes);
                tok->data.heading.content = md_parse_inline(content);
                append_block_token(&head, &tail, tok);
            }
            free(hashes);
            free(content);
            line = next_line;
            continue;
        }
        
        /* ---- Horizontal rule ---- */
        if (match_regex(re_hr, line, ov, 10)) {
            in_list = 0;
            in_table = 0;
            md_block_token_t* tok = new_block_token(MD_BLOCK_HR);
            append_block_token(&head, &tail, tok);
            line = next_line;
            continue;
        }
        
        /* ---- Block quote ---- */
        if (match_regex(re_quote, line, ov, 10)) {
            in_list = 0;
            in_table = 0;
            char* content = extract_match(line, ov[2], ov[3]);
            md_block_token_t* tok = new_block_token(MD_BLOCK_QUOTE);
            if (tok) {
                tok->data.quote.content = md_parse_inline(content ? content : "");
                append_block_token(&head, &tail, tok);
            }
            free(content);
            line = next_line;
            continue;
        }
        
        /* ---- Unordered list ---- */
        if (match_regex(re_bullet, line, ov, 10)) {
            in_table = 0;
            char* indent_str = extract_match(line, ov[2], ov[3]);
            char* content = extract_match(line, ov[6], ov[7]);
            int indent = indent_str ? md_count_indent(indent_str) : 0;
            
            if (!in_list || current_list_type != MD_LIST_UNORDERED) {
                /* Start new list */
                in_list = 1;
                current_list_type = MD_LIST_UNORDERED;
                current_list = new_block_token(MD_BLOCK_LIST);
                if (current_list) {
                    current_list->data.list.type = MD_LIST_UNORDERED;
                    current_list->data.list.items = NULL;
                    append_block_token(&head, &tail, current_list);
                }
                list_tail = NULL;
            }
            
            /* Add item */
            md_list_item_t* item = new_list_item();
            if (item) {
                item->content = md_parse_inline(content ? content : "");
                item->indent_level = indent / 2; /* 2 spaces per level */
                if (list_tail) {
                    list_tail->next = item;
                } else if (current_list) {
                    current_list->data.list.items = item;
                }
                list_tail = item;
            }
            
            free(indent_str);
            free(content);
            line = next_line;
            continue;
        }
        
        /* ---- Ordered list ---- */
        if (match_regex(re_ordered, line, ov, 10)) {
            in_table = 0;
            char* indent_str = extract_match(line, ov[2], ov[3]);
            char* content = extract_match(line, ov[6], ov[7]);
            int indent = indent_str ? md_count_indent(indent_str) : 0;
            
            if (!in_list || current_list_type != MD_LIST_ORDERED) {
                in_list = 1;
                current_list_type = MD_LIST_ORDERED;
                current_list = new_block_token(MD_BLOCK_LIST);
                if (current_list) {
                    current_list->data.list.type = MD_LIST_ORDERED;
                    current_list->data.list.items = NULL;
                    append_block_token(&head, &tail, current_list);
                }
                list_tail = NULL;
            }
            
            md_list_item_t* item = new_list_item();
            if (item) {
                item->content = md_parse_inline(content ? content : "");
                item->indent_level = indent / 3; /* 3 chars per level (e.g., "1. ") */
                if (list_tail) {
                    list_tail->next = item;
                } else if (current_list) {
                    current_list->data.list.items = item;
                }
                list_tail = item;
            }
            
            free(indent_str);
            free(content);
            line = next_line;
            continue;
        }
        
        /* ---- Table ---- */
        /* Check if this could be a table header (contains |) */
        if (!in_table && !in_list && strchr(line, '|')) {
            /* Look ahead for separator line */
            if (next_line) {
                char* sep_line = next_line;
                char* sep_end = strchr(sep_line, '\n');
                char saved = 0;
                if (sep_end) {
                    saved = *sep_end;
                    *sep_end = '\0';
                }
                
                if (match_regex(re_table_sep, sep_line, ov, 10)) {
                    /* This is a table! */
                    in_table = 1;
                    in_list = 0;
                    
                    current_table = new_block_token(MD_BLOCK_TABLE);
                    if (current_table) {
                        /* Parse header row */
                        size_t col_count;
                        current_table->data.table.headers = split_table_row(line, &col_count);
                        current_table->data.table.col_count = col_count;
                        current_table->data.table.row_count = 0;
                        current_table->data.table.rows = NULL;
                        
                        /* Parse alignments from separator */
                        current_table->data.table.aligns = (md_align_t*)calloc(col_count, sizeof(md_align_t));
                        if (current_table->data.table.aligns) {
                            const char* p = sep_line;
                            while (*p && isspace((unsigned char)*p)) p++;
                            if (*p == '|') p++;
                            
                            size_t align_idx = 0;
                            while (*p && align_idx < col_count) {
                                const char* cell_start = p;
                                while (*p && *p != '|') p++;
                                char* cell = md_strndup(cell_start, p - cell_start);
                                if (cell) {
                                    current_table->data.table.aligns[align_idx] = parse_align(cell);
                                    free(cell);
                                }
                                align_idx++;
                                if (*p == '|') p++;
                            }
                        }
                        
                        append_block_token(&head, &tail, current_table);
                    }
                    
                    /* Skip separator line */
                    if (sep_end) *sep_end = saved;
                    next_line = sep_end ? sep_end + 1 : NULL;
                    line = next_line;
                    continue;
                }
                
                if (sep_end) *sep_end = saved;
            }
        }
        
        /* Continue parsing table rows */
        if (in_table && current_table && strchr(line, '|')) {
            size_t row_col_count;
            md_inline_token_t** row = split_table_row(line, &row_col_count);
            
            if (row) {
                /* Resize rows array */
                size_t new_count = current_table->data.table.row_count + 1;
                md_inline_token_t*** new_rows = (md_inline_token_t***)realloc(
                    current_table->data.table.rows,
                    new_count * sizeof(md_inline_token_t**)
                );
                if (new_rows) {
                    current_table->data.table.rows = new_rows;
                    current_table->data.table.rows[current_table->data.table.row_count] = row;
                    current_table->data.table.row_count = new_count;
                } else {
                    free(row);
                }
            }
            
            line = next_line;
            continue;
        }
        
        /* ---- Default: Paragraph ---- */
        in_list = 0;
        in_table = 0;
        md_block_token_t* tok = new_block_token(MD_BLOCK_PARAGRAPH);
        if (tok) {
            tok->data.paragraph.content = md_parse_inline(line);
            append_block_token(&head, &tail, tok);
        }
        
        line = next_line;
    }
    
    /* Cleanup */
    free(input);
    free(code_buffer);
    
    return head;
}

/* ========== Memory cleanup ========== */

void md_free_inline_tokens(md_inline_token_t* token) {
    while (token) {
        md_inline_token_t* next = token->next;
        free(token->text);
        free(token->url);
        free(token);
        token = next;
    }
}

void md_free_list_items(md_list_item_t* item) {
    while (item) {
        md_list_item_t* next = item->next;
        md_free_inline_tokens(item->content);
        md_free_list_items(item->children);
        free(item);
        item = next;
    }
}

void md_free_tokens(md_block_token_t* token) {
    while (token) {
        md_block_token_t* next = token->next;
        
        switch (token->type) {
            case MD_BLOCK_HEADING:
                md_free_inline_tokens(token->data.heading.content);
                break;
            case MD_BLOCK_PARAGRAPH:
                md_free_inline_tokens(token->data.paragraph.content);
                break;
            case MD_BLOCK_QUOTE:
                md_free_inline_tokens(token->data.quote.content);
                break;
            case MD_BLOCK_LIST:
                md_free_list_items(token->data.list.items);
                break;
            case MD_BLOCK_CODE:
                free(token->data.code.lang);
                free(token->data.code.code);
                break;
            case MD_BLOCK_TABLE:
                /* Free headers */
                if (token->data.table.headers) {
                    for (size_t i = 0; i < token->data.table.col_count; i++) {
                        md_free_inline_tokens(token->data.table.headers[i]);
                    }
                    free(token->data.table.headers);
                }
                /* Free rows */
                if (token->data.table.rows) {
                    for (size_t r = 0; r < token->data.table.row_count; r++) {
                        if (token->data.table.rows[r]) {
                            for (size_t c = 0; c < token->data.table.col_count; c++) {
                                md_free_inline_tokens(token->data.table.rows[r][c]);
                            }
                            free(token->data.table.rows[r]);
                        }
                    }
                    free(token->data.table.rows);
                }
                free(token->data.table.aligns);
                break;
            case MD_BLOCK_HR:
                /* Nothing to free */
                break;
        }
        
        free(token);
        token = next;
    }
}
