/**
 * @file agentc.h
 * @brief AgentC - LLM Agent Runtime for Embedded and Constrained Systems
 *
 * Main include file. Include this to use AgentC.
 *
 * @code
 * #include <agentc.h>
 *
 * int main(void) {
 *     ac_init();
 *     // ... use AgentC APIs ...
 *     ac_cleanup();
 *     return 0;
 * }
 * @endcode
 */

#ifndef AGENTC_H
#define AGENTC_H

#include "agentc/platform.h"
#include "agentc/http_client.h"
#include "agentc/llm.h"
#include "agentc/tool.h"
#include "agentc/memory.h"
#include "agentc/agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version
 *============================================================================*/

#define AGENTC_VERSION_MAJOR 0
#define AGENTC_VERSION_MINOR 1
#define AGENTC_VERSION_PATCH 0
#define AGENTC_VERSION_STRING "0.1.0"

/*============================================================================
 * Global Initialization
 *============================================================================*/

/**
 * @brief Initialize AgentC runtime
 *
 * Must be called once before using any AgentC functions.
 * Initializes all subsystems (HTTP, etc.)
 *
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_init(void);

/**
 * @brief Cleanup AgentC runtime
 *
 * Call before program exit to release resources.
 */
void ac_cleanup(void);

/**
 * @brief Get AgentC version string
 *
 * @return Version string (e.g., "0.1.0")
 */
const char *ac_version(void);

/**
 * @brief Get error message for error code
 *
 * @param err  Error code
 * @return Human-readable error message
 */
const char *ac_strerror(agentc_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_H */
