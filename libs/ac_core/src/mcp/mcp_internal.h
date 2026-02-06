/**
 * @file mcp_internal.h
 * @brief MCP Internal Transport Interface
 *
 * Defines the abstract transport layer for MCP protocol.
 * Concrete implementations: mcp_http.c (Streamable HTTP), mcp_sse.c (SSE)
 */

#ifndef MCP_INTERNAL_H
#define MCP_INTERNAL_H

#include "arc/mcp.h"
#include "arc/arena.h"
#include "arc/log.h"
#include "arc/platform.h"
#include "http_client.h"
#include "cJSON.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define MCP_PROTOCOL_VERSION    "2024-11-05"
#define MCP_DEFAULT_TIMEOUT_MS  30000
#define MCP_INITIAL_TOOL_CAP    16
#define MCP_ERROR_MSG_SIZE      256

/*============================================================================
 * Transport Interface
 *============================================================================*/

typedef struct mcp_transport mcp_transport_t;

/**
 * @brief Transport operations (virtual function table)
 */
typedef struct {
    /**
     * @brief Establish transport connection
     *
     * For HTTP: no-op (stateless)
     * For SSE: GET request to establish SSE stream and get endpoint
     */
    arc_err_t (*connect)(mcp_transport_t *t);

    /**
     * @brief Send JSON-RPC request and wait for response
     *
     * @param t             Transport handle
     * @param request_json  JSON-RPC request string
     * @param request_id    Request ID for matching response
     * @param response_json Output: response string (caller must free)
     * @return ARC_OK on success
     */
    arc_err_t (*request)(
        mcp_transport_t *t,
        const char *request_json,
        int request_id,
        char **response_json
    );

    /**
     * @brief Disconnect transport
     */
    void (*disconnect)(mcp_transport_t *t);

    /**
     * @brief Destroy transport and free resources
     */
    void (*destroy)(mcp_transport_t *t);
} mcp_transport_ops_t;

/**
 * @brief Transport base structure
 *
 * Concrete transports embed this as first member for polymorphism.
 */
struct mcp_transport {
    const mcp_transport_ops_t *ops;
    arc_http_client_t *http;
    arena_t *arena;

    /* Configuration */
    char *server_url;
    char *api_key;
    uint32_t timeout_ms;
    int verify_ssl;

    /* State */
    int connected;
    char error_msg[MCP_ERROR_MSG_SIZE];
};

/*============================================================================
 * Transport Constructors
 *============================================================================*/

/**
 * @brief Create HTTP (Streamable HTTP) transport
 */
mcp_transport_t *mcp_http_create(
    arena_t *arena,
    arc_http_client_t *http,
    const ac_mcp_config_t *config
);

/**
 * @brief Create SSE transport
 */
mcp_transport_t *mcp_sse_create(
    arena_t *arena,
    arc_http_client_t *http,
    const ac_mcp_config_t *config
);

/*============================================================================
 * Helper: Set Transport Error
 *============================================================================*/

static inline void mcp_transport_set_error(mcp_transport_t *t, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(t->error_msg, MCP_ERROR_MSG_SIZE, fmt, args);
    va_end(args);
    AC_LOG_ERROR("MCP transport: %s", t->error_msg);
}

/*============================================================================
 * Helper: Build HTTP Headers
 *============================================================================*/

static inline arc_http_header_t *mcp_build_headers(
    mcp_transport_t *t,
    const char *content_type,
    const char *accept
) {
    arc_http_header_t *headers = NULL;

    if (content_type) {
        arc_http_header_t *ct = arc_http_header_create("Content-Type", content_type);
        arc_http_header_append(&headers, ct);
    }

    if (accept) {
        arc_http_header_t *acc = arc_http_header_create("Accept", accept);
        arc_http_header_append(&headers, acc);
    }

    if (t->api_key) {
        char auth_value[512];
        snprintf(auth_value, sizeof(auth_value), "Bearer %s", t->api_key);
        arc_http_header_t *auth = arc_http_header_create("Authorization", auth_value);
        arc_http_header_append(&headers, auth);
    }

    return headers;
}

#endif /* MCP_INTERNAL_H */
