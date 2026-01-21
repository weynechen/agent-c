/**
 * @file llm.h
 * @brief AgentC LLM API Abstraction Layer
 *
 * Unified interface for OpenAI, Claude, DeepSeek and other LLM providers.
 * Similar to LiteLLM design.
 */

#ifndef AGENTC_LLM_H
#define AGENTC_LLM_H

#include <stdint.h>
#include "error.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct ac_tool_call;  /* Defined in tool.h */

/*============================================================================
 * Message Role
 *============================================================================*/

typedef enum {
    AC_ROLE_SYSTEM,
    AC_ROLE_USER,
    AC_ROLE_ASSISTANT,
    AC_ROLE_TOOL,
} ac_role_t;

/*============================================================================
 * Chat Message
 *============================================================================*/

typedef struct ac_message {
    ac_role_t role;
    char *content;                       /* Message content (may be NULL for tool_calls) */
    char *name;                          /* Optional: for tool messages */
    char *tool_call_id;                  /* Optional: for tool responses */
    struct ac_tool_call *tool_calls;     /* Optional: for assistant tool calls */
    struct ac_message *next;
} ac_message_t;

/*============================================================================
 * LLM Parameters (combines config and request params)
 *============================================================================*/

typedef struct {
    /* Provider selection */
    const char *provider;               /* Provider name: "openai", "anthropic", "gemini", etc. (optional) */
    const char *compatible;             /* Compatibility mode: "openai" for OpenAI-compatible APIs (optional) */
    
    /* LLM base info */
    const char *model;                  /* Model name (required) */
    const char *api_key;                /* API key (required) */
    const char *api_base;               /* API base URL (optional) */
    const char *instructions;           /* System prompt (optional) */

    /* LLM parameters */
    float temperature;                  /* 0.0 - 2.0 (default: 0.7) */
    int max_tokens;                     /* Max tokens to generate (default: 0 = no limit) */
    float top_p;                        /* 0.0 - 1.0 (default: 1.0) */
    int top_k;                          /* Top-k sampling (provider specific) */
    
    /* Advanced settings */
    const char *organization;           /* Organization ID (optional) */
    uint32_t timeout_ms;                /* Request timeout (default: 60000) */
} ac_llm_params_t;

/*============================================================================
 * Chat Completion Response (non-streaming)
 *============================================================================*/

typedef struct {
    char *id;                           /* Response ID */
    char *model;                        /* Model used */
    char *content;                      /* Assistant message content (may be NULL) */
    char *finish_reason;                /* stop, length, tool_calls, etc. */
    struct ac_tool_call *tool_calls;    /* Tool calls (if finish_reason == "tool_calls") */
    int prompt_tokens;                  /* Input tokens used */
    int completion_tokens;              /* Output tokens generated */
    int total_tokens;                   /* Total tokens */
} ac_chat_response_t;

/*============================================================================
 * LLM Client Handle (opaque)
 *============================================================================*/

typedef struct ac_llm ac_llm_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Create an LLM client
 *
 * Example:
 * @code
 * ac_llm_t *llm = ac_llm_create(&(ac_llm_params_t){
 *     .model = "deepseek/deepseek-chat",
 *     .api_key = getenv("DEEPSEEK_API_KEY"),
 *     .instructions = "You are a helpful assistant",
 *     .temperature = 0.7
 * });
 * @endcode
 *
 * @param params  LLM parameters
 * @return LLM client handle, NULL on error
 */
ac_llm_t *ac_llm_create(const ac_llm_params_t *params);

/**
 * @brief Destroy an LLM client
 *
 * @param llm  LLM client handle
 */
void ac_llm_destroy(ac_llm_t *llm);

/**
 * @brief Perform a chat completion (blocking)
 *
 * @param llm       LLM client handle
 * @param messages  Message history (linked list)
 * @param tools     Tools JSON string (optional)
 * @param response  Output response (caller must free with ac_chat_response_free)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_llm_chat(
    ac_llm_t *llm,
    const ac_message_t *messages,
    const char *tools,
    ac_chat_response_t *response
);

/**
 * @brief Simple one-shot completion
 *
 * Convenience function for single prompt -> response.
 *
 * @param llm      LLM client handle
 * @param prompt   User prompt
 * @param response Output response string (caller must free)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t ac_llm_complete(
    ac_llm_t *llm,
    const char *prompt,
    char **response
);

/**
 * @brief Free chat response resources
 *
 * @param response  Response to free
 */
void ac_chat_response_free(ac_chat_response_t *response);

/*============================================================================
 * Message Helper Functions
 *============================================================================*/

/**
 * @brief Create a message
 *
 * @param role     Message role
 * @param content  Message content
 * @return New message (caller must free), NULL on error
 */
ac_message_t *ac_message_create(ac_role_t role, const char *content);

/**
 * @brief Create a tool result message
 *
 * @param tool_call_id  ID of the tool call this responds to
 * @param content       Tool result content
 * @return New message (caller must free), NULL on error
 */
ac_message_t *ac_message_create_tool_result(
    const char *tool_call_id,
    const char *content
);

/**
 * @brief Create an assistant message with tool calls
 *
 * @param content     Optional text content (can be NULL)
 * @param tool_calls  Tool calls (ownership transferred to message)
 * @return New message (caller must free), NULL on error
 */
ac_message_t *ac_message_create_assistant_tool_calls(
    const char *content,
    struct ac_tool_call *tool_calls
);

/**
 * @brief Append message to list
 *
 * @param list     Pointer to message list head
 * @param message  Message to append
 */
void ac_message_append(ac_message_t **list, ac_message_t *message);

/**
 * @brief Free message list
 *
 * @param list  Message list to free
 */
void ac_message_free(ac_message_t *list);

/**
 * @brief Get role string
 *
 * @param role  Role enum
 * @return Role string ("system", "user", "assistant", "tool")
 */
const char *ac_role_to_string(ac_role_t role);

/**
 * @brief Clone a tool call list
 *
 * @param calls  Tool calls to clone
 * @return Cloned list (caller must free)
 */
struct ac_tool_call *ac_tool_call_clone(const struct ac_tool_call *calls);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_H */
