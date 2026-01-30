/**
 * @file http_pool.h
 * @brief HTTP Connection Pool for Hosted Platforms
 *
 * Provides a global HTTP connection pool to efficiently manage connections
 * across multiple LLM providers and MCP clients. This reduces file descriptor
 * usage and enables connection reuse.
 *
 * Usage:
 * 1. Call ac_http_pool_init() at application startup
 * 2. LLM/MCP clients will automatically use pooled connections
 * 3. Call ac_http_pool_shutdown() at application exit
 *
 * This is an optional optimization for hosted platforms (Linux/Windows/macOS).
 * If not initialized, LLM/MCP clients fall back to creating their own connections.
 */

#ifndef ARC_HTTP_POOL_H
#define ARC_HTTP_POOL_H

#include "arc/error.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

/* HTTP client handle from ac_core port layer */
typedef struct arc_http_client arc_http_client_t;

/*============================================================================
 * Pool Configuration
 *============================================================================*/

/**
 * @brief HTTP connection pool configuration
 */
typedef struct {
    size_t max_connections;        /**< Max pooled connections (default: 16) */
    uint32_t idle_timeout_ms;      /**< Idle connection timeout (default: 60000) */
    uint32_t acquire_timeout_ms;   /**< Max wait time to acquire (default: 5000) */
    uint32_t default_request_timeout_ms; /**< Default request timeout (default: 30000) */
} ac_http_pool_config_t;

/*============================================================================
 * Pool Lifecycle
 *============================================================================*/

/**
 * @brief Initialize the global HTTP connection pool
 *
 * Call once at application startup, before creating any LLM/MCP clients.
 * Thread-safe: can be called from multiple threads, only first call takes effect.
 *
 * @param config  Pool configuration (NULL for defaults)
 * @return ARC_OK on success
 */
arc_err_t ac_http_pool_init(const ac_http_pool_config_t *config);

/**
 * @brief Check if the pool is initialized
 *
 * @return 1 if initialized, 0 otherwise
 */
int ac_http_pool_is_initialized(void);

/**
 * @brief Shutdown the global HTTP connection pool
 *
 * Call at application exit, after all LLM/MCP clients are destroyed.
 * Waits for all borrowed connections to be returned (with timeout).
 */
void ac_http_pool_shutdown(void);

/*============================================================================
 * Connection Acquire/Release
 *============================================================================*/

/**
 * @brief Acquire an HTTP client from the pool
 *
 * Blocks until a connection is available or timeout expires.
 * The returned client must be released with ac_http_pool_release().
 *
 * @param timeout_ms  Max wait time in milliseconds (0 = use default)
 * @return HTTP client handle, or NULL on timeout/error
 */
arc_http_client_t *ac_http_pool_acquire(uint32_t timeout_ms);

/**
 * @brief Release an HTTP client back to the pool
 *
 * The client is returned to the pool for reuse.
 * Do NOT use the client after releasing.
 *
 * @param client  HTTP client to release
 */
void ac_http_pool_release(arc_http_client_t *client);

/*============================================================================
 * Pool Statistics
 *============================================================================*/

/**
 * @brief Pool statistics
 */
typedef struct {
    size_t max_connections;        /**< Configured max connections */
    size_t total_connections;      /**< Total connections created */
    size_t active_connections;     /**< Currently in-use connections */
    size_t idle_connections;       /**< Available connections in pool */
    size_t waiting_requests;       /**< Threads waiting for connection */
    uint64_t total_acquires;       /**< Total acquire requests */
    uint64_t pool_hits;            /**< Reused existing connection */
    uint64_t pool_misses;          /**< Created new connection */
    uint64_t timeouts;             /**< Acquire timeouts */
} ac_http_pool_stats_t;

/**
 * @brief Get pool statistics
 *
 * @param stats  Output statistics structure
 * @return ARC_OK on success, ARC_ERR_NOT_INITIALIZED if pool not initialized
 */
arc_err_t ac_http_pool_get_stats(ac_http_pool_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* ARC_HTTP_POOL_H */
