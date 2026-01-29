/**
 * @file mcp_sse.c
 * @brief MCP SSE (Server-Sent Events) Transport Implementation
 *
 * SSE transport requires maintaining a persistent SSE connection:
 * 1. GET /sse to establish SSE stream (keep open)
 * 2. Receive "endpoint" event with POST URL
 * 3. POST JSON-RPC requests to endpoint (returns 202 Accepted)
 * 4. Receive responses via the ORIGINAL SSE stream
 *
 * This implementation uses a background thread to maintain the SSE connection.
 */

#include "mcp_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/*============================================================================
 * SSE Parser
 *============================================================================*/

typedef struct {
    char *event_type;
    char *data;
    char *id;
} sse_event_t;

typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t buffer_len;
    sse_event_t current;

    void (*on_event)(const sse_event_t *event, void *ctx);
    void *ctx;
} sse_parser_t;

static void sse_parser_init(sse_parser_t *p, void (*on_event)(const sse_event_t*, void*), void *ctx) {
    memset(p, 0, sizeof(*p));
    p->buffer_size = 4096;
    p->buffer = (char *)malloc(p->buffer_size);
    p->on_event = on_event;
    p->ctx = ctx;
}

static void sse_parser_free(sse_parser_t *p) {
    if (p->buffer) free(p->buffer);
    if (p->current.event_type) free(p->current.event_type);
    if (p->current.data) free(p->current.data);
    if (p->current.id) free(p->current.id);
    memset(p, 0, sizeof(*p));
}

static void sse_emit_event(sse_parser_t *p) {
    if (p->current.data && p->on_event) {
        p->on_event(&p->current, p->ctx);
    }
    if (p->current.event_type) { free(p->current.event_type); p->current.event_type = NULL; }
    if (p->current.data) { free(p->current.data); p->current.data = NULL; }
    if (p->current.id) { free(p->current.id); p->current.id = NULL; }
}

static void sse_process_line(sse_parser_t *p, const char *line, size_t len) {
    if (len == 0) {
        sse_emit_event(p);
        return;
    }
    if (line[0] == ':') return;

    const char *colon = memchr(line, ':', len);
    size_t field_len;
    const char *value;
    size_t value_len;

    if (colon) {
        field_len = colon - line;
        value = colon + 1;
        value_len = len - field_len - 1;
        if (value_len > 0 && *value == ' ') { value++; value_len--; }
    } else {
        field_len = len;
        value = "";
        value_len = 0;
    }

    if (field_len == 5 && strncmp(line, "event", 5) == 0) {
        if (p->current.event_type) free(p->current.event_type);
        p->current.event_type = strndup(value, value_len);
    } else if (field_len == 4 && strncmp(line, "data", 4) == 0) {
        if (p->current.data) {
            size_t old_len = strlen(p->current.data);
            char *new_data = (char *)realloc(p->current.data, old_len + 1 + value_len + 1);
            if (new_data) {
                new_data[old_len] = '\n';
                memcpy(new_data + old_len + 1, value, value_len);
                new_data[old_len + 1 + value_len] = '\0';
                p->current.data = new_data;
            }
        } else {
            p->current.data = strndup(value, value_len);
        }
    } else if (field_len == 2 && strncmp(line, "id", 2) == 0) {
        if (p->current.id) free(p->current.id);
        p->current.id = strndup(value, value_len);
    }
}

static void sse_parser_feed(sse_parser_t *p, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            size_t line_len = p->buffer_len;
            if (line_len > 0 && p->buffer[line_len - 1] == '\r') line_len--;
            p->buffer[line_len] = '\0';
            sse_process_line(p, p->buffer, line_len);
            p->buffer_len = 0;
        } else {
            if (p->buffer_len >= p->buffer_size - 1) {
                size_t new_size = p->buffer_size * 2;
                char *new_buf = (char *)realloc(p->buffer, new_size);
                if (new_buf) { p->buffer = new_buf; p->buffer_size = new_size; }
            }
            if (p->buffer_len < p->buffer_size - 1) {
                p->buffer[p->buffer_len++] = c;
            }
        }
    }
}

/*============================================================================
 * SSE Transport Structure
 *============================================================================*/

#define SSE_MAX_PENDING_RESPONSES 16

typedef struct {
    int id;
    char *json;
} sse_pending_response_t;

typedef struct {
    mcp_transport_t base;

    char *endpoint;
    char *base_url;

    /* Background SSE thread */
    pthread_t sse_thread;
    volatile int sse_running;
    volatile int sse_connected;  /* 0=waiting, 1=connected, -1=error */

    /* HTTP client for POST requests (separate from SSE stream) */
    agentc_http_client_t *post_http;

    /* Response queue (protected by mutex) */
    pthread_mutex_t mutex;
    sse_pending_response_t responses[SSE_MAX_PENDING_RESPONSES];
    volatile int response_count;

    /* Error from SSE thread */
    char sse_error[256];
} mcp_sse_transport_t;

/*============================================================================
 * SSE Event Handler (called from background thread)
 *============================================================================*/

static void sse_on_event(const sse_event_t *event, void *user_data) {
    mcp_sse_transport_t *sse = (mcp_sse_transport_t *)user_data;

    AC_LOG_DEBUG("SSE event: type=%s, data=%.60s%s",
                 event->event_type ? event->event_type : "(none)",
                 event->data ? event->data : "(none)",
                 (event->data && strlen(event->data) > 60) ? "..." : "");

    /* endpoint event */
    if (event->event_type && strcmp(event->event_type, "endpoint") == 0 && event->data) {
        pthread_mutex_lock(&sse->mutex);
        if (sse->endpoint) free(sse->endpoint);
        sse->endpoint = strdup(event->data);
        sse->sse_connected = 1;
        pthread_mutex_unlock(&sse->mutex);
        AC_LOG_INFO("SSE: endpoint = %s", sse->endpoint);
        return;
    }

    /* message event - JSON-RPC response */
    if (event->data) {
        cJSON *json = cJSON_Parse(event->data);
        if (json) {
            cJSON *jsonrpc = cJSON_GetObjectItem(json, "jsonrpc");
            if (jsonrpc) {
                cJSON *id_json = cJSON_GetObjectItem(json, "id");
                int resp_id = id_json ? (int)cJSON_GetNumberValue(id_json) : 0;

                pthread_mutex_lock(&sse->mutex);
                if (sse->response_count < SSE_MAX_PENDING_RESPONSES) {
                    sse->responses[sse->response_count].id = resp_id;
                    sse->responses[sse->response_count].json = strdup(event->data);
                    sse->response_count++;
                    AC_LOG_DEBUG("SSE: queued response id=%d", resp_id);
                }
                pthread_mutex_unlock(&sse->mutex);
            }
            cJSON_Delete(json);
        }
    }
}

/*============================================================================
 * SSE Stream Callback
 *============================================================================*/

typedef struct {
    mcp_sse_transport_t *sse;
    sse_parser_t parser;
} sse_thread_ctx_t;

static int sse_stream_callback(const char *data, size_t len, void *user_data) {
    sse_thread_ctx_t *ctx = (sse_thread_ctx_t *)user_data;
    sse_parser_feed(&ctx->parser, data, len);

    /* Continue as long as we should be running */
    return ctx->sse->sse_running ? 0 : 1;
}

/*============================================================================
 * SSE Background Thread
 *============================================================================*/

static void *sse_thread_func(void *arg) {
    mcp_sse_transport_t *sse = (mcp_sse_transport_t *)arg;

    AC_LOG_DEBUG("SSE thread started");

    while (sse->sse_running) {
        sse_thread_ctx_t ctx = {0};
        ctx.sse = sse;
        sse_parser_init(&ctx.parser, sse_on_event, sse);

        /* Build headers */
        agentc_http_header_t *headers = mcp_build_headers(&sse->base, NULL, "text/event-stream");

        agentc_http_stream_request_t req = {
            .base = {
                .url = sse->base.server_url,
                .method = AGENTC_HTTP_GET,
                .headers = headers,
                .timeout_ms = 0,  /* No timeout - keep connection open */
                .verify_ssl = sse->base.verify_ssl
            },
            .on_data = sse_stream_callback,
            .user_data = &ctx
        };

        agentc_http_response_t resp = {0};

        AC_LOG_DEBUG("SSE thread: connecting to %s", sse->base.server_url);

        agentc_err_t err = agentc_http_request_stream(sse->base.http, &req, &resp);

        agentc_http_header_free(headers);
        sse_parser_free(&ctx.parser);

        agentc_http_response_free(&resp);

        /* If we're shutting down, exit cleanly */
        if (!sse->sse_running) {
            break;
        }

        if (err != AGENTC_OK) {
            snprintf(sse->sse_error, sizeof(sse->sse_error),
                     "SSE connection failed: %s",
                     resp.error_msg ? resp.error_msg : ac_strerror(err));
            
            /* Timeout during idle is normal for long-running connections.
             * Log as DEBUG since we will automatically reconnect. */
            if (err == AGENTC_ERR_TIMEOUT) {
                AC_LOG_DEBUG("SSE: connection timeout, will reconnect");
            } else {
                AC_LOG_WARN("SSE: %s (will reconnect)", sse->sse_error);
            }

            /* Signal temporary error but keep running for reconnect */
            sse->sse_connected = -1;
        }

        /* Reconnect delay if still running */
        if (sse->sse_running) {
            AC_LOG_DEBUG("SSE thread: reconnecting in 1s...");
            sleep(1);
        }
    }

    AC_LOG_DEBUG("SSE thread exiting");
    return NULL;
}

/*============================================================================
 * Helper: Extract Base URL
 *============================================================================*/

static char *extract_base_url(arena_t *arena, const char *url) {
    const char *scheme_end = strstr(url, "://");
    if (!scheme_end) return NULL;

    const char *path_start = strchr(scheme_end + 3, '/');
    if (!path_start) {
        return arena_strdup(arena, url);
    }

    size_t base_len = path_start - url;
    char *base = (char *)arena_alloc(arena, base_len + 1);
    if (!base) return NULL;

    memcpy(base, url, base_len);
    base[base_len] = '\0';

    return base;
}

/*============================================================================
 * Transport Operations
 *============================================================================*/

static agentc_err_t sse_connect(mcp_transport_t *t) {
    mcp_sse_transport_t *sse = (mcp_sse_transport_t *)t;

    if (sse->sse_running) {
        return AGENTC_OK;
    }

    AC_LOG_INFO("SSE: Starting connection to %s", t->server_url);

    /* Initialize synchronization */
    pthread_mutex_init(&sse->mutex, NULL);

    /* Start background thread */
    sse->sse_running = 1;
    sse->sse_connected = 0;

    if (pthread_create(&sse->sse_thread, NULL, sse_thread_func, sse) != 0) {
        mcp_transport_set_error(t, "Failed to create SSE thread");
        sse->sse_running = 0;
        return AGENTC_ERR_MEMORY;
    }

    /* Wait for endpoint (polling) */
    uint32_t elapsed = 0;
    uint32_t poll_interval = 50;  /* 50ms */

    while (sse->sse_connected == 0 && elapsed < t->timeout_ms) {
        usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }

    if (sse->sse_connected == 0) {
        mcp_transport_set_error(t, "Timeout waiting for SSE endpoint");
        sse->sse_running = 0;
        pthread_join(sse->sse_thread, NULL);
        return AGENTC_ERR_TIMEOUT;
    }

    if (sse->sse_connected < 0) {
        mcp_transport_set_error(t, "%s", sse->sse_error);
        sse->sse_running = 0;
        pthread_join(sse->sse_thread, NULL);
        return AGENTC_ERR_HTTP;
    }

    t->connected = 1;
    AC_LOG_INFO("SSE: Connected, endpoint = %s", sse->endpoint);

    return AGENTC_OK;
}

static agentc_err_t sse_request(
    mcp_transport_t *t,
    const char *request_json,
    int request_id,
    char **response_json
) {
    mcp_sse_transport_t *sse = (mcp_sse_transport_t *)t;

    if (!t->connected || !sse->endpoint) {
        mcp_transport_set_error(t, "Not connected");
        return AGENTC_ERR_NOT_CONNECTED;
    }

    /* Build full endpoint URL */
    char *full_url;
    if (sse->endpoint[0] == '/') {
        size_t len = strlen(sse->base_url) + strlen(sse->endpoint) + 1;
        full_url = (char *)malloc(len);
        if (!full_url) return AGENTC_ERR_MEMORY;
        snprintf(full_url, len, "%s%s", sse->base_url, sse->endpoint);
    } else {
        full_url = strdup(sse->endpoint);
        if (!full_url) return AGENTC_ERR_MEMORY;
    }

    AC_LOG_DEBUG("SSE POST: %s (id=%d)", full_url, request_id);

    /* Build headers */
    agentc_http_header_t *headers = mcp_build_headers(t, "application/json", "text/event-stream");

    /* POST request */
    agentc_http_request_t req = {
        .url = full_url,
        .method = AGENTC_HTTP_POST,
        .headers = headers,
        .body = request_json,
        .body_len = strlen(request_json),
        .timeout_ms = t->timeout_ms,
        .verify_ssl = t->verify_ssl
    };

    agentc_http_response_t resp = {0};
    agentc_err_t err = agentc_http_request(sse->post_http, &req, &resp);

    agentc_http_header_free(headers);
    free(full_url);

    if (err != AGENTC_OK) {
        mcp_transport_set_error(t, "POST failed: %s",
                                 resp.error_msg ? resp.error_msg : ac_strerror(err));
        agentc_http_response_free(&resp);
        return err;
    }

    AC_LOG_DEBUG("SSE POST response: status=%d", resp.status_code);

    /* Some servers return response directly in POST body */
    if (resp.body && resp.body_len > 0) {
        cJSON *json = cJSON_Parse(resp.body);
        if (json) {
            cJSON *jsonrpc = cJSON_GetObjectItem(json, "jsonrpc");
            if (jsonrpc) {
                AC_LOG_DEBUG("SSE: Got direct JSON response in POST body");
                *response_json = strdup(resp.body);
                cJSON_Delete(json);
                agentc_http_response_free(&resp);
                return *response_json ? AGENTC_OK : AGENTC_ERR_MEMORY;
            }
            cJSON_Delete(json);
        }
    }

    agentc_http_response_free(&resp);

    /* For notifications (request_id == 0), no response is expected */
    if (request_id == 0) {
        AC_LOG_DEBUG("SSE: Notification sent (no response expected)");
        *response_json = NULL;
        return AGENTC_OK;
    }

    /* Wait for response via SSE stream (polling) */
    AC_LOG_DEBUG("SSE: Waiting for response id=%d via SSE stream...", request_id);

    uint32_t elapsed = 0;
    uint32_t poll_interval = 50;  /* 50ms */

    while (elapsed < t->timeout_ms) {
        /* Check for matching response */
        pthread_mutex_lock(&sse->mutex);
        for (int i = 0; i < sse->response_count; i++) {
            if (sse->responses[i].id == request_id) {
                *response_json = sse->responses[i].json;
                sse->responses[i].json = NULL;

                /* Remove from queue */
                for (int j = i; j < sse->response_count - 1; j++) {
                    sse->responses[j] = sse->responses[j + 1];
                }
                sse->response_count--;

                pthread_mutex_unlock(&sse->mutex);
                AC_LOG_DEBUG("SSE: Got response id=%d", request_id);
                return AGENTC_OK;
            }
        }
        pthread_mutex_unlock(&sse->mutex);

        /* Check if SSE thread died */
        if (!sse->sse_running || sse->sse_connected < 0) {
            mcp_transport_set_error(t, "SSE connection lost");
            return AGENTC_ERR_NOT_CONNECTED;
        }

        usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }

    mcp_transport_set_error(t, "Timeout waiting for response id=%d", request_id);
    return AGENTC_ERR_TIMEOUT;
}

static void sse_disconnect(mcp_transport_t *t) {
    mcp_sse_transport_t *sse = (mcp_sse_transport_t *)t;

    if (sse->sse_running) {
        sse->sse_running = 0;
        pthread_join(sse->sse_thread, NULL);
    }

    /* Cleanup responses */
    pthread_mutex_lock(&sse->mutex);
    for (int i = 0; i < sse->response_count; i++) {
        if (sse->responses[i].json) {
            free(sse->responses[i].json);
        }
    }
    sse->response_count = 0;
    pthread_mutex_unlock(&sse->mutex);

    pthread_mutex_destroy(&sse->mutex);

    t->connected = 0;
    AC_LOG_DEBUG("SSE transport: disconnected");
}

static void sse_destroy(mcp_transport_t *t) {
    mcp_sse_transport_t *sse = (mcp_sse_transport_t *)t;

    if (sse->post_http) {
        agentc_http_client_destroy(sse->post_http);
        sse->post_http = NULL;
    }

    if (sse->endpoint) {
        free(sse->endpoint);
        sse->endpoint = NULL;
    }

    AC_LOG_DEBUG("SSE transport: destroyed");
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const mcp_transport_ops_t sse_ops = {
    .connect = sse_connect,
    .request = sse_request,
    .disconnect = sse_disconnect,
    .destroy = sse_destroy
};

/*============================================================================
 * Constructor
 *============================================================================*/

mcp_transport_t *mcp_sse_create(
    arena_t *arena,
    agentc_http_client_t *http,
    const ac_mcp_config_t *config
) {
    mcp_sse_transport_t *t = (mcp_sse_transport_t *)arena_alloc(
        arena, sizeof(mcp_sse_transport_t)
    );
    if (!t) return NULL;

    memset(t, 0, sizeof(*t));

    /* Initialize base */
    t->base.ops = &sse_ops;
    t->base.http = http;
    t->base.arena = arena;
    t->base.server_url = arena_strdup(arena, config->server_url);
    t->base.api_key = config->api_key ? arena_strdup(arena, config->api_key) : NULL;
    t->base.timeout_ms = config->timeout_ms ? config->timeout_ms : MCP_DEFAULT_TIMEOUT_MS;
    t->base.verify_ssl = config->verify_ssl;

    /* Extract base URL */
    t->base_url = extract_base_url(arena, config->server_url);

    /* Create separate HTTP client for POST requests */
    agentc_http_client_config_t http_cfg = {
        .default_timeout_ms = t->base.timeout_ms
    };
    if (agentc_http_client_create(&http_cfg, &t->post_http) != AGENTC_OK) {
        AC_LOG_ERROR("Failed to create POST HTTP client");
        return NULL;
    }

    if (!t->base.server_url || !t->base_url) {
        if (t->post_http) agentc_http_client_destroy(t->post_http);
        return NULL;
    }

    AC_LOG_DEBUG("SSE transport created for: %s (base: %s)",
                 config->server_url, t->base_url);

    return &t->base;
}
