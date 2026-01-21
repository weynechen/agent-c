/**
 * @file tool_call.h
 * @brief Tool call data structure management
 * 
 * Manages tool call structures returned by LLM.
 * Separated from tool registry (tool.h) which manages tool definitions.
 */

#ifndef AGENTC_TOOL_CALL_H
#define AGENTC_TOOL_CALL_H

#include "agentc/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Tool Call Operations
 *============================================================================*/

/**
 * @brief Clone a tool call list
 *
 * @param calls  Tool calls to clone
 * @return Cloned list (caller must free with ac_tool_call_free)
 */
ac_tool_call_t *ac_tool_call_clone(const ac_tool_call_t *calls);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_TOOL_CALL_H */
