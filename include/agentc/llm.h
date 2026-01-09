/**
 * @file llm.h
 * @brief AgentC LLM API Abstraction Layer
 *
 * OpenAI-compatible Chat Completions API interface.
 * Supports: OpenAI, Azure OpenAI, compatible endpoints (Ollama, vLLM, etc.)
 */

#ifndef AGENTC_LLM_H
#define AGENTC_LLM_H

#include "http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Message Role
 *============================================================================*/

typedef enum {
    AGENTC_ROLE_SYSTEM,
    AGENTC_ROLE_USER,
    AGENTC_ROLE_ASSISTANT,
    AGENTC_ROLE_TOOL,
} agentc_role_t;

/*============================================================================
 * Chat Message
 *============================================================================*/

typedef struct agentc_message {
    agentc_role_t role;
    const char *content;
    const char *name;                   /* Optional: for tool messages */
    const char *tool_call_id;           /* Optional: for tool responses */
    struct agentc_message *next;
} agentc_message_t;

/*============================================================================
 * LLM Configuration
 *============================================================================*/

typedef struct {
    const char *api_key;                /* API key (required) */
    const char *base_url;               /* Base URL (default: https://api.openai.com/v1) */
    const char *model;                  /* Model name (default: gpt-3.5-turbo) */
    const char *organization;           /* Organization ID (optional) */
    uint32_t timeout_ms;                /* Request timeout (default: 60000) */
} agentc_llm_config_t;

/*============================================================================
 * Chat Completion Request
 *============================================================================*/

typedef struct {
    agentc_message_t *messages;         /* Message history (linked list) */
    const char *model;                  /* Override config model (optional) */
    float temperature;                  /* 0.0 - 2.0 (default: 1.0) */
    float top_p;                        /* 0.0 - 1.0 (default: 1.0) */
    int max_tokens;                     /* Max tokens to generate (0 = no limit) */
    int stream;                         /* 1 = streaming, 0 = blocking */
    const char *stop;                   /* Stop sequence (optional) */
} agentc_chat_request_t;

/*============================================================================
 * Chat Completion Response (non-streaming)
 *============================================================================*/

typedef struct {
    char *id;                           /* Response ID */
    char *model;                        /* Model used */
    char *content;                      /* Assistant message content */
    char *finish_reason;                /* stop, length, tool_calls, etc. */
    int prompt_tokens;                  /* Input tokens used */
    int completion_tokens;              /* Output tokens generated */
    int total_tokens;                   /* Total tokens */
} agentc_chat_response_t;

/*============================================================================
 * Streaming Callbacks
 *============================================================================*/

/**
 * Called for each token/chunk in streaming mode.
 * Return 0 to continue, non-zero to abort.
 */
typedef int (*agentc_llm_stream_callback_t)(
    const char *delta,                  /* New content chunk */
    size_t len,                         /* Chunk length */
    void *user_data                     /* User context */
);

/**
 * Called when streaming is complete.
 */
typedef void (*agentc_llm_stream_done_callback_t)(
    const char *finish_reason,          /* Completion reason */
    int total_tokens,                   /* Total tokens used */
    void *user_data                     /* User context */
);

/*============================================================================
 * LLM Client Handle (opaque)
 *============================================================================*/

typedef struct agentc_llm_client agentc_llm_client_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Create an LLM client
 *
 * @param config  LLM configuration
 * @param out     Output client handle
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_llm_create(
    const agentc_llm_config_t *config,
    agentc_llm_client_t **out
);

/**
 * @brief Destroy an LLM client
 *
 * @param client  Client handle
 */
void agentc_llm_destroy(agentc_llm_client_t *client);

/**
 * @brief Perform a chat completion (blocking)
 *
 * @param client    Client handle
 * @param request   Chat request
 * @param response  Output response (caller must free with agentc_chat_response_free)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_llm_chat(
    agentc_llm_client_t *client,
    const agentc_chat_request_t *request,
    agentc_chat_response_t *response
);

/**
 * @brief Perform a streaming chat completion
 *
 * @param client      Client handle
 * @param request     Chat request (stream field is ignored, always streams)
 * @param on_chunk    Callback for each content chunk
 * @param on_done     Callback when complete (optional)
 * @param user_data   User context passed to callbacks
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_llm_chat_stream(
    agentc_llm_client_t *client,
    const agentc_chat_request_t *request,
    agentc_llm_stream_callback_t on_chunk,
    agentc_llm_stream_done_callback_t on_done,
    void *user_data
);

/**
 * @brief Simple one-shot completion
 *
 * Convenience function for single prompt -> response.
 *
 * @param client    Client handle
 * @param prompt    User prompt
 * @param response  Output response string (caller must free)
 * @return AGENTC_OK on success, error code otherwise
 */
agentc_err_t agentc_llm_complete(
    agentc_llm_client_t *client,
    const char *prompt,
    char **response
);

/**
 * @brief Free chat response resources
 *
 * @param response  Response to free
 */
void agentc_chat_response_free(agentc_chat_response_t *response);

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
agentc_message_t *agentc_message_create(agentc_role_t role, const char *content);

/**
 * @brief Append message to list
 *
 * @param list     Pointer to message list head
 * @param message  Message to append
 */
void agentc_message_append(agentc_message_t **list, agentc_message_t *message);

/**
 * @brief Free message list
 *
 * @param list  Message list to free
 */
void agentc_message_free(agentc_message_t *list);

/**
 * @brief Get role string
 *
 * @param role  Role enum
 * @return Role string ("system", "user", "assistant", "tool")
 */
const char *agentc_role_to_string(agentc_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_LLM_H */
