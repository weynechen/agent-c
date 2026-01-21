/**
 * @file http_client.h
 * @brief AgentC HTTP Client Platform Abstraction Layer
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

#ifndef AGENTC_HTTP_CLIENT_H
#define AGENTC_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "agentc/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * HTTP Method
 *============================================================================*/

typedef enum {
    AGENTC_HTTP_GET,
    AGENTC_HTTP_POST,
    AGENTC_HTTP_PUT,
    AGENTC_HTTP_DELETE,
    AGENTC_HTTP_PATCH,
} agentc_http_method_t;

/*============================================================================
 * HTTP Headers (linked list)
 *============================================================================*/

typedef struct agentc_http_header {
    const char *name;
    const char *value;
    struct agentc_http_header *next;
} agentc_http_header_t;

/*============================================================================
 * HTTP Request Configuration
 *============================================================================*/

typedef struct {
    const char *url;                    /* Full URL (https://api.openai.com/v1/...) */
    agentc_http_method_t method;        /* HTTP method */
    agentc_http_header_t *headers;      /* Request headers (linked list) */
    const char *body;                   /* Request body (NULL for GET) */
    size_t body_len;                    /* Body length (0 = strlen if body is string) */
    uint32_t timeout_ms;                /* Request timeout in milliseconds */
    int verify_ssl;                     /* 1 = verify SSL cert, 0 = skip (dev only) */
} agentc_http_request_t;

/*============================================================================
 * HTTP Response
 *============================================================================*/

typedef struct {
    int status_code;                    /* HTTP status code (200, 404, etc.) */
    agentc_http_header_t *headers;      /* Response headers */
    char *body;                         /* Response body (caller must free) */
    size_t body_len;                    /* Body length */
    char *error_msg;                    /* Error message if failed (caller must free) */
} agentc_http_response_t;

/*============================================================================
 * Streaming Callback (for SSE / chunked responses)
 *
 * Called for each chunk of data received.
 * Return 0 to continue, non-zero to abort.
 *============================================================================*/

typedef int (*agentc_stream_callback_t)(
    const char *data,                   /* Chunk data */
    size_t len,                         /* Chunk length */
    void *user_data                     /* User context */
);

/*============================================================================
 * Streaming Request Configuration
 *============================================================================*/

typedef struct {
    agentc_http_request_t base;         /* Base request config */
    agentc_stream_callback_t on_data;   /* Callback for each chunk */
    void *user_data;                    /* User context passed to callback */
} agentc_http_stream_request_t;

/*============================================================================
 * Client Handle (opaque)
 *============================================================================*/

typedef struct agentc_http_client agentc_http_client_t;

/*============================================================================
 * Client Configuration
 *============================================================================*/

typedef struct {
    const char *ca_cert_path;           /* Path to CA certificate file (optional) */
    const char *ca_cert_data;           /* CA cert data in PEM format (optional) */
    size_t ca_cert_len;                 /* CA cert data length */
    uint32_t default_timeout_ms;        /* Default timeout (0 = 30000) */
    size_t max_response_size;           /* Max response body size (0 = 10MB) */
} agentc_http_client_config_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Initialize the HTTP client subsystem
 *
 * Must be called once before any other HTTP functions.
 * Thread-safe: No (call from main thread only)
 *
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_http_init(void);

/**
 * @brief Cleanup the HTTP client subsystem
 *
 * Call before program exit.
 */
void agentc_http_cleanup(void);

/**
 * @brief Create an HTTP client instance
 *
 * @param config  Client configuration (NULL for defaults)
 * @param out     Output client handle
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_http_client_create(
    const agentc_http_client_config_t *config,
    agentc_http_client_t **out
);

/**
 * @brief Destroy an HTTP client instance
 *
 * @param client  Client handle
 */
void agentc_http_client_destroy(agentc_http_client_t *client);

/**
 * @brief Perform a synchronous HTTP request
 *
 * Blocks until response is received or timeout.
 *
 * @param client    Client handle
 * @param request   Request configuration
 * @param response  Output response (caller must free with agentc_http_response_free)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_http_request(
    agentc_http_client_t *client,
    const agentc_http_request_t *request,
    agentc_http_response_t *response
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
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_http_request_stream(
    agentc_http_client_t *client,
    const agentc_http_stream_request_t *request,
    agentc_http_response_t *response
);

/**
 * @brief Free response resources
 *
 * @param response  Response to free
 */
void agentc_http_response_free(agentc_http_response_t *response);

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
agentc_http_header_t *agentc_http_header_create(const char *name, const char *value);

/**
 * @brief Append header to list
 *
 * @param list   Pointer to header list head
 * @param header Header to append
 */
void agentc_http_header_append(agentc_http_header_t **list, agentc_http_header_t *header);

/**
 * @brief Find header by name (case-insensitive)
 *
 * @param list  Header list
 * @param name  Header name to find
 * @return Header node or NULL if not found
 */
const agentc_http_header_t *agentc_http_header_find(
    const agentc_http_header_t *list,
    const char *name
);

/**
 * @brief Free header list
 *
 * @param list  Header list to free
 */
void agentc_http_header_free(agentc_http_header_t *list);

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/* Quick JSON POST request setup */
#define AGENTC_HTTP_JSON_HEADERS(auth_token) \
    &(agentc_http_header_t){ \
        .name = "Content-Type", \
        .value = "application/json", \
        .next = &(agentc_http_header_t){ \
            .name = "Authorization", \
            .value = "Bearer " auth_token, \
            .next = NULL \
        } \
    }

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_HTTP_CLIENT_H */
