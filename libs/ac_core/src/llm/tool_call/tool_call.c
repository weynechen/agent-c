/**
 * @file tool_call.c
 * @brief Tool call management implementation
 */

#include "tool_call.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include <string.h>

/*============================================================================
 * Tool Call Operations
 *============================================================================*/

ac_tool_call_t *ac_tool_call_clone(const ac_tool_call_t *calls) {
    if (!calls) return NULL;

    ac_tool_call_t *head = NULL;
    ac_tool_call_t *tail = NULL;

    for (const ac_tool_call_t *src = calls; src; src = src->next) {
        ac_tool_call_t *clone = AGENTC_CALLOC(1, sizeof(ac_tool_call_t));
        if (!clone) {
            ac_tool_call_free(head);
            return NULL;
        }

        clone->id = src->id ? AGENTC_STRDUP(src->id) : NULL;
        clone->name = src->name ? AGENTC_STRDUP(src->name) : NULL;
        clone->arguments = src->arguments ? AGENTC_STRDUP(src->arguments) : NULL;

        if (!head) {
            head = clone;
            tail = clone;
        } else {
            tail->next = clone;
            tail = clone;
        }
    }

    return head;
}
