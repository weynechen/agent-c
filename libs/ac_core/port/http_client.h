/**
 * @file http_client.h
 * @brief ArC HTTP Client Platform Abstraction Layer
 *
 * This header defines a platform-agnostic HTTP client interface.
 * Platform-specific implementations:
 * - POSIX: libcurl (port/posix/http/http_curl.c)
 * - Windows: WinHTTP (port/windows/http/http_winhttp.c)
 * - FreeRTOS: mongoose+mbedTLS (port/freertos/http/http_mongoose.c)
 *
 * Used by:
 * - LLM providers (openai.c, anthropic.c)
 * - MCP client (ac_hosted/mcp.c)
 *
 * NOTE: This is an internal port layer header, not part of public API.
 */

#ifndef ARC_HTTP_CLIENT_H
#define ARC_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "arc/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * HTTP Method
 *============================================================================*/

typedef enum {
    ARC_HTTP_GET,
    ARC_HTTP_POST,
    ARC_HTTP_PUT,
    ARC_HTTP_DELETE,
    ARC_HTTP_PATCH,
} arc_http_method_t;

/*============================================================================
 * HTTP Headers (linked list)
 *============================================================================*/

typedef struct arc_http_header {
    const char *name;
    const char *value;
    struct arc_http_header *next;
} arc_http_header_t;

/*============================================================================
 * HTTP Request Configuration
 *============================================================================*/

typedef struct {
    const char *url;                    /* Full URL (https://api.openai.com/v1/...) */
    arc_http_method_t method;        /* HTTP method */
    arc_http_header_t *headers;      /* Request headers (linked list) */
    const char *body;                   /* Request body (NULL for GET) */
    size_t body_len;                    /* Body length (0 = strlen if body is string) */
    uint32_t timeout_ms;                /* Request timeout in milliseconds */
    int verify_ssl;                     /* 1 = verify SSL cert, 0 = skip (dev only) */
} arc_http_request_t;

/*============================================================================
 * HTTP Response
 *============================================================================*/

typedef struct {
    int status_code;                    /* HTTP status code (200, 404, etc.) */
    arc_http_header_t *headers;      /* Response headers */
    char *body;                         /* Response body (caller must free) */
    size_t body_len;                    /* Body length */
    char *error_msg;                    /* Error message if failed (caller must free) */
} arc_http_response_t;

/*============================================================================
 * Streaming Callback (for SSE / chunked responses)
 *
 * Called for each chunk of data received.
 * Return 0 to continue, non-zero to abort.
 *============================================================================*/

typedef int (*arc_stream_callback_t)(
    const char *data,                   /* Chunk data */
    size_t len,                         /* Chunk length */
    void *user_data                     /* User context */
);

/*============================================================================
 * Streaming Request Configuration
 *============================================================================*/

typedef struct {
    arc_http_request_t base;         /* Base request config */
    arc_stream_callback_t on_data;   /* Callback for each chunk */
    void *user_data;                    /* User context passed to callback */
} arc_http_stream_request_t;

/*============================================================================
 * Client Handle (opaque)
 *============================================================================*/

typedef struct arc_http_client arc_http_client_t;

/*============================================================================
 * Client Configuration
 *============================================================================*/

typedef struct {
    const char *ca_cert_path;           /* Path to CA certificate file (optional) */
    const char *ca_cert_data;           /* CA cert data in PEM format (optional) */
    size_t ca_cert_len;                 /* CA cert data length */
    uint32_t default_timeout_ms;        /* Default timeout (0 = 30000) */
    size_t max_response_size;           /* Max response body size (0 = 10MB) */
} arc_http_client_config_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Create an HTTP client instance
 *
 * @param config  Client configuration (NULL for defaults)
 * @param out     Output client handle
 * @return ARC_OK on success, error code otherwise
 */
arc_err_t arc_http_client_create(
    const arc_http_client_config_t *config,
    arc_http_client_t **out
);

/**
 * @brief Destroy an HTTP client instance
 *
 * @param client  Client handle
 */
void arc_http_client_destroy(arc_http_client_t *client);

/**
 * @brief Perform a synchronous HTTP request
 *
 * Blocks until response is received or timeout.
 *
 * @param client    Client handle
 * @param request   Request configuration
 * @param response  Output response (caller must free with arc_http_response_free)
 * @return ARC_OK on success, error code otherwise
 */
arc_err_t arc_http_request(
    arc_http_client_t *client,
    const arc_http_request_t *request,
    arc_http_response_t *response
);

/**
 * @brief Perform a streaming HTTP request
 *
 * For SSE (Server-Sent Events) or chunked transfer.
 * Callback is invoked for each chunk received.
 *
 * @param client    Client handle
 * @param request   Streaming request configuration
 * @param response  Output response (headers + final status, body may be empty)
 * @return ARC_OK on success, error code otherwise
 */
arc_err_t arc_http_request_stream(
    arc_http_client_t *client,
    const arc_http_stream_request_t *request,
    arc_http_response_t *response
);

/**
 * @brief Free response resources
 *
 * @param response  Response to free
 */
void arc_http_response_free(arc_http_response_t *response);

/*============================================================================
 * Header Helper Functions
 *============================================================================*/

/**
 * @brief Create a header node
 *
 * @param name   Header name
 * @param value  Header value
 * @return New header node (caller must free), NULL on error
 */
arc_http_header_t *arc_http_header_create(const char *name, const char *value);

/**
 * @brief Append header to list
 *
 * @param list   Pointer to header list head
 * @param header Header to append
 */
void arc_http_header_append(arc_http_header_t **list, arc_http_header_t *header);

/**
 * @brief Find header by name (case-insensitive)
 *
 * @param list  Header list
 * @param name  Header name to find
 * @return Header node or NULL if not found
 */
const arc_http_header_t *arc_http_header_find(
    const arc_http_header_t *list,
    const char *name
);

/**
 * @brief Free header list
 *
 * @param list  Header list to free
 */
void arc_http_header_free(arc_http_header_t *list);

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/* Quick JSON POST request setup */
#define ARC_HTTP_JSON_HEADERS(auth_token) \
    &(arc_http_header_t){ \
        .name = "Content-Type", \
        .value = "application/json", \
        .next = &(arc_http_header_t){ \
            .name = "Authorization", \
            .value = "Bearer " auth_token, \
            .next = NULL \
        } \
    }

#ifdef __cplusplus
}
#endif

#endif /* ARC_HTTP_CLIENT_H */
