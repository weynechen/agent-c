/**
 * @file message_json.h
 * @brief Message JSON serialization/deserialization
 *
 * Handles conversion between ac_message_t and JSON format for LLM APIs.
 */

#ifndef AGENTC_MESSAGE_JSON_H
#define AGENTC_MESSAGE_JSON_H

#include "agentc/message.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Message to JSON
 *============================================================================*/

/**
 * @brief Convert message to JSON object
 *
 * Creates a JSON object suitable for OpenAI-compatible API:
 * - role: "system" | "user" | "assistant" | "tool"
 * - content: message text
 * - tool_call_id: (for tool messages) which call this responds to
 * - tool_calls: (for assistant messages) array of tool calls
 *
 * @param msg Message to convert
 * @return cJSON object (caller owns), NULL on error
 */
cJSON* ac_message_to_json(const ac_message_t* msg);

/**
 * @brief Convert tool call to JSON object
 *
 * @param call Tool call to convert
 * @return cJSON object (caller owns), NULL on error
 */
cJSON* ac_tool_call_to_json(const ac_tool_call_t* call);

/*============================================================================
 * JSON to Response
 *============================================================================*/

/**
 * @brief Parse LLM API response JSON into ac_chat_response_t
 *
 * Handles OpenAI-compatible response format:
 * {
 *   "choices": [{
 *     "message": {
 *       "content": "...",
 *       "tool_calls": [...]
 *     },
 *     "finish_reason": "stop" | "tool_calls" | ...
 *   }],
 *   "usage": { "prompt_tokens": N, "completion_tokens": N, "total_tokens": N }
 * }
 *
 * @param json_str Raw JSON response string
 * @param response Output structure (caller must init and free)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_chat_response_parse(const char* json_str, ac_chat_response_t* response);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MESSAGE_JSON_H */
