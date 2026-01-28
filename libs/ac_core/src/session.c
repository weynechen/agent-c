/**
 * @file session.c
 * @brief Session management implementation
 *
 * Manages lifecycle of agents, tool registries, and MCP clients.
 */

#include "agentc/session.h"
#include "agentc/agent.h"
#include "agentc/tool.h"
#include "agentc/mcp.h"
#include "agentc/arena.h"
#include "agentc/log.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define MAX_AGENTS 32
#define MAX_REGISTRIES 16
#define MAX_MCP_CLIENTS 16
#define SESSION_ARENA_SIZE (4 * 1024 * 1024)  /* 4MB for session resources */

/*============================================================================
 * Session Structure
 *============================================================================*/

struct ac_session {
    arena_t *arena;                              /* Session arena */
    
    ac_agent_t *agents[MAX_AGENTS];              /* Agents */
    size_t agent_count;
    
    ac_tool_registry_t *registries[MAX_REGISTRIES];  /* Tool registries */
    size_t registry_count;
    
    ac_mcp_client_t *mcp_clients[MAX_MCP_CLIENTS];   /* MCP clients */
    size_t mcp_client_count;
};

/*============================================================================
 * Internal API Declarations (called by mcp.c cleanup)
 *============================================================================*/

extern void ac_mcp_cleanup(ac_mcp_client_t *client);

/*============================================================================
 * Session API Implementation
 *============================================================================*/

ac_session_t *ac_session_open(void) {
    ac_session_t *session = (ac_session_t *)calloc(1, sizeof(ac_session_t));
    if (!session) {
        AC_LOG_ERROR("Failed to allocate session");
        return NULL;
    }
    
    /* Create session arena for registries and MCP clients */
    session->arena = arena_create(SESSION_ARENA_SIZE);
    if (!session->arena) {
        AC_LOG_ERROR("Failed to create session arena");
        free(session);
        return NULL;
    }
    
    session->agent_count = 0;
    session->registry_count = 0;
    session->mcp_client_count = 0;
    
    AC_LOG_INFO("Session opened (arena=%zuKB)", SESSION_ARENA_SIZE / 1024);
    return session;
}

void ac_session_close(ac_session_t *session) {
    if (!session) {
        return;
    }
    
    /* Cleanup MCP clients first (they may be referenced by tools) */
    for (size_t i = 0; i < session->mcp_client_count; i++) {
        if (session->mcp_clients[i]) {
            ac_mcp_cleanup(session->mcp_clients[i]);
        }
    }
    
    /* Destroy all agents (each has its own arena) */
    for (size_t i = 0; i < session->agent_count; i++) {
        if (session->agents[i]) {
            ac_agent_destroy(session->agents[i]);
        }
    }
    
    /* Tool registries are allocated from session arena, 
     * they will be freed when arena is destroyed */
    
    AC_LOG_INFO("Session closed: destroyed %zu agents, %zu registries, %zu MCP clients",
                session->agent_count, session->registry_count, session->mcp_client_count);
    
    /* Destroy session arena (frees all registries and their data) */
    if (session->arena) {
        arena_destroy(session->arena);
    }
    
    free(session);
}

/*============================================================================
 * Internal API (used by agent.c, tool.c, mcp.c)
 *============================================================================*/

arena_t *ac_session_get_arena(ac_session_t *session) {
    return session ? session->arena : NULL;
}

agentc_err_t ac_session_add_agent(ac_session_t *session, ac_agent_t *agent) {
    if (!session || !agent) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (session->agent_count >= MAX_AGENTS) {
        AC_LOG_ERROR("Session full: cannot add more agents (max=%d)", MAX_AGENTS);
        return AGENTC_ERR_NO_MEMORY;
    }
    
    session->agents[session->agent_count++] = agent;
    AC_LOG_DEBUG("Agent added to session (total=%zu)", session->agent_count);
    return AGENTC_OK;
}

agentc_err_t ac_session_add_registry(ac_session_t *session, ac_tool_registry_t *registry) {
    if (!session || !registry) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (session->registry_count >= MAX_REGISTRIES) {
        AC_LOG_ERROR("Session full: cannot add more registries (max=%d)", MAX_REGISTRIES);
        return AGENTC_ERR_NO_MEMORY;
    }
    
    session->registries[session->registry_count++] = registry;
    AC_LOG_DEBUG("Registry added to session (total=%zu)", session->registry_count);
    return AGENTC_OK;
}

agentc_err_t ac_session_add_mcp(ac_session_t *session, ac_mcp_client_t *client) {
    if (!session || !client) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (session->mcp_client_count >= MAX_MCP_CLIENTS) {
        AC_LOG_ERROR("Session full: cannot add more MCP clients (max=%d)", MAX_MCP_CLIENTS);
        return AGENTC_ERR_NO_MEMORY;
    }
    
    session->mcp_clients[session->mcp_client_count++] = client;
    AC_LOG_DEBUG("MCP client added to session (total=%zu)", session->mcp_client_count);
    return AGENTC_OK;
}
