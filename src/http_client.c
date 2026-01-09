/**
 * @file http_client.c
 * @brief HTTP client common implementation
 */

#include "agentc/http_client.h"
#include "agentc/platform.h"
#include <string.h>
#include <ctype.h>

/*============================================================================
 * Header Helpers
 *============================================================================*/

agentc_http_header_t *agentc_http_header_create(const char *name, const char *value) {
    if (!name || !value) return NULL;
    
    agentc_http_header_t *h = AGENTC_CALLOC(1, sizeof(agentc_http_header_t));
    if (!h) return NULL;
    
    h->name = AGENTC_STRDUP(name);
    h->value = AGENTC_STRDUP(value);
    h->next = NULL;
    
    if (!h->name || !h->value) {
        AGENTC_FREE((void*)h->name);
        AGENTC_FREE((void*)h->value);
        AGENTC_FREE(h);
        return NULL;
    }
    
    return h;
}

void agentc_http_header_append(agentc_http_header_t **list, agentc_http_header_t *header) {
    if (!list || !header) return;
    
    if (!*list) {
        *list = header;
        return;
    }
    
    agentc_http_header_t *tail = *list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = header;
}

const agentc_http_header_t *agentc_http_header_find(
    const agentc_http_header_t *list,
    const char *name
) {
    if (!name) return NULL;
    
    for (const agentc_http_header_t *h = list; h; h = h->next) {
        if (h->name && strcasecmp(h->name, name) == 0) {
            return h;
        }
    }
    return NULL;
}

void agentc_http_header_free(agentc_http_header_t *list) {
    while (list) {
        agentc_http_header_t *next = list->next;
        AGENTC_FREE((void*)list->name);
        AGENTC_FREE((void*)list->value);
        AGENTC_FREE(list);
        list = next;
    }
}

/*============================================================================
 * Response Helpers
 *============================================================================*/

void agentc_http_response_free(agentc_http_response_t *response) {
    if (!response) return;
    
    agentc_http_header_free(response->headers);
    AGENTC_FREE(response->body);
    AGENTC_FREE(response->error_msg);
    
    memset(response, 0, sizeof(*response));
}
