/**
 * @file session.c
 * @brief Session management implementation with dynamic arrays
 *
 * Manages lifecycle of agents, tool registries, and MCP clients.
 * Uses dynamic arrays instead of fixed-size arrays to support
 * arbitrary number of resources.
 *
 * Thread Safety:
 * - All session operations are protected by a mutex
 * - Safe to call ac_session_add_* from multiple threads
 * - ac_session_close should only be called once, after all agents complete
 */

#include "arc/session.h"
#include "arc/agent.h"
#include "arc/tool.h"
#include "arc/mcp.h"
#include "arc/arena.h"
#include "arc/log.h"
#include "arc/platform.h"
#include "pthread_port.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Constants (from platform.h, can be overridden at compile time)
 *============================================================================*/

/* Use platform-specific defaults from platform.h */
#define INITIAL_CAPACITY        ARC_ARRAY_INITIAL_CAPACITY
#define GROWTH_FACTOR           ARC_ARENA_GROWTH_FACTOR
#define SESSION_ARENA_SIZE      ARC_SESSION_ARENA_SIZE

/*============================================================================
 * Dynamic Array Type
 *============================================================================*/

typedef struct {
    void **items;           /* Array of pointers */
    size_t count;           /* Current item count */
    size_t capacity;        /* Array capacity */
} dyn_array_t;

/*============================================================================
 * Session Structure
 *============================================================================*/

struct ac_session {
    arena_t *arena;                     /* Session arena for registries */

    dyn_array_t agents;                 /* Dynamic array of agents */
    dyn_array_t registries;             /* Dynamic array of tool registries */
    dyn_array_t mcp_clients;            /* Dynamic array of MCP clients */

    pthread_mutex_t lock;               /* Thread safety mutex */
    int closed;                         /* Flag to prevent double-close */
};

/*============================================================================
 * Internal API Declarations (called by mcp.c cleanup)
 *============================================================================*/

extern void ac_mcp_cleanup(ac_mcp_client_t *client);

/*============================================================================
 * Dynamic Array Operations
 *============================================================================*/

static arc_err_t dyn_array_init(dyn_array_t *arr, size_t initial_cap) {
    arr->items = (void **)ARC_CALLOC(initial_cap, sizeof(void *));
    if (!arr->items) {
        return ARC_ERR_MEMORY;
    }
    arr->count = 0;
    arr->capacity = initial_cap;
    return ARC_OK;
}

static arc_err_t dyn_array_add(dyn_array_t *arr, void *item) {
    /* Grow if needed */
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity * GROWTH_FACTOR;
        void **new_items = (void **)ARC_REALLOC(arr->items, new_cap * sizeof(void *));
        if (!new_items) {
            return ARC_ERR_MEMORY;
        }
        arr->items = new_items;
        arr->capacity = new_cap;
        AC_LOG_DEBUG("Session array grown to capacity=%zu", new_cap);
    }

    arr->items[arr->count++] = item;
    return ARC_OK;
}

static void dyn_array_free(dyn_array_t *arr) {
    if (arr->items) {
        ARC_FREE(arr->items);
        arr->items = NULL;
    }
    arr->count = 0;
    arr->capacity = 0;
}

/*============================================================================
 * Session API Implementation
 *============================================================================*/

ac_session_t *ac_session_open(void) {
    ac_session_t *session = (ac_session_t *)ARC_CALLOC(1, sizeof(ac_session_t));
    if (!session) {
        AC_LOG_ERROR("Failed to allocate session");
        return NULL;
    }

    /* Initialize mutex */
    if (pthread_mutex_init(&session->lock, NULL) != 0) {
        AC_LOG_ERROR("Failed to initialize session mutex");
        ARC_FREE(session);
        return NULL;
    }

    /* Initialize dynamic arrays */
    if (dyn_array_init(&session->agents, INITIAL_CAPACITY) != ARC_OK ||
        dyn_array_init(&session->registries, INITIAL_CAPACITY) != ARC_OK ||
        dyn_array_init(&session->mcp_clients, INITIAL_CAPACITY) != ARC_OK) {
        AC_LOG_ERROR("Failed to initialize session arrays");
        dyn_array_free(&session->agents);
        dyn_array_free(&session->registries);
        dyn_array_free(&session->mcp_clients);
        pthread_mutex_destroy(&session->lock);
        ARC_FREE(session);
        return NULL;
    }

    /* Create session arena for registries and MCP clients */
    session->arena = arena_create(SESSION_ARENA_SIZE);
    if (!session->arena) {
        AC_LOG_ERROR("Failed to create session arena");
        dyn_array_free(&session->agents);
        dyn_array_free(&session->registries);
        dyn_array_free(&session->mcp_clients);
        pthread_mutex_destroy(&session->lock);
        ARC_FREE(session);
        return NULL;
    }

    session->closed = 0;

    AC_LOG_INFO("Session opened (arena=%zuKB, initial_capacity=%d)",
                SESSION_ARENA_SIZE / 1024, INITIAL_CAPACITY);
    return session;
}

void ac_session_close(ac_session_t *session) {
    if (!session) {
        return;
    }

    pthread_mutex_lock(&session->lock);

    /* Prevent double-close */
    if (session->closed) {
        pthread_mutex_unlock(&session->lock);
        AC_LOG_WARN("Session already closed");
        return;
    }
    session->closed = 1;

    size_t agent_count = session->agents.count;
    size_t registry_count = session->registries.count;
    size_t mcp_count = session->mcp_clients.count;

    /* Cleanup MCP clients first (they may be referenced by tools) */
    for (size_t i = 0; i < session->mcp_clients.count; i++) {
        ac_mcp_client_t *client = (ac_mcp_client_t *)session->mcp_clients.items[i];
        if (client) {
            ac_mcp_cleanup(client);
        }
    }

    /* Destroy all agents (each has its own arena) */
    for (size_t i = 0; i < session->agents.count; i++) {
        ac_agent_t *agent = (ac_agent_t *)session->agents.items[i];
        if (agent) {
            ac_agent_destroy(agent);
        }
    }

    /* Tool registries are allocated from session arena,
     * they will be freed when arena is destroyed */

    /* Free dynamic arrays */
    dyn_array_free(&session->agents);
    dyn_array_free(&session->registries);
    dyn_array_free(&session->mcp_clients);

    /* Destroy session arena (frees all registries and their data) */
    if (session->arena) {
        arena_destroy(session->arena);
    }

    pthread_mutex_unlock(&session->lock);

    AC_LOG_INFO("Session closed: destroyed %zu agents, %zu registries, %zu MCP clients",
                agent_count, registry_count, mcp_count);

    pthread_mutex_destroy(&session->lock);
    ARC_FREE(session);
}

/*============================================================================
 * Internal API (used by agent.c, tool.c, mcp.c)
 *============================================================================*/

arena_t *ac_session_get_arena(ac_session_t *session) {
    return session ? session->arena : NULL;
}

arc_err_t ac_session_add_agent(ac_session_t *session, ac_agent_t *agent) {
    if (!session || !agent) {
        return ARC_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&session->lock);

    if (session->closed) {
        pthread_mutex_unlock(&session->lock);
        AC_LOG_ERROR("Cannot add agent to closed session");
        return ARC_ERR_INVALID_STATE;
    }

    arc_err_t err = dyn_array_add(&session->agents, agent);

    if (err == ARC_OK) {
        AC_LOG_DEBUG("Agent added to session (total=%zu)", session->agents.count);
    } else {
        AC_LOG_ERROR("Failed to add agent to session: out of memory");
    }

    pthread_mutex_unlock(&session->lock);
    return err;
}

arc_err_t ac_session_add_registry(ac_session_t *session, ac_tool_registry_t *registry) {
    if (!session || !registry) {
        return ARC_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&session->lock);

    if (session->closed) {
        pthread_mutex_unlock(&session->lock);
        AC_LOG_ERROR("Cannot add registry to closed session");
        return ARC_ERR_INVALID_STATE;
    }

    arc_err_t err = dyn_array_add(&session->registries, registry);

    if (err == ARC_OK) {
        AC_LOG_DEBUG("Registry added to session (total=%zu)", session->registries.count);
    } else {
        AC_LOG_ERROR("Failed to add registry to session: out of memory");
    }

    pthread_mutex_unlock(&session->lock);
    return err;
}

arc_err_t ac_session_add_mcp(ac_session_t *session, ac_mcp_client_t *client) {
    if (!session || !client) {
        return ARC_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&session->lock);

    if (session->closed) {
        pthread_mutex_unlock(&session->lock);
        AC_LOG_ERROR("Cannot add MCP client to closed session");
        return ARC_ERR_INVALID_STATE;
    }

    arc_err_t err = dyn_array_add(&session->mcp_clients, client);

    if (err == ARC_OK) {
        AC_LOG_DEBUG("MCP client added to session (total=%zu)", session->mcp_clients.count);
    } else {
        AC_LOG_ERROR("Failed to add MCP client to session: out of memory");
    }

    pthread_mutex_unlock(&session->lock);
    return err;
}
