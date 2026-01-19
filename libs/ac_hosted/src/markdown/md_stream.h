/**
 * @file md_stream.h
 * @brief Streaming Markdown parser and renderer interface
 */

#ifndef MD_STREAM_H
#define MD_STREAM_H

#include "md_types.h"
#include "md_renderer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Stream context for incremental Markdown parsing and rendering
 */
typedef struct md_stream md_stream_t;

/**
 * Create a new stream context
 * @return New stream context, or NULL on failure
 */
md_stream_t* md_stream_new(void);

/**
 * Set output callback for rendering
 * @param stream Stream context
 * @param output Output callback function
 * @param userdata User data passed to callback
 */
void md_stream_set_output(md_stream_t* stream, md_output_fn output, void* userdata);

/**
 * Feed data to the stream
 * Data will be parsed and rendered incrementally as complete lines are received.
 * @param stream Stream context
 * @param data Input data (may be partial, doesn't need to be null-terminated)
 * @param len Length of data
 */
void md_stream_feed(md_stream_t* stream, const char* data, size_t len);

/**
 * Feed a null-terminated string to the stream
 * @param stream Stream context
 * @param str Input string
 */
void md_stream_feed_str(md_stream_t* stream, const char* str);

/**
 * Finish streaming - flush any buffered content
 * @param stream Stream context
 */
void md_stream_finish(md_stream_t* stream);

/**
 * Reset stream state for reuse
 * @param stream Stream context
 */
void md_stream_reset(md_stream_t* stream);

/**
 * Free stream context
 * @param stream Stream context
 */
void md_stream_free(md_stream_t* stream);

#ifdef __cplusplus
}
#endif

#endif /* MD_STREAM_H */
