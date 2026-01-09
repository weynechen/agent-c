/**
 * @file chat_tui.c
 * @brief WeChat-style TUI chatbot using AgentC and Notcurses
 *
 * Features:
 *   - User messages aligned right, AI messages aligned left
 *   - Scrollable message history
 *   - Editable input area with cursor support
 *   - Streaming response display
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./chat_tui
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <notcurses/notcurses.h>
#include "agentc.h"
#include "dotenv.h"

/*============================================================================
 * Constants
 *============================================================================*/

#define MAX_MESSAGES 100
#define MAX_MESSAGE_LEN 8192
#define INPUT_HEIGHT 3
#define HEADER_HEIGHT 2
#define BUBBLE_PADDING 2
#define MAX_BUBBLE_WIDTH_RATIO 0.7  /* Max 70% of screen width for bubbles */

/*============================================================================
 * Color Definitions
 *============================================================================*/

/* Color scheme - modern dark theme */
#define COLOR_BG           0x1a1a2e
#define COLOR_HEADER_BG    0x16213e
#define COLOR_USER_BUBBLE  0x0f3460
#define COLOR_AI_BUBBLE    0x2d2d44
#define COLOR_INPUT_BG     0x252540
#define COLOR_TEXT         0xe8e8e8
#define COLOR_TEXT_DIM     0x888888
#define COLOR_ACCENT       0x00adb5
#define COLOR_USER_NAME    0x00d4ff
#define COLOR_AI_NAME      0x7fdbda

/*============================================================================
 * Message Structure
 *============================================================================*/

typedef enum {
    MSG_USER,
    MSG_ASSISTANT,
    MSG_SYSTEM
} msg_type_t;

typedef struct {
    msg_type_t type;
    char *content;
    int content_len;
} chat_message_t;

/*============================================================================
 * Application State
 *============================================================================*/

typedef struct {
    struct notcurses *nc;
    struct ncplane *stdplane;
    struct ncplane *header_plane;
    struct ncplane *messages_plane;
    struct ncplane *input_plane;
    struct ncplane *input_border_plane;
    
    /* Dimensions */
    unsigned int term_rows;
    unsigned int term_cols;
    unsigned int msg_area_rows;
    
    /* Messages */
    chat_message_t messages[MAX_MESSAGES];
    int message_count;
    int scroll_offset;
    
    /* Input buffer (manual implementation for better control) */
    char input_buffer[MAX_MESSAGE_LEN];
    int input_len;
    int cursor_pos;
    
    /* Streaming state */
    int is_streaming;
    char *streaming_buffer;
    int streaming_len;
    int streaming_cap;
    
    /* LLM */
    agentc_llm_client_t *llm;
    agentc_message_t *history;
    const char *model_name;
    
    /* Running state */
    volatile int running;
} app_state_t;

static app_state_t g_app = {0};

/*============================================================================
 * Utility Functions
 *============================================================================*/

static void app_cleanup(void) {
    if (g_app.nc) {
        notcurses_stop(g_app.nc);
        g_app.nc = NULL;
    }
    
    /* Free messages */
    for (int i = 0; i < g_app.message_count; i++) {
        free(g_app.messages[i].content);
    }
    
    /* Free streaming buffer */
    free(g_app.streaming_buffer);
    
    /* Free LLM resources */
    if (g_app.history) {
        agentc_message_free(g_app.history);
    }
    if (g_app.llm) {
        agentc_llm_destroy(g_app.llm);
    }
    
    agentc_cleanup();
}

static void signal_handler(int sig) {
    (void)sig;
    g_app.running = 0;
}

/* Calculate display width of a UTF-8 string */
static int utf8_display_width(const char *str, int len) {
    int width = 0;
    const char *end = str + len;
    while (str < end && *str) {
        unsigned char c = (unsigned char)*str;
        if (c < 0x80) {
            width++;
            str++;
        } else if ((c & 0xE0) == 0xC0) {
            width++;  /* Assume 1 cell width */
            str += 2;
        } else if ((c & 0xF0) == 0xE0) {
            width += 2;  /* CJK characters are 2 cells */
            str += 3;
        } else if ((c & 0xF8) == 0xF0) {
            width += 2;  /* Emoji etc */
            str += 4;
        } else {
            str++;
        }
    }
    return width;
}

/*============================================================================
 * Message Management
 *============================================================================*/

static void add_message(msg_type_t type, const char *content) {
    if (g_app.message_count >= MAX_MESSAGES) {
        /* Remove oldest message */
        free(g_app.messages[0].content);
        memmove(&g_app.messages[0], &g_app.messages[1], 
                sizeof(chat_message_t) * (MAX_MESSAGES - 1));
        g_app.message_count--;
    }
    
    g_app.messages[g_app.message_count].type = type;
    g_app.messages[g_app.message_count].content = strdup(content);
    g_app.messages[g_app.message_count].content_len = strlen(content);
    g_app.message_count++;
}

/*============================================================================
 * Rendering Functions
 *============================================================================*/

static void render_header(void) {
    struct ncplane *n = g_app.header_plane;
    
    /* Clear header */
    uint64_t ch = NCCHANNELS_INITIALIZER(
        (COLOR_TEXT >> 16) & 0xff, (COLOR_TEXT >> 8) & 0xff, COLOR_TEXT & 0xff,
        (COLOR_HEADER_BG >> 16) & 0xff, (COLOR_HEADER_BG >> 8) & 0xff, COLOR_HEADER_BG & 0xff
    );
    ncplane_set_channels(n, ch);
    ncplane_erase(n);
    
    /* Title */
    ncplane_set_fg_rgb(n, COLOR_ACCENT);
    ncplane_set_bg_rgb(n, COLOR_HEADER_BG);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_printf_yx(n, 0, 2, "🤖 AgentC Chat");
    
    /* Model info */
    ncplane_set_fg_rgb(n, COLOR_TEXT_DIM);
    ncplane_set_styles(n, NCSTYLE_NONE);
    ncplane_printf_yx(n, 0, g_app.term_cols - 30, "Model: %s", 
                      g_app.model_name ? g_app.model_name : "gpt-3.5-turbo");
    
    /* Separator line */
    ncplane_set_fg_rgb(n, COLOR_ACCENT);
    for (unsigned int i = 0; i < g_app.term_cols; i++) {
        ncplane_putstr_yx(n, 1, i, "─");
    }
}

/* Word wrap a string, returns number of lines */
static int wrap_text(const char *text, int max_width, char ***lines_out, int **line_lens) {
    if (!text || max_width <= 0) return 0;
    
    int capacity = 16;
    char **lines = malloc(capacity * sizeof(char*));
    int *lens = malloc(capacity * sizeof(int));
    int count = 0;
    
    const char *p = text;
    
    while (*p) {
        /* Find line break or wrap point */
        const char *line_start = p;
        int line_width = 0;
        const char *wrap_point = NULL;
        const char *line_end = p;
        
        while (*p && *p != '\n') {
            unsigned char c = (unsigned char)*p;
            int char_width = 1;
            int char_len = 1;
            
            if (c < 0x80) {
                char_len = 1;
                char_width = 1;
            } else if ((c & 0xE0) == 0xC0) {
                char_len = 2;
                char_width = 1;
            } else if ((c & 0xF0) == 0xE0) {
                char_len = 3;
                char_width = 2;
            } else if ((c & 0xF8) == 0xF0) {
                char_len = 4;
                char_width = 2;
            }
            
            if (line_width + char_width > max_width) {
                /* Need to wrap */
                if (wrap_point && wrap_point > line_start) {
                    line_end = wrap_point;
                } else {
                    line_end = p;
                }
                break;
            }
            
            line_width += char_width;
            line_end = p + char_len;
            
            /* Track word boundaries */
            if (*p == ' ') {
                wrap_point = p + 1;
            }
            
            p += char_len;
        }
        
        /* Store line */
        if (count >= capacity) {
            capacity *= 2;
            lines = realloc(lines, capacity * sizeof(char*));
            lens = realloc(lens, capacity * sizeof(int));
        }
        
        int len = line_end - line_start;
        lines[count] = malloc(len + 1);
        memcpy(lines[count], line_start, len);
        lines[count][len] = '\0';
        lens[count] = len;
        count++;
        
        /* Skip newline */
        if (*p == '\n') p++;
        else if (line_end > line_start) p = line_end;
        
        /* Skip leading space on wrapped lines */
        while (*p == ' ') p++;
    }
    
    *lines_out = lines;
    *line_lens = lens;
    return count;
}

static void free_wrapped_lines(char **lines, int *lens, int count) {
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
    free(lens);
}

/* Render a single message bubble, returns height used */
static int render_message_bubble(struct ncplane *n, int y, chat_message_t *msg, 
                                  int max_width, int is_streaming) {
    int bubble_max = (int)(g_app.term_cols * MAX_BUBBLE_WIDTH_RATIO);
    if (bubble_max < 20) bubble_max = 20;
    int content_max = bubble_max - BUBBLE_PADDING * 2 - 2;
    
    char **lines;
    int *line_lens;
    int line_count = wrap_text(msg->content, content_max, &lines, &line_lens);
    if (line_count == 0) return 0;
    
    /* Calculate bubble width */
    int max_line_width = 0;
    for (int i = 0; i < line_count; i++) {
        int w = utf8_display_width(lines[i], line_lens[i]);
        if (w > max_line_width) max_line_width = w;
    }
    int bubble_width = max_line_width + BUBBLE_PADDING * 2 + 2;
    if (bubble_width > bubble_max) bubble_width = bubble_max;
    
    int bubble_height = line_count + 2;  /* +2 for top/bottom borders */
    
    /* Calculate x position */
    int x;
    uint32_t bubble_color, name_color;
    const char *name_label;
    
    if (msg->type == MSG_USER) {
        x = g_app.term_cols - bubble_width - 2;
        bubble_color = COLOR_USER_BUBBLE;
        name_color = COLOR_USER_NAME;
        name_label = "You";
    } else {
        x = 2;
        bubble_color = COLOR_AI_BUBBLE;
        name_color = COLOR_AI_NAME;
        name_label = "AI";
    }
    
    if (y < 0) {
        free_wrapped_lines(lines, line_lens, line_count);
        return bubble_height + 2;  /* +2 for name label and spacing */
    }
    
    /* Render name label */
    ncplane_set_fg_rgb(n, name_color);
    ncplane_set_bg_rgb(n, COLOR_BG);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    if (msg->type == MSG_USER) {
        ncplane_printf_yx(n, y, x + bubble_width - strlen(name_label), "%s", name_label);
    } else {
        ncplane_printf_yx(n, y, x, "%s", name_label);
    }
    y++;
    
    /* Set bubble colors */
    ncplane_set_fg_rgb(n, COLOR_TEXT);
    ncplane_set_bg_rgb(n, bubble_color);
    ncplane_set_styles(n, NCSTYLE_NONE);
    
    /* Top border */
    ncplane_putstr_yx(n, y, x, "╭");
    for (int i = 1; i < bubble_width - 1; i++) {
        ncplane_putstr(n, "─");
    }
    ncplane_putstr(n, "╮");
    y++;
    
    /* Content lines */
    for (int i = 0; i < line_count; i++) {
        ncplane_printf_yx(n, y, x, "│");
        ncplane_set_bg_rgb(n, bubble_color);
        
        /* Padding and content */
        for (int p = 0; p < BUBBLE_PADDING; p++) {
            ncplane_putstr(n, " ");
        }
        ncplane_putstr(n, lines[i]);
        
        /* Fill remaining space */
        int line_w = utf8_display_width(lines[i], line_lens[i]);
        int remaining = bubble_width - 2 - BUBBLE_PADDING * 2 - line_w;
        for (int p = 0; p < remaining; p++) {
            ncplane_putstr(n, " ");
        }
        for (int p = 0; p < BUBBLE_PADDING; p++) {
            ncplane_putstr(n, " ");
        }
        
        ncplane_putstr(n, "│");
        y++;
    }
    
    /* Bottom border with optional streaming indicator */
    ncplane_printf_yx(n, y, x, "╰");
    for (int i = 1; i < bubble_width - 1; i++) {
        ncplane_putstr(n, "─");
    }
    ncplane_putstr(n, "╯");
    y++;
    
    /* Streaming indicator */
    if (is_streaming) {
        ncplane_set_fg_rgb(n, COLOR_ACCENT);
        ncplane_set_bg_rgb(n, COLOR_BG);
        ncplane_putstr_yx(n, y - 1, x + bubble_width + 1, "▌");
    }
    
    free_wrapped_lines(lines, line_lens, line_count);
    return bubble_height + 2;  /* +2 for name label and spacing */
}

static void render_messages(void) {
    struct ncplane *n = g_app.messages_plane;
    
    /* Clear messages area */
    ncplane_set_fg_rgb(n, COLOR_TEXT);
    ncplane_set_bg_rgb(n, COLOR_BG);
    ncplane_erase(n);
    
    if (g_app.message_count == 0) {
        /* Welcome message */
        ncplane_set_fg_rgb(n, COLOR_TEXT_DIM);
        int y = g_app.msg_area_rows / 2 - 1;
        int x = (g_app.term_cols - 30) / 2;
        ncplane_printf_yx(n, y, x, "Welcome to AgentC Chat!");
        ncplane_printf_yx(n, y + 1, x - 5, "Type a message and press Enter to start");
        return;
    }
    
    /* Calculate total height needed */
    int total_height = 0;
    int *msg_heights = malloc(g_app.message_count * sizeof(int));
    
    for (int i = 0; i < g_app.message_count; i++) {
        int h = render_message_bubble(n, -1, &g_app.messages[i], g_app.term_cols, 0);
        msg_heights[i] = h;
        total_height += h;
    }
    
    /* Auto-scroll to bottom */
    int visible_height = g_app.msg_area_rows;
    int start_y = visible_height - total_height;
    if (start_y > 0) start_y = 0;
    start_y += g_app.scroll_offset;
    
    /* Render messages */
    int y = start_y;
    for (int i = 0; i < g_app.message_count; i++) {
        int is_last = (i == g_app.message_count - 1);
        int is_streaming = is_last && g_app.is_streaming;
        
        if (y + msg_heights[i] > 0 && y < (int)g_app.msg_area_rows) {
            render_message_bubble(n, y, &g_app.messages[i], g_app.term_cols, is_streaming);
        }
        y += msg_heights[i];
    }
    
    free(msg_heights);
}

static void render_input(void) {
    struct ncplane *border = g_app.input_border_plane;
    struct ncplane *input = g_app.input_plane;
    
    /* Border plane */
    ncplane_set_fg_rgb(border, COLOR_ACCENT);
    ncplane_set_bg_rgb(border, COLOR_INPUT_BG);
    ncplane_erase(border);
    
    /* Top border */
    ncplane_putstr_yx(border, 0, 0, "╭");
    for (unsigned int i = 1; i < g_app.term_cols - 2; i++) {
        ncplane_putstr(border, "─");
    }
    ncplane_putstr(border, "╮");
    
    /* Side borders */
    ncplane_putstr_yx(border, 1, 0, "│");
    ncplane_putstr_yx(border, 1, g_app.term_cols - 2, "│");
    
    /* Bottom border */
    ncplane_putstr_yx(border, 2, 0, "╰");
    for (unsigned int i = 1; i < g_app.term_cols - 2; i++) {
        ncplane_putstr(border, "─");
    }
    ncplane_putstr(border, "╯");
    
    /* Input area */
    ncplane_set_fg_rgb(input, COLOR_TEXT);
    ncplane_set_bg_rgb(input, COLOR_INPUT_BG);
    ncplane_erase(input);
    
    /* Prompt */
    ncplane_set_fg_rgb(input, COLOR_ACCENT);
    ncplane_putstr_yx(input, 0, 0, "▶ ");
    
    /* Input text */
    ncplane_set_fg_rgb(input, COLOR_TEXT);
    if (g_app.input_len > 0) {
        ncplane_putstr(input, g_app.input_buffer);
    } else {
        ncplane_set_fg_rgb(input, COLOR_TEXT_DIM);
        ncplane_putstr(input, "Type a message...");
    }
    
    /* Send button hint */
    ncplane_set_fg_rgb(input, COLOR_TEXT_DIM);
    int hint_x = g_app.term_cols - 20;
    if (hint_x > 0) {
        ncplane_printf_yx(input, 0, hint_x, "[Enter] Send");
    }
    
    /* Position cursor */
    if (g_app.input_len > 0) {
        int cursor_x = 2 + utf8_display_width(g_app.input_buffer, g_app.cursor_pos);
        ncplane_cursor_move_yx(input, 0, cursor_x);
    }
}

static void render_all(void) {
    render_header();
    render_messages();
    render_input();
    notcurses_render(g_app.nc);
}

/*============================================================================
 * Layout Management
 *============================================================================*/

static void setup_planes(void) {
    notcurses_term_dim_yx(g_app.nc, &g_app.term_rows, &g_app.term_cols);
    g_app.msg_area_rows = g_app.term_rows - HEADER_HEIGHT - INPUT_HEIGHT;
    
    /* Destroy existing planes */
    if (g_app.header_plane && g_app.header_plane != g_app.stdplane) {
        ncplane_destroy(g_app.header_plane);
    }
    if (g_app.messages_plane) {
        ncplane_destroy(g_app.messages_plane);
    }
    if (g_app.input_plane) {
        ncplane_destroy(g_app.input_plane);
    }
    if (g_app.input_border_plane) {
        ncplane_destroy(g_app.input_border_plane);
    }
    
    g_app.stdplane = notcurses_stdplane(g_app.nc);
    
    /* Set background */
    ncplane_set_bg_rgb(g_app.stdplane, COLOR_BG);
    ncplane_erase(g_app.stdplane);
    
    /* Header plane */
    struct ncplane_options header_opts = {
        .y = 0,
        .x = 0,
        .rows = HEADER_HEIGHT,
        .cols = g_app.term_cols,
    };
    g_app.header_plane = ncplane_create(g_app.stdplane, &header_opts);
    
    /* Messages plane */
    struct ncplane_options msg_opts = {
        .y = HEADER_HEIGHT,
        .x = 0,
        .rows = g_app.msg_area_rows,
        .cols = g_app.term_cols,
    };
    g_app.messages_plane = ncplane_create(g_app.stdplane, &msg_opts);
    
    /* Input border plane */
    struct ncplane_options border_opts = {
        .y = g_app.term_rows - INPUT_HEIGHT,
        .x = 1,
        .rows = INPUT_HEIGHT,
        .cols = g_app.term_cols - 2,
    };
    g_app.input_border_plane = ncplane_create(g_app.stdplane, &border_opts);
    
    /* Input content plane */
    struct ncplane_options input_opts = {
        .y = g_app.term_rows - INPUT_HEIGHT + 1,
        .x = 2,
        .rows = 1,
        .cols = g_app.term_cols - 4,
    };
    g_app.input_plane = ncplane_create(g_app.stdplane, &input_opts);
}

/*============================================================================
 * LLM Interaction
 *============================================================================*/

static int on_stream_chunk(const char *data, size_t len, void *user_data) {
    (void)user_data;
    
    /* Accumulate streaming content */
    if (g_app.streaming_len + (int)len >= g_app.streaming_cap) {
        g_app.streaming_cap = (g_app.streaming_len + len + 1) * 2;
        g_app.streaming_buffer = realloc(g_app.streaming_buffer, g_app.streaming_cap);
    }
    
    memcpy(g_app.streaming_buffer + g_app.streaming_len, data, len);
    g_app.streaming_len += len;
    g_app.streaming_buffer[g_app.streaming_len] = '\0';
    
    /* Update the last message (which is the streaming response) */
    if (g_app.message_count > 0) {
        chat_message_t *last = &g_app.messages[g_app.message_count - 1];
        free(last->content);
        last->content = strdup(g_app.streaming_buffer);
        last->content_len = g_app.streaming_len;
    }
    
    /* Render update */
    render_messages();
    render_input();
    notcurses_render(g_app.nc);
    
    return g_app.running ? 0 : -1;
}

static void on_stream_done(const char *finish_reason, int total_tokens, void *user_data) {
    (void)finish_reason;
    (void)total_tokens;
    (void)user_data;
    
    g_app.is_streaming = 0;
    
    /* Add to conversation history */
    if (g_app.streaming_buffer && g_app.streaming_len > 0) {
        agentc_message_append(&g_app.history,
            agentc_message_create(AGENTC_ROLE_ASSISTANT, g_app.streaming_buffer));
    }
    
    render_all();
}

static void send_message(void) {
    if (g_app.input_len == 0 || g_app.is_streaming) return;
    
    /* Add user message to display */
    add_message(MSG_USER, g_app.input_buffer);
    
    /* Add to conversation history */
    agentc_message_append(&g_app.history,
        agentc_message_create(AGENTC_ROLE_USER, g_app.input_buffer));
    
    /* Clear input */
    memset(g_app.input_buffer, 0, sizeof(g_app.input_buffer));
    g_app.input_len = 0;
    g_app.cursor_pos = 0;
    
    /* Add placeholder for AI response */
    add_message(MSG_ASSISTANT, "...");
    
    /* Start streaming */
    g_app.is_streaming = 1;
    g_app.streaming_len = 0;
    if (g_app.streaming_buffer) {
        g_app.streaming_buffer[0] = '\0';
    }
    
    render_all();
    
    /* Build request */
    agentc_chat_request_t req = {
        .messages = g_app.history,
        .temperature = 0.7f,
    };
    
    /* Perform streaming request */
    agentc_err_t err = agentc_llm_chat_stream(g_app.llm, &req,
        on_stream_chunk, on_stream_done, NULL);
    
    if (err != AGENTC_OK) {
        /* Update last message with error */
        if (g_app.message_count > 0) {
            chat_message_t *last = &g_app.messages[g_app.message_count - 1];
            free(last->content);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Error: %s", agentc_strerror(err));
            last->content = strdup(error_msg);
            last->content_len = strlen(error_msg);
        }
        g_app.is_streaming = 0;
        render_all();
    }
}

/*============================================================================
 * Input Handling
 *============================================================================*/

static void handle_backspace(void) {
    if (g_app.cursor_pos > 0 && g_app.input_len > 0) {
        /* Find start of previous character */
        int prev_pos = g_app.cursor_pos - 1;
        while (prev_pos > 0 && (g_app.input_buffer[prev_pos] & 0xC0) == 0x80) {
            prev_pos--;
        }
        
        /* Shift remaining text */
        int del_len = g_app.cursor_pos - prev_pos;
        memmove(&g_app.input_buffer[prev_pos],
                &g_app.input_buffer[g_app.cursor_pos],
                g_app.input_len - g_app.cursor_pos + 1);
        
        g_app.input_len -= del_len;
        g_app.cursor_pos = prev_pos;
    }
}

static void handle_delete(void) {
    if (g_app.cursor_pos < g_app.input_len) {
        /* Find end of current character */
        int next_pos = g_app.cursor_pos + 1;
        while (next_pos < g_app.input_len && (g_app.input_buffer[next_pos] & 0xC0) == 0x80) {
            next_pos++;
        }
        
        /* Shift remaining text */
        int del_len = next_pos - g_app.cursor_pos;
        memmove(&g_app.input_buffer[g_app.cursor_pos],
                &g_app.input_buffer[next_pos],
                g_app.input_len - next_pos + 1);
        
        g_app.input_len -= del_len;
    }
}

static void handle_left(void) {
    if (g_app.cursor_pos > 0) {
        g_app.cursor_pos--;
        while (g_app.cursor_pos > 0 && (g_app.input_buffer[g_app.cursor_pos] & 0xC0) == 0x80) {
            g_app.cursor_pos--;
        }
    }
}

static void handle_right(void) {
    if (g_app.cursor_pos < g_app.input_len) {
        g_app.cursor_pos++;
        while (g_app.cursor_pos < g_app.input_len && 
               (g_app.input_buffer[g_app.cursor_pos] & 0xC0) == 0x80) {
            g_app.cursor_pos++;
        }
    }
}

static void handle_home(void) {
    g_app.cursor_pos = 0;
}

static void handle_end(void) {
    g_app.cursor_pos = g_app.input_len;
}

static void insert_char(const char *utf8, int len) {
    if (g_app.input_len + len >= MAX_MESSAGE_LEN - 1) return;
    
    /* Make room for new character */
    memmove(&g_app.input_buffer[g_app.cursor_pos + len],
            &g_app.input_buffer[g_app.cursor_pos],
            g_app.input_len - g_app.cursor_pos + 1);
    
    /* Insert character */
    memcpy(&g_app.input_buffer[g_app.cursor_pos], utf8, len);
    g_app.cursor_pos += len;
    g_app.input_len += len;
}

static void handle_input(ncinput *ni) {
    if (ni->id == NCKEY_ENTER) {
        send_message();
        return;
    }
    
    if (ni->id == NCKEY_BACKSPACE) {
        handle_backspace();
        return;
    }
    
    if (ni->id == NCKEY_DEL) {  /* Delete key */
        handle_delete();
        return;
    }
    
    if (ni->id == NCKEY_LEFT) {
        handle_left();
        return;
    }
    
    if (ni->id == NCKEY_RIGHT) {
        handle_right();
        return;
    }
    
    if (ni->id == NCKEY_HOME) {
        handle_home();
        return;
    }
    
    if (ni->id == NCKEY_END) {
        handle_end();
        return;
    }
    
    if (ni->id == NCKEY_UP) {
        g_app.scroll_offset += 3;
        return;
    }
    
    if (ni->id == NCKEY_DOWN) {
        g_app.scroll_offset -= 3;
        if (g_app.scroll_offset < 0) g_app.scroll_offset = 0;
        return;
    }
    
    if (ni->id == NCKEY_PGUP) {
        g_app.scroll_offset += g_app.msg_area_rows / 2;
        return;
    }
    
    if (ni->id == NCKEY_PGDOWN) {
        g_app.scroll_offset -= g_app.msg_area_rows / 2;
        if (g_app.scroll_offset < 0) g_app.scroll_offset = 0;
        return;
    }
    
    /* Ctrl+C to quit */
    if (ni->ctrl && (ni->id == 'c' || ni->id == 'C')) {
        g_app.running = 0;
        return;
    }
    
    /* Ctrl+L to clear history */
    if (ni->ctrl && (ni->id == 'l' || ni->id == 'L')) {
        for (int i = 0; i < g_app.message_count; i++) {
            free(g_app.messages[i].content);
        }
        g_app.message_count = 0;
        g_app.scroll_offset = 0;
        
        agentc_message_free(g_app.history);
        g_app.history = NULL;
        agentc_message_append(&g_app.history,
            agentc_message_create(AGENTC_ROLE_SYSTEM,
                "You are a helpful assistant. Be concise and clear."));
        return;
    }
    
    /* Regular character input */
    if (ni->id >= 32 && ni->id < 0x110000 && !ni->ctrl && !ni->alt) {
        char utf8[5] = {0};
        int len = 0;
        
        if (ni->id < 0x80) {
            utf8[0] = (char)ni->id;
            len = 1;
        } else if (ni->id < 0x800) {
            utf8[0] = 0xC0 | (ni->id >> 6);
            utf8[1] = 0x80 | (ni->id & 0x3F);
            len = 2;
        } else if (ni->id < 0x10000) {
            utf8[0] = 0xE0 | (ni->id >> 12);
            utf8[1] = 0x80 | ((ni->id >> 6) & 0x3F);
            utf8[2] = 0x80 | (ni->id & 0x3F);
            len = 3;
        } else {
            utf8[0] = 0xF0 | (ni->id >> 18);
            utf8[1] = 0x80 | ((ni->id >> 12) & 0x3F);
            utf8[2] = 0x80 | ((ni->id >> 6) & 0x3F);
            utf8[3] = 0x80 | (ni->id & 0x3F);
            len = 4;
        }
        
        insert_char(utf8, len);
    }
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    /* Set locale for UTF-8 */
    setlocale(LC_ALL, "");
    
    /* Load environment */
    if (env_load(".", false) == 0) {
        fprintf(stderr, "[Loaded .env file]\n");
    }
    
    /* Get API key */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        fprintf(stderr, "Error: OPENAI_API_KEY not set\n");
        fprintf(stderr, "Create a .env file with: OPENAI_API_KEY=sk-xxx\n");
        return 1;
    }
    
    const char *base_url = getenv("OPENAI_BASE_URL");
    g_app.model_name = getenv("OPENAI_MODEL");
    if (!g_app.model_name) g_app.model_name = "gpt-3.5-turbo";
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    
    /* Initialize AgentC */
    agentc_err_t err = agentc_init();
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to initialize AgentC: %s\n", agentc_strerror(err));
        return 1;
    }
    
    /* Create LLM client */
    agentc_llm_config_t config = {
        .api_key = api_key,
        .base_url = base_url,
        .model = g_app.model_name,
        .timeout_ms = 120000,
    };
    
    err = agentc_llm_create(&config, &g_app.llm);
    if (err != AGENTC_OK) {
        fprintf(stderr, "Failed to create LLM client: %s\n", agentc_strerror(err));
        agentc_cleanup();
        return 1;
    }
    
    /* Add system message to history */
    agentc_message_append(&g_app.history,
        agentc_message_create(AGENTC_ROLE_SYSTEM,
            "You are a helpful assistant. Be concise and clear."));
    
    /* Allocate streaming buffer */
    g_app.streaming_cap = 4096;
    g_app.streaming_buffer = malloc(g_app.streaming_cap);
    
    /* Initialize Notcurses */
    struct notcurses_options opts = {
        .flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_ALTERNATE_SCREEN,
    };
    
    g_app.nc = notcurses_init(&opts, NULL);
    if (!g_app.nc) {
        fprintf(stderr, "Failed to initialize Notcurses\n");
        app_cleanup();
        return 1;
    }
    
    /* Use alternate screen for cleaner exit */
    notcurses_enter_alternate_screen(g_app.nc);
    
    /* Setup planes */
    setup_planes();
    
    g_app.running = 1;
    render_all();
    
    /* Main event loop */
    ncinput ni;
    while (g_app.running) {
        uint32_t id = notcurses_get(g_app.nc, NULL, &ni);
        
        if (id == (uint32_t)-1) {
            break;  /* Error */
        }
        
        if (id == NCKEY_RESIZE) {
            setup_planes();
            render_all();
            continue;
        }
        
        if (id != 0) {
            handle_input(&ni);
            render_all();
        }
    }
    
    /* Leave alternate screen */
    notcurses_leave_alternate_screen(g_app.nc);
    
    app_cleanup();
    printf("Goodbye!\n");
    return 0;
}
