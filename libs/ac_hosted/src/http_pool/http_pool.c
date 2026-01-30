/**
 * @file http_pool.c
 * @brief HTTP Connection Pool Implementation
 *
 * Provides a thread-safe global HTTP connection pool for hosted platforms.
 * Uses pthread for synchronization and condition variables for waiting.
 */

#include "arc/http_pool.h"
#include "arc/log.h"
#include "arc/platform.h"
#include "http_client.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/*============================================================================
 * Default Configuration
 *============================================================================*/

#define HTTP_POOL_DEFAULT_MAX_CONNECTIONS       16
#define HTTP_POOL_DEFAULT_IDLE_TIMEOUT_MS       60000
#define HTTP_POOL_DEFAULT_ACQUIRE_TIMEOUT_MS    5000
#define HTTP_POOL_DEFAULT_REQUEST_TIMEOUT_MS    30000
#define HTTP_POOL_SHUTDOWN_TIMEOUT_MS           10000

/*============================================================================
 * Pool Entry
 *============================================================================*/

typedef struct pool_entry {
    arc_http_client_t *client;  /**< HTTP client handle */
    uint64_t last_used_ms;         /**< Last use timestamp (for idle timeout) */
    int in_use;                    /**< Currently borrowed */
    struct pool_entry *next;       /**< Next in linked list */
} pool_entry_t;

/*============================================================================
 * Global Pool State
 *============================================================================*/

typedef struct {
    /* Configuration */
    ac_http_pool_config_t config;

    /* Connection storage */
    pool_entry_t *entries;         /**< Head of entries list */
    size_t total_count;            /**< Total entries */
    size_t active_count;           /**< In-use entries */

    /* Synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t available;      /**< Signal when connection returned */
    size_t waiting_count;          /**< Threads waiting for connection */

    /* Statistics */
    uint64_t total_acquires;
    uint64_t pool_hits;
    uint64_t pool_misses;
    uint64_t timeouts;

    /* State */
    int initialized;
    int shutting_down;
} http_pool_t;

static http_pool_t s_pool = {0};

/*============================================================================
 * Time Helpers
 *============================================================================*/

static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void timespec_from_timeout(struct timespec *ts, uint32_t timeout_ms) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    uint64_t ns = (uint64_t)now.tv_nsec + (uint64_t)timeout_ms * 1000000;
    ts->tv_sec = now.tv_sec + ns / 1000000000;
    ts->tv_nsec = ns % 1000000000;
}

/*============================================================================
 * Pool Entry Management
 *============================================================================*/

static pool_entry_t *entry_create(void) {
    pool_entry_t *entry = ARC_CALLOC(1, sizeof(pool_entry_t));
    if (!entry) {
        return NULL;
    }

    /* Create HTTP client with default config */
    arc_http_client_config_t http_cfg = {
        .default_timeout_ms = s_pool.config.default_request_timeout_ms,
    };

    arc_err_t err = arc_http_client_create(&http_cfg, &entry->client);
    if (err != ARC_OK || !entry->client) {
        AC_LOG_ERROR("HTTP pool: failed to create client: %s", ac_strerror(err));
        ARC_FREE(entry);
        return NULL;
    }

    entry->last_used_ms = get_current_time_ms();
    entry->in_use = 0;
    entry->next = NULL;

    return entry;
}

static void entry_destroy(pool_entry_t *entry) {
    if (!entry) return;

    if (entry->client) {
        arc_http_client_destroy(entry->client);
    }
    ARC_FREE(entry);
}

/**
 * @brief Find an available (idle) entry
 */
static pool_entry_t *find_idle_entry(void) {
    for (pool_entry_t *e = s_pool.entries; e; e = e->next) {
        if (!e->in_use) {
            return e;
        }
    }
    return NULL;
}

/**
 * @brief Find entry by client pointer
 */
static pool_entry_t *find_entry_by_client(arc_http_client_t *client) {
    for (pool_entry_t *e = s_pool.entries; e; e = e->next) {
        if (e->client == client) {
            return e;
        }
    }
    return NULL;
}

/**
 * @brief Clean up idle connections that have timed out
 */
static void cleanup_idle_connections(void) {
    if (s_pool.config.idle_timeout_ms == 0) {
        return;  /* No idle timeout */
    }

    uint64_t now = get_current_time_ms();
    uint64_t cutoff = now - s_pool.config.idle_timeout_ms;

    pool_entry_t **pp = &s_pool.entries;
    while (*pp) {
        pool_entry_t *e = *pp;

        /* Remove if idle and timed out (keep at least one connection) */
        if (!e->in_use && e->last_used_ms < cutoff && s_pool.total_count > 1) {
            *pp = e->next;
            entry_destroy(e);
            s_pool.total_count--;
            AC_LOG_DEBUG("HTTP pool: removed idle connection (total=%zu)", s_pool.total_count);
        } else {
            pp = &e->next;
        }
    }
}

/*============================================================================
 * Public API: Lifecycle
 *============================================================================*/

arc_err_t ac_http_pool_init(const ac_http_pool_config_t *config) {
    /* Thread-safe initialization check */
    static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&init_mutex);

    if (s_pool.initialized) {
        pthread_mutex_unlock(&init_mutex);
        AC_LOG_DEBUG("HTTP pool: already initialized");
        return ARC_OK;
    }

    memset(&s_pool, 0, sizeof(s_pool));

    /* Apply configuration */
    if (config) {
        s_pool.config = *config;
    }

    /* Set defaults */
    if (s_pool.config.max_connections == 0) {
        s_pool.config.max_connections = HTTP_POOL_DEFAULT_MAX_CONNECTIONS;
    }
    if (s_pool.config.idle_timeout_ms == 0) {
        s_pool.config.idle_timeout_ms = HTTP_POOL_DEFAULT_IDLE_TIMEOUT_MS;
    }
    if (s_pool.config.acquire_timeout_ms == 0) {
        s_pool.config.acquire_timeout_ms = HTTP_POOL_DEFAULT_ACQUIRE_TIMEOUT_MS;
    }
    if (s_pool.config.default_request_timeout_ms == 0) {
        s_pool.config.default_request_timeout_ms = HTTP_POOL_DEFAULT_REQUEST_TIMEOUT_MS;
    }

    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&s_pool.mutex, NULL) != 0) {
        pthread_mutex_unlock(&init_mutex);
        return ARC_ERR_BACKEND;
    }

    if (pthread_cond_init(&s_pool.available, NULL) != 0) {
        pthread_mutex_destroy(&s_pool.mutex);
        pthread_mutex_unlock(&init_mutex);
        return ARC_ERR_BACKEND;
    }

    s_pool.initialized = 1;
    s_pool.shutting_down = 0;

    pthread_mutex_unlock(&init_mutex);

    AC_LOG_INFO("HTTP pool initialized: max_connections=%zu, idle_timeout=%ums, acquire_timeout=%ums",
                s_pool.config.max_connections,
                s_pool.config.idle_timeout_ms,
                s_pool.config.acquire_timeout_ms);

    return ARC_OK;
}

int ac_http_pool_is_initialized(void) {
    return s_pool.initialized && !s_pool.shutting_down;
}

void ac_http_pool_shutdown(void) {
    if (!s_pool.initialized) {
        return;
    }

    AC_LOG_INFO("HTTP pool shutting down...");

    pthread_mutex_lock(&s_pool.mutex);
    s_pool.shutting_down = 1;

    /* Wake up all waiting threads */
    pthread_cond_broadcast(&s_pool.available);

    /* Wait for active connections to be returned (with timeout) */
    if (s_pool.active_count > 0) {
        struct timespec timeout;
        timespec_from_timeout(&timeout, HTTP_POOL_SHUTDOWN_TIMEOUT_MS);

        while (s_pool.active_count > 0) {
            int ret = pthread_cond_timedwait(&s_pool.available, &s_pool.mutex, &timeout);
            if (ret == ETIMEDOUT) {
                AC_LOG_WARN("HTTP pool: shutdown timeout, %zu connections still active",
                            s_pool.active_count);
                break;
            }
        }
    }

    /* Destroy all entries */
    pool_entry_t *e = s_pool.entries;
    while (e) {
        pool_entry_t *next = e->next;
        entry_destroy(e);
        e = next;
    }

    s_pool.entries = NULL;
    s_pool.total_count = 0;
    s_pool.active_count = 0;

    pthread_mutex_unlock(&s_pool.mutex);

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&s_pool.mutex);
    pthread_cond_destroy(&s_pool.available);

    AC_LOG_INFO("HTTP pool shutdown complete (acquires=%llu, hits=%llu, misses=%llu, timeouts=%llu)",
                (unsigned long long)s_pool.total_acquires,
                (unsigned long long)s_pool.pool_hits,
                (unsigned long long)s_pool.pool_misses,
                (unsigned long long)s_pool.timeouts);

    s_pool.initialized = 0;
}

/*============================================================================
 * Public API: Acquire/Release
 *============================================================================*/

arc_http_client_t *ac_http_pool_acquire(uint32_t timeout_ms) {
    if (!s_pool.initialized || s_pool.shutting_down) {
        AC_LOG_ERROR("HTTP pool: not initialized or shutting down");
        return NULL;
    }

    if (timeout_ms == 0) {
        timeout_ms = s_pool.config.acquire_timeout_ms;
    }

    pthread_mutex_lock(&s_pool.mutex);

    s_pool.total_acquires++;

    /* Periodic cleanup of idle connections */
    cleanup_idle_connections();

    /* Try to find an idle connection */
    pool_entry_t *entry = find_idle_entry();

    if (entry) {
        /* Pool hit: reuse existing connection */
        entry->in_use = 1;
        entry->last_used_ms = get_current_time_ms();
        s_pool.active_count++;
        s_pool.pool_hits++;

        pthread_mutex_unlock(&s_pool.mutex);

        AC_LOG_DEBUG("HTTP pool: acquired (hit, active=%zu, total=%zu)",
                     s_pool.active_count, s_pool.total_count);
        return entry->client;
    }

    /* No idle connection available */

    /* Can we create a new one? */
    if (s_pool.total_count < s_pool.config.max_connections) {
        /* Pool miss: create new connection */
        entry = entry_create();
        if (entry) {
            entry->in_use = 1;
            entry->next = s_pool.entries;
            s_pool.entries = entry;
            s_pool.total_count++;
            s_pool.active_count++;
            s_pool.pool_misses++;

            pthread_mutex_unlock(&s_pool.mutex);

            AC_LOG_DEBUG("HTTP pool: acquired (new, active=%zu, total=%zu)",
                         s_pool.active_count, s_pool.total_count);
            return entry->client;
        }
        /* Failed to create, fall through to wait */
    }

    /* Pool is full, wait for a connection to be released */
    struct timespec deadline;
    timespec_from_timeout(&deadline, timeout_ms);

    s_pool.waiting_count++;

    while (!s_pool.shutting_down) {
        entry = find_idle_entry();
        if (entry) {
            entry->in_use = 1;
            entry->last_used_ms = get_current_time_ms();
            s_pool.active_count++;
            s_pool.waiting_count--;
            s_pool.pool_hits++;

            pthread_mutex_unlock(&s_pool.mutex);

            AC_LOG_DEBUG("HTTP pool: acquired (waited, active=%zu, total=%zu)",
                         s_pool.active_count, s_pool.total_count);
            return entry->client;
        }

        int ret = pthread_cond_timedwait(&s_pool.available, &s_pool.mutex, &deadline);
        if (ret == ETIMEDOUT) {
            s_pool.waiting_count--;
            s_pool.timeouts++;

            pthread_mutex_unlock(&s_pool.mutex);

            AC_LOG_WARN("HTTP pool: acquire timeout (%ums)", timeout_ms);
            return NULL;
        }
    }

    /* Shutting down */
    s_pool.waiting_count--;
    pthread_mutex_unlock(&s_pool.mutex);

    return NULL;
}

void ac_http_pool_release(arc_http_client_t *client) {
    if (!client) {
        return;
    }

    if (!s_pool.initialized) {
        /* Pool was shutdown, destroy the orphaned client */
        AC_LOG_WARN("HTTP pool: releasing client after shutdown");
        arc_http_client_destroy(client);
        return;
    }

    pthread_mutex_lock(&s_pool.mutex);

    pool_entry_t *entry = find_entry_by_client(client);
    if (!entry) {
        pthread_mutex_unlock(&s_pool.mutex);
        AC_LOG_WARN("HTTP pool: releasing unknown client");
        return;
    }

    if (!entry->in_use) {
        pthread_mutex_unlock(&s_pool.mutex);
        AC_LOG_WARN("HTTP pool: double release detected");
        return;
    }

    entry->in_use = 0;
    entry->last_used_ms = get_current_time_ms();
    s_pool.active_count--;

    /* Signal waiting threads */
    pthread_cond_signal(&s_pool.available);

    pthread_mutex_unlock(&s_pool.mutex);

    AC_LOG_DEBUG("HTTP pool: released (active=%zu, total=%zu)",
                 s_pool.active_count, s_pool.total_count);
}

/*============================================================================
 * Public API: Statistics
 *============================================================================*/

arc_err_t ac_http_pool_get_stats(ac_http_pool_stats_t *stats) {
    if (!stats) {
        return ARC_ERR_INVALID_ARG;
    }

    if (!s_pool.initialized) {
        memset(stats, 0, sizeof(*stats));
        return ARC_ERR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&s_pool.mutex);

    stats->max_connections = s_pool.config.max_connections;
    stats->total_connections = s_pool.total_count;
    stats->active_connections = s_pool.active_count;
    stats->idle_connections = s_pool.total_count - s_pool.active_count;
    stats->waiting_requests = s_pool.waiting_count;
    stats->total_acquires = s_pool.total_acquires;
    stats->pool_hits = s_pool.pool_hits;
    stats->pool_misses = s_pool.pool_misses;
    stats->timeouts = s_pool.timeouts;

    pthread_mutex_unlock(&s_pool.mutex);

    return ARC_OK;
}
