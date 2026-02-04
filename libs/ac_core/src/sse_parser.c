/**
 * @file sse_parser.c
 * @brief SSE (Server-Sent Events) Parser implementation
 */

#include "arc/sse_parser.h"
#include "arc/platform.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static void emit_event(sse_parser_t *p) {
    if (p->data && p->callback && !p->aborted) {
        sse_event_t event = {
            .event = p->event_type ? p->event_type : "message",
            .data = p->data,
            .id = p->id
        };

        int ret = p->callback(&event, p->ctx);
        if (ret != 0) {
            p->aborted = 1;
        }
    }

    /* Reset current event */
    if (p->event_type) { ARC_FREE(p->event_type); p->event_type = NULL; }
    if (p->data) { ARC_FREE(p->data); p->data = NULL; }
    if (p->id) { ARC_FREE(p->id); p->id = NULL; }
}

static void process_line(sse_parser_t *p, const char *line, size_t len) {
    /* Empty line = dispatch event */
    if (len == 0) {
        emit_event(p);
        return;
    }

    /* Comment line */
    if (line[0] == ':') {
        return;
    }

    /* Find colon separator */
    const char *colon = memchr(line, ':', len);
    size_t field_len;
    const char *value;
    size_t value_len;

    if (colon) {
        field_len = colon - line;
        value = colon + 1;
        value_len = len - field_len - 1;
        /* Skip leading space in value */
        if (value_len > 0 && *value == ' ') {
            value++;
            value_len--;
        }
    } else {
        field_len = len;
        value = "";
        value_len = 0;
    }

    /* Process field */
    if (field_len == 5 && strncmp(line, "event", 5) == 0) {
        if (p->event_type) ARC_FREE(p->event_type);
        p->event_type = ARC_STRNDUP(value, value_len);
    } else if (field_len == 4 && strncmp(line, "data", 4) == 0) {
        if (p->data) {
            /* Append to existing data with newline */
            size_t old_len = strlen(p->data);
            char *new_data = ARC_REALLOC(p->data, old_len + 1 + value_len + 1);
            if (new_data) {
                new_data[old_len] = '\n';
                memcpy(new_data + old_len + 1, value, value_len);
                new_data[old_len + 1 + value_len] = '\0';
                p->data = new_data;
            }
        } else {
            p->data = ARC_STRNDUP(value, value_len);
        }
    } else if (field_len == 2 && strncmp(line, "id", 2) == 0) {
        if (p->id) ARC_FREE(p->id);
        p->id = ARC_STRNDUP(value, value_len);
    }
    /* Ignore other fields */
}

/*============================================================================
 * Public API
 *============================================================================*/

void sse_parser_init(sse_parser_t *p, sse_event_callback_t callback, void *ctx) {
    memset(p, 0, sizeof(*p));
    p->buffer_size = 8192;
    p->buffer = ARC_MALLOC(p->buffer_size);
    p->callback = callback;
    p->ctx = ctx;
}

void sse_parser_free(sse_parser_t *p) {
    if (p->buffer) ARC_FREE(p->buffer);
    if (p->event_type) ARC_FREE(p->event_type);
    if (p->data) ARC_FREE(p->data);
    if (p->id) ARC_FREE(p->id);
    memset(p, 0, sizeof(*p));
}

int sse_parser_feed(sse_parser_t *p, const char *data, size_t len) {
    if (!p || !data || p->aborted) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\n' || c == '\r') {
            /* End of line - process it */
            if (p->buffer_len > 0 || c == '\n') {
                p->buffer[p->buffer_len] = '\0';
                process_line(p, p->buffer, p->buffer_len);
                p->buffer_len = 0;

                if (p->aborted) {
                    return -1;
                }
            }
            /* Skip \r\n as single newline */
            if (c == '\r' && i + 1 < len && data[i + 1] == '\n') {
                i++;
            }
        } else {
            /* Append to buffer */
            if (p->buffer_len + 1 >= p->buffer_size) {
                size_t new_size = p->buffer_size * 2;
                char *new_buf = ARC_REALLOC(p->buffer, new_size);
                if (!new_buf) {
                    return -1;
                }
                p->buffer = new_buf;
                p->buffer_size = new_size;
            }
            p->buffer[p->buffer_len++] = c;
        }
    }

    return 0;
}
