/**
 * @file http_client.c
 * @brief HTTP client common implementation (Platform Layer)
 *
 * This file provides platform-agnostic HTTP client utilities.
 * Platform-specific implementations are in posix/http/, windows/http/, etc.
 */

#include "http_client.h"
#include "arc/platform.h"
#include <string.h>
#include <ctype.h>

/*============================================================================
 * Header Helpers
 *============================================================================*/

arc_http_header_t *arc_http_header_create(const char *name, const char *value) {
    if (!name || !value) return NULL;

    arc_http_header_t *h = ARC_CALLOC(1, sizeof(arc_http_header_t));
    if (!h) return NULL;

    h->name = ARC_STRDUP(name);
    h->value = ARC_STRDUP(value);
    h->next = NULL;

    if (!h->name || !h->value) {
        ARC_FREE((void*)h->name);
        ARC_FREE((void*)h->value);
        ARC_FREE(h);
        return NULL;
    }

    return h;
}

void arc_http_header_append(arc_http_header_t **list, arc_http_header_t *header) {
    if (!list || !header) return;

    if (!*list) {
        *list = header;
        return;
    }

    arc_http_header_t *tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = header;
}

const arc_http_header_t *arc_http_header_find(
    const arc_http_header_t *list,
    const char *name
) {
    if (!name) return NULL;

    for (const arc_http_header_t *h = list; h; h = h->next) {
        if (h->name && strcasecmp(h->name, name) == 0) {
            return h;
        }
    }
    return NULL;
}

void arc_http_header_free(arc_http_header_t *list) {
    while (list) {
        arc_http_header_t *next = list->next;
        ARC_FREE((void*)list->name);
        ARC_FREE((void*)list->value);
        ARC_FREE(list);
        list = next;
    }
}

/*============================================================================
 * Response Helpers
 *============================================================================*/

void arc_http_response_free(arc_http_response_t *response) {
    if (!response) return;

    arc_http_header_free(response->headers);
    ARC_FREE(response->body);
    ARC_FREE(response->error_msg);

    memset(response, 0, sizeof(*response));
}
