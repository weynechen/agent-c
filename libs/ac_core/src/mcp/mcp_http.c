/**
 * @file mcp_http.c
 * @brief MCP Streamable HTTP Transport Implementation
 *
 * Simple HTTP POST-based transport where responses are returned directly
 * in the HTTP response body.
 */

#include "mcp_internal.h"
#include <stdlib.h>

/*============================================================================
 * HTTP Transport Structure
 *============================================================================*/

typedef struct {
    mcp_transport_t base;
    /* No additional state needed for HTTP transport */
} mcp_http_transport_t;

/*============================================================================
 * Transport Operations
 *============================================================================*/

static agentc_err_t http_connect(mcp_transport_t *t) {
    /* HTTP is stateless, no connection needed */
    t->connected = 1;
    AC_LOG_DEBUG("HTTP transport: connected (stateless)");
    return AGENTC_OK;
}

static agentc_err_t http_request(
    mcp_transport_t *t,
    const char *request_json,
    int request_id,
    char **response_json
) {
    if (!t->connected) {
        mcp_transport_set_error(t, "Not connected");
        return AGENTC_ERR_NOT_CONNECTED;
    }

    /* Build headers */
    agentc_http_header_t *headers = mcp_build_headers(
        t,
        "application/json",
        "application/json, text/event-stream"
    );

    /* Build request */
    agentc_http_request_t req = {
        .url = t->server_url,
        .method = AGENTC_HTTP_POST,
        .headers = headers,
        .body = request_json,
        .body_len = strlen(request_json),
        .timeout_ms = t->timeout_ms,
        .verify_ssl = t->verify_ssl
    };

    agentc_http_response_t resp = {0};

    AC_LOG_DEBUG("HTTP request: POST %s", t->server_url);

    /* Send request */
    agentc_err_t err = agentc_http_request(t->http, &req, &resp);

    agentc_http_header_free(headers);

    if (err != AGENTC_OK) {
        mcp_transport_set_error(t, "HTTP request failed: %s",
                                 resp.error_msg ? resp.error_msg : ac_strerror(err));
        agentc_http_response_free(&resp);
        return err;
    }

    /* Check status */
    if (resp.status_code < 200 || resp.status_code >= 300) {
        mcp_transport_set_error(t, "HTTP error %d: %s",
                                 resp.status_code,
                                 resp.body ? resp.body : "No body");
        agentc_http_response_free(&resp);
        return AGENTC_ERR_HTTP;
    }

    /* Check response body */
    if (!resp.body || resp.body_len == 0) {
        /* For notifications (request_id == 0), empty response is normal.
         * Server may return 204 No Content or empty body. */
        if (request_id == 0) {
            AC_LOG_DEBUG("HTTP notification: empty response (normal)");
            *response_json = NULL;
            agentc_http_response_free(&resp);
            return AGENTC_OK;
        }
        mcp_transport_set_error(t, "Empty response");
        agentc_http_response_free(&resp);
        return AGENTC_ERR_PROTOCOL;
    }

    AC_LOG_DEBUG("HTTP response: %d, %zu bytes", resp.status_code, resp.body_len);

    /* Return response (caller frees) */
    *response_json = strdup(resp.body);
    agentc_http_response_free(&resp);

    if (!*response_json) {
        return AGENTC_ERR_MEMORY;
    }

    return AGENTC_OK;
}

static void http_disconnect(mcp_transport_t *t) {
    t->connected = 0;
    AC_LOG_DEBUG("HTTP transport: disconnected");
}

static void http_destroy(mcp_transport_t *t) {
    /* Base transport resources are managed by arena */
    AC_LOG_DEBUG("HTTP transport: destroyed");
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const mcp_transport_ops_t http_ops = {
    .connect = http_connect,
    .request = http_request,
    .disconnect = http_disconnect,
    .destroy = http_destroy
};

/*============================================================================
 * Constructor
 *============================================================================*/

mcp_transport_t *mcp_http_create(
    arena_t *arena,
    agentc_http_client_t *http,
    const ac_mcp_config_t *config
) {
    mcp_http_transport_t *t = (mcp_http_transport_t *)arena_alloc(
        arena, sizeof(mcp_http_transport_t)
    );
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));

    /* Initialize base */
    t->base.ops = &http_ops;
    t->base.http = http;
    t->base.arena = arena;
    t->base.server_url = arena_strdup(arena, config->server_url);
    t->base.api_key = config->api_key ? arena_strdup(arena, config->api_key) : NULL;
    t->base.timeout_ms = config->timeout_ms ? config->timeout_ms : MCP_DEFAULT_TIMEOUT_MS;
    t->base.verify_ssl = config->verify_ssl;

    if (!t->base.server_url) {
        return NULL;
    }

    AC_LOG_DEBUG("HTTP transport created for: %s", config->server_url);
    return &t->base;
}
