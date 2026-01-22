/**
 * @file session.c
 * @brief Session management implementation
 */

#include "agentc/session.h"
#include "agentc/agent.h"
#include "agentc/log.h"
#include <stdlib.h>
#include <string.h>

#define MAX_AGENTS 32

/*============================================================================
 * Session Structure
 *============================================================================*/

struct ac_session {
    ac_agent_t* agents[MAX_AGENTS];
    size_t agent_count;
};

/*============================================================================
 * Session API Implementation
 *============================================================================*/

ac_session_t* ac_session_open(void) {
    ac_session_t* session = (ac_session_t*)calloc(1, sizeof(ac_session_t));
    if (!session) {
        AC_LOG_ERROR("Failed to allocate session");
        return NULL;
    }
    
    session->agent_count = 0;
    
    AC_LOG_INFO("Session opened");
    return session;
}

void ac_session_close(ac_session_t* session) {
    if (!session) {
        return;
    }
    
    // Destroy all agents in the session
    for (size_t i = 0; i < session->agent_count; i++) {
        if (session->agents[i]) {
            ac_agent_destroy(session->agents[i]);
        }
    }
    
    AC_LOG_INFO("Session closed: destroyed %zu agents", session->agent_count);
    
    free(session);
}

/*============================================================================
 * Internal API (used by agent.c)
 *============================================================================*/

agentc_err_t ac_session_add_agent(ac_session_t* session, ac_agent_t* agent) {
    if (!session || !agent) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    if (session->agent_count >= MAX_AGENTS) {
        AC_LOG_ERROR("Session full: cannot add more agents (max=%d)", MAX_AGENTS);
        return AGENTC_ERR_NO_MEMORY;
    }
    
    session->agents[session->agent_count++] = agent;
    return AGENTC_OK;
}
