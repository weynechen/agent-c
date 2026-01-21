/**
 * @file tool_call_json.h
 * @brief Tool call JSON serialization
 * 
 * Handles parsing tool calls from LLM responses.
 */

#ifndef AGENTC_TOOL_CALL_JSON_H
#define AGENTC_TOOL_CALL_JSON_H

#include "agentc/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct cJSON;

/*============================================================================
 * Deserialization Functions
 *============================================================================*/

/**
 * @brief Parse tool calls from JSON array
 * 
 * Parses LLM response tool_calls array into tool call structures.
 * 
 * @param tool_calls_arr cJSON array of tool calls
 * @return Tool call list (caller must free with ac_tool_call_free), NULL on error
 */
ac_tool_call_t *ac_tool_call_parse_json(struct cJSON *tool_calls_arr);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_TOOL_CALL_JSON_H */
