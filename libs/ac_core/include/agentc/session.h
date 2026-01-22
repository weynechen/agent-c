/**
 * @file session.h
 * @brief AgentC Session Management
 *
 * Session provides lifecycle management for agents.
 * All agents belong to a session and are destroyed when the session closes.
 */

#ifndef AGENTC_SESSION_H
#define AGENTC_SESSION_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Session Handle (opaque)
 *============================================================================*/

typedef struct ac_session ac_session_t;

/*============================================================================
 * Session API
 *============================================================================*/

/**
 * @brief Open a new session
 *
 * Creates a session to manage agent lifecycle.
 *
 * Example:
 * @code
 * ac_session_t* session = ac_session_open();
 * 
 * // Create agents...
 * ac_agent_t* agent = ac_agent_create(session, config);
 * 
 * // Use agents...
 * 
 * // Close session (destroys all agents)
 * ac_session_close(session);
 * @endcode
 *
 * @return Session handle, NULL on error
 */
ac_session_t* ac_session_open(void);

/**
 * @brief Close session and destroy all agents
 *
 * Automatically destroys all agents created in this session.
 * All agent pointers become invalid after this call.
 *
 * @param session  Session handle
 */
void ac_session_close(ac_session_t* session);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_SESSION_H */
