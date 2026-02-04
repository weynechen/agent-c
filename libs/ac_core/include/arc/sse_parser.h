/**
 * @file sse_parser.h
 * @brief SSE (Server-Sent Events) Parser
 *
 * Parses SSE events from streaming HTTP responses.
 * Used by both LLM streaming and MCP SSE transport.
 */

#ifndef ARC_SSE_PARSER_H
#define ARC_SSE_PARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * SSE Event Structure
 *============================================================================*/

typedef struct {
    const char *event;  /**< Event type (e.g., "message", "endpoint") */
    const char *data;   /**< Event data (JSON string) */
    const char *id;     /**< Event ID (optional) */
} sse_event_t;

/*============================================================================
 * SSE Parser Callback
 *============================================================================*/

/**
 * @brief SSE event callback
 * @param event  Parsed SSE event
 * @param ctx    User context
 * @return 0 to continue, non-zero to abort
 */
typedef int (*sse_event_callback_t)(const sse_event_t *event, void *ctx);

/*============================================================================
 * SSE Parser Structure
 *============================================================================*/

typedef struct {
    char *buffer;           /**< Line buffer */
    size_t buffer_size;     /**< Buffer capacity */
    size_t buffer_len;      /**< Current buffer length */

    char *event_type;       /**< Current event type */
    char *data;             /**< Current data (accumulated) */
    char *id;               /**< Current ID */

    sse_event_callback_t callback;
    void *ctx;
    int aborted;
} sse_parser_t;

/*============================================================================
 * SSE Parser API
 *============================================================================*/

/**
 * @brief Initialize SSE parser
 *
 * @param p         Parser to initialize
 * @param callback  Callback for received events
 * @param ctx       User context passed to callback
 */
void sse_parser_init(sse_parser_t *p, sse_event_callback_t callback, void *ctx);

/**
 * @brief Free SSE parser resources
 *
 * @param p  Parser to free
 */
void sse_parser_free(sse_parser_t *p);

/**
 * @brief Feed data to parser
 *
 * Parses incoming data and invokes callback for each complete event.
 *
 * @param p     Parser
 * @param data  Incoming data
 * @param len   Data length
 * @return 0 on success, -1 if aborted
 */
int sse_parser_feed(sse_parser_t *p, const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ARC_SSE_PARSER_H */
