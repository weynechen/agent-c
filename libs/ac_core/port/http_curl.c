/**
 * @file http_curl.c
 * @brief libcurl HTTP backend (cross-platform)
 *
 * This is the hosted platform implementation using libcurl.
 * Implements the interface defined in port/http_client.h.
 */

#include "arc/platform.h"
#include "http_client.h"
#include "arc/log.h"
#include <curl/curl.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>

/*============================================================================
 * Internal Structures
 *============================================================================*/

struct arc_http_client {
    CURL *curl;
    arc_http_client_config_t config;
};

typedef struct {
    char *data;
    size_t size;
    size_t cap;
    size_t max_response_size;  /* 0 = unlimited */
    int size_exceeded;         /* Set to 1 if response size limit exceeded */
} write_buffer_t;

typedef struct {
    arc_stream_callback_t callback;
    void *user_data;
    int aborted;
} stream_context_t;

/*============================================================================
 * CURL Callbacks
 *============================================================================*/

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    write_buffer_t *buf = (write_buffer_t *)userp;

    /* Check response size limit */
    if (buf->max_response_size > 0 && buf->size + realsize > buf->max_response_size) {
        AC_LOG_ERROR("Response size exceeds limit: %zu > %zu",
            buf->size + realsize, buf->max_response_size);
        buf->size_exceeded = 1;
        return 0;  /* Abort transfer */
    }

    /* Grow buffer if needed */
    if (buf->size + realsize + 1 > buf->cap) {
        size_t new_cap = buf->cap * 2;
        if (new_cap < buf->size + realsize + 1) {
            new_cap = buf->size + realsize + 1;
        }
        char *new_data = ARC_REALLOC(buf->data, new_cap);
        if (!new_data) {
            return 0;  /* Out of memory */
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

static size_t stream_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    stream_context_t *ctx = (stream_context_t *)userp;

    if (ctx->aborted) {
        return 0;  /* Abort transfer */
    }

    if (ctx->callback) {
        int ret = ctx->callback((const char *)contents, realsize, ctx->user_data);
        if (ret != 0) {
            ctx->aborted = 1;
            return 0;
        }
    }

    return realsize;
}

static int s_curl_refcount = 0;
static pthread_mutex_t s_curl_mutex = PTHREAD_MUTEX_INITIALIZER;

static arc_err_t curl_global_init_once(void) {
    pthread_mutex_lock(&s_curl_mutex);
    if (s_curl_refcount == 0) {
        CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (res != CURLE_OK) {
            AC_LOG_ERROR("curl_global_init failed: %s", curl_easy_strerror(res));
            return ARC_ERR_BACKEND;
        }
        AC_LOG_DEBUG("CURL backend initialized");
    }
    s_curl_refcount++;
    AC_LOG_DEBUG("CURL refcount: %d", s_curl_refcount);
    pthread_mutex_unlock(&s_curl_mutex);
    return ARC_OK;
}

static void curl_global_cleanup_once(void) {
    pthread_mutex_lock(&s_curl_mutex);
    if (s_curl_refcount > 0) {
        s_curl_refcount--;
        AC_LOG_DEBUG("CURL refcount: %d", s_curl_refcount);
        if (s_curl_refcount == 0) {
            curl_global_cleanup();
            AC_LOG_DEBUG("CURL backend cleaned up");
        }
    }
    pthread_mutex_unlock(&s_curl_mutex);
}


/*============================================================================
 * Client Create/Destroy
 *============================================================================*/

arc_err_t arc_http_client_create(
    const arc_http_client_config_t *config,
    arc_http_client_t **out
) {
    if (!out) {
        return ARC_ERR_INVALID_ARG;
    }

    /* Initialize curl globally if this is the first client */
    arc_err_t err = curl_global_init_once();
    if (err != ARC_OK) {
        return err;
    }

    arc_http_client_t *client = ARC_CALLOC(1, sizeof(arc_http_client_t));
    if (!client) {
        curl_global_cleanup_once();  /* Decrement refcount on failure */
        return ARC_ERR_NO_MEMORY;
    }

    client->curl = curl_easy_init();
    if (!client->curl) {
        ARC_FREE(client);
        curl_global_cleanup_once();  /* Decrement refcount on failure */
        return ARC_ERR_BACKEND;
    }

    /* Store config */
    if (config) {
        client->config = *config;
    }

    /* Set defaults */
    if (client->config.default_timeout_ms == 0) {
        client->config.default_timeout_ms = 30000;
    }
    if (client->config.max_response_size == 0) {
        client->config.max_response_size = 10 * 1024 * 1024;  /* 10MB */
    }

    *out = client;
    return ARC_OK;
}

void arc_http_client_destroy(arc_http_client_t *client) {
    if (!client) return;

    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }

    ARC_FREE(client);

    /* Cleanup curl globally if this was the last client */
    curl_global_cleanup_once();
}

/*============================================================================
 * HTTP Request
 *============================================================================*/

arc_err_t arc_http_request(
    arc_http_client_t *client,
    const arc_http_request_t *request,
    arc_http_response_t *response
) {
    if (!client || !client->curl || !request || !request->url || !response) {
        return ARC_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));

    CURL *curl = client->curl;
    curl_easy_reset(curl);

    /* Response buffer */
    write_buffer_t buf = {0};
    buf.data = ARC_MALLOC(4096);
    buf.cap = 4096;
    buf.size = 0;
    buf.max_response_size = client->config.max_response_size;
    if (!buf.data) {
        return ARC_ERR_NO_MEMORY;
    }
    buf.data[0] = '\0';

    /* Set URL */
    curl_easy_setopt(curl, CURLOPT_URL, request->url);

    /* Set method and body */
    switch (request->method) {
        case ARC_HTTP_GET:
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
        case ARC_HTTP_POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (request->body) {
                size_t body_len = request->body_len > 0 ? request->body_len : strlen(request->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
            }
            break;
        case ARC_HTTP_PUT:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (request->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
            }
            break;
        case ARC_HTTP_DELETE:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
        case ARC_HTTP_PATCH:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (request->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
            }
            break;
    }

    /* Set headers */
    struct curl_slist *headers = NULL;

    for (const arc_http_header_t *h = request->headers; h; h = h->next) {
        size_t len = strlen(h->name) + strlen(h->value) + 3; /* +3 for ": " and \0 */
        len = ((len / 1024 + 1) *1024); //keep 1024 align

        char *header_line = ARC_MALLOC(len);
        if (!header_line) {
            curl_slist_free_all(headers);
            return ARC_ERR_NO_MEMORY;
        }
        snprintf(header_line, len, "%s: %s", h->name, h->value);
        headers = curl_slist_append(headers, header_line);
        ARC_FREE(header_line);
    }

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    /* Set timeout */
    uint32_t timeout = request->timeout_ms > 0 ? request->timeout_ms : client->config.default_timeout_ms;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout);

    /* SSL options */
    if (request->verify_ssl == 0) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        if (client->config.ca_cert_path) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, client->config.ca_cert_path);
        }
    }

    /* Set callbacks */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    /* Perform request */
    AC_LOG_DEBUG("HTTP %s %s",
        request->method == ARC_HTTP_POST ? "POST" : "GET",
        request->url);

    CURLcode res = curl_easy_perform(curl);

    /* Cleanup headers */
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        const char *err_msg = curl_easy_strerror(res);
        AC_LOG_ERROR("CURL request failed: %s", err_msg);

        ARC_FREE(buf.data);

        /* Check if aborted due to response size limit */
        if (buf.size_exceeded) {
            response->error_msg = ARC_STRDUP("Response size exceeds limit");
            return ARC_ERR_RESPONSE_TOO_LARGE;
        }

        response->error_msg = ARC_STRDUP(err_msg);

        if (res == CURLE_OPERATION_TIMEDOUT) {
            return ARC_ERR_TIMEOUT;
        } else if (res == CURLE_COULDNT_RESOLVE_HOST) {
            return ARC_ERR_DNS;
        } else if (res == CURLE_SSL_CONNECT_ERROR || res == CURLE_SSL_CERTPROBLEM) {
            return ARC_ERR_TLS;
        }
        return ARC_ERR_NETWORK;
    }

    /* Get response code */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response->status_code = (int)http_code;

    /* Set response body */
    response->body = buf.data;
    response->body_len = buf.size;

    AC_LOG_DEBUG("HTTP response: %d, %zu bytes", response->status_code, response->body_len);

    return ARC_OK;
}

/*============================================================================
 * Streaming HTTP Request
 *============================================================================*/

arc_err_t arc_http_request_stream(
    arc_http_client_t *client,
    const arc_http_stream_request_t *request,
    arc_http_response_t *response
) {
    if (!client || !client->curl || !request || !request->base.url || !response) {
        return ARC_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));

    CURL *curl = client->curl;
    curl_easy_reset(curl);

    /* Stream context */
    stream_context_t ctx = {
        .callback = request->on_data,
        .user_data = request->user_data,
        .aborted = 0
    };

    /* Set URL */
    curl_easy_setopt(curl, CURLOPT_URL, request->base.url);

    /* Set method and body */
    if (request->base.method == ARC_HTTP_POST) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (request->base.body) {
            size_t body_len = request->base.body_len > 0 ?
                request->base.body_len : strlen(request->base.body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->base.body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    }

    /* Set headers */
    struct curl_slist *headers = NULL;
    for (const arc_http_header_t *h = request->base.headers; h; h = h->next) {
        char header_line[1024];
        snprintf(header_line, sizeof(header_line), "%s: %s", h->name, h->value);
        headers = curl_slist_append(headers, header_line);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    /* Set timeout */
    uint32_t timeout = request->base.timeout_ms > 0 ?
        request->base.timeout_ms : client->config.default_timeout_ms;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout);

    /* SSL */
    if (request->base.verify_ssl == 0) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    /* Streaming callback */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    /* Perform request */
    AC_LOG_DEBUG("HTTP stream POST %s", request->base.url);

    CURLcode res = curl_easy_perform(curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK && !ctx.aborted) {
        const char *err_msg = curl_easy_strerror(res);
        response->error_msg = ARC_STRDUP(err_msg);
        return ARC_ERR_NETWORK;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response->status_code = (int)http_code;

    return ARC_OK;
}
