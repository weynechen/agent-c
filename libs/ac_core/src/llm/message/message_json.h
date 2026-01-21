/**
 * @file message_json.h
 * @brief Message JSON serialization
 * 
 * Handles conversion between message structures and JSON format.
 * Separated for better testability and to support different formats.
 */

#ifndef AGENTC_MESSAGE_JSON_H
#define AGENTC_MESSAGE_JSON_H

#include "agentc/llm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct cJSON;

/*============================================================================
 * Serialization Functions
 *============================================================================*/

/**
 * @brief Build message JSON object
 * 
 * Converts a message structure to cJSON object suitable for API requests.
 * 
 * @param msg Message to convert
 * @return cJSON object (caller must delete with cJSON_Delete)
 */
struct cJSON *ac_message_to_json(const ac_message_t *msg);

/**
 * @brief Build messages array JSON
 * 
 * Converts a linked list of messages to cJSON array.
 * 
 * @param messages Message list
 * @return cJSON array (caller must delete with cJSON_Delete)
 */
struct cJSON *ac_messages_to_json_array(const ac_message_t *messages);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_MESSAGE_JSON_H */
