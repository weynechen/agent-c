/**
 * @file llm.c
 * @brief LLM API client implementation with provider routing
 */

#include "agentc/llm.h"
#include "agentc/tool.h"
#include "agentc/platform.h"
#include "agentc/log.h"
#include "llm_internal.h"
#include "message/message.h"
#include "message/message_json.h"
#include "tool_call/tool_call.h"
#include "tool_call/tool_call_json.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define DEFAULT_PROVIDER "openai"
#define DEFAULT_BASE_URL "https://api.openai.com/v1"
#define DEFAULT_MODEL "gpt-3.5-turbo"
#define DEFAULT_TIMEOUT_MS 60000
#define DEFAULT_TEMPERATURE 0.7f

/*============================================================================
 * Client Create/Destroy
 *============================================================================*/

ac_llm_t *ac_llm_create(const ac_llm_params_t *params) {
    if (!params || !params->model || !params->api_key) {
        AC_LOG_ERROR("Invalid parameters: model and api_key are required");
        return NULL;
    }

    ac_llm_t *llm = AGENTC_CALLOC(1, sizeof(ac_llm_t));
    if (!llm) {
        return NULL;
    }

    /* Copy parameters */
    llm->provider_copy = params->provider ? AGENTC_STRDUP(params->provider) : NULL;
    llm->compatible_copy = params->compatible ? AGENTC_STRDUP(params->compatible) : NULL;
    llm->model_copy = AGENTC_STRDUP(params->model);
    llm->api_key_copy = AGENTC_STRDUP(params->api_key);
    llm->api_base_copy = params->api_base ? AGENTC_STRDUP(params->api_base) : AGENTC_STRDUP(DEFAULT_BASE_URL);
    llm->instructions_copy = params->instructions ? AGENTC_STRDUP(params->instructions) : NULL;
    llm->organization_copy = params->organization ? AGENTC_STRDUP(params->organization) : NULL;

    if (!llm->model_copy || !llm->api_key_copy || !llm->api_base_copy) {
        AGENTC_FREE(llm->provider_copy);
        AGENTC_FREE(llm->compatible_copy);
        AGENTC_FREE(llm->model_copy);
        AGENTC_FREE(llm->api_key_copy);
        AGENTC_FREE(llm->api_base_copy);
        AGENTC_FREE(llm->instructions_copy);
        AGENTC_FREE(llm->organization_copy);
        AGENTC_FREE(llm);
        return NULL;
    }

    /* Set parameters */
    llm->params.provider = llm->provider_copy;
    llm->params.compatible = llm->compatible_copy;
    llm->params.model = llm->model_copy;
    llm->params.api_key = llm->api_key_copy;
    llm->params.api_base = llm->api_base_copy;
    llm->params.instructions = llm->instructions_copy;
    llm->params.organization = llm->organization_copy;
    llm->params.temperature = (params->temperature > 0.0f) ? params->temperature : DEFAULT_TEMPERATURE;
    llm->params.max_tokens = params->max_tokens;
    llm->params.top_p = params->top_p;
    llm->params.top_k = params->top_k;
    llm->params.timeout_ms = (params->timeout_ms > 0) ? params->timeout_ms : DEFAULT_TIMEOUT_MS;

    /* Find appropriate provider */
    llm->ops = ac_llm_find_provider(&llm->params);
    if (!llm->ops) {
        AC_LOG_ERROR("No provider found, please check you " );
        AGENTC_FREE(llm->provider_copy);
        AGENTC_FREE(llm->compatible_copy);
        AGENTC_FREE(llm->model_copy);
        AGENTC_FREE(llm->api_key_copy);
        AGENTC_FREE(llm->api_base_copy);
        AGENTC_FREE(llm->instructions_copy);
        AGENTC_FREE(llm->organization_copy);
        AGENTC_FREE(llm);
        return NULL;
    }

    /* Create provider private data */
    if (llm->ops->create) {
        llm->priv = llm->ops->create(&llm->params);
        if (!llm->priv) {
            AC_LOG_ERROR("Provider %s failed to create private data", llm->ops->name);
            AGENTC_FREE(llm->provider_copy);
            AGENTC_FREE(llm->compatible_copy);
            AGENTC_FREE(llm->model_copy);
            AGENTC_FREE(llm->api_key_copy);
            AGENTC_FREE(llm->api_base_copy);
            AGENTC_FREE(llm->instructions_copy);
            AGENTC_FREE(llm->organization_copy);
            AGENTC_FREE(llm);
            return NULL;
        }
    }

    AC_LOG_INFO("LLM created: provider=%s, model=%s, base=%s", 
                llm->ops->name, llm->params.model, llm->params.api_base);
    return llm;
}

void ac_llm_destroy(ac_llm_t *llm) {
    if (!llm) return;

    /* Cleanup provider private data */
    if (llm->ops && llm->ops->cleanup) {
        llm->ops->cleanup(llm->priv);
    }

    /* Free string copies */
    AGENTC_FREE(llm->provider_copy);
    AGENTC_FREE(llm->compatible_copy);
    AGENTC_FREE(llm->model_copy);
    AGENTC_FREE(llm->api_key_copy);
    AGENTC_FREE(llm->api_base_copy);
    AGENTC_FREE(llm->instructions_copy);
    AGENTC_FREE(llm->organization_copy);
    
    AGENTC_FREE(llm);
}

/*============================================================================
 * Provider Registration & Discovery
 *============================================================================*/

#define MAX_PROVIDERS 32

typedef struct {
    const char *name;
    const ac_llm_ops_t *ops;
} provider_entry_t;

static provider_entry_t s_providers[MAX_PROVIDERS];
static int s_provider_count = 0;
static int s_providers_initialized = 0;

/* Forward declarations for built-in providers */
extern const ac_llm_ops_t openai_ops;
extern const ac_llm_ops_t anthropic_ops;

/**
 * @brief Initialize built-in providers
 * 
 * This is called lazily on first use to ensure built-in providers
 * are registered even in static library builds where constructors
 * may not execute automatically.
 */
static void ac_llm_init_builtin_providers(void) {
    if (s_providers_initialized) {
        return;
    }
    
    /* Register built-in providers */
    ac_llm_register_provider("openai", &openai_ops);
    ac_llm_register_provider("anthropic", &anthropic_ops);
    
    s_providers_initialized = 1;
    AC_LOG_DEBUG("Built-in providers initialized");
}

void ac_llm_register_provider(const char *name, const ac_llm_ops_t *ops) {
    if (!name || !ops || s_provider_count >= MAX_PROVIDERS) {
        return;
    }
    
    /* Check for duplicate */
    for (int i = 0; i < s_provider_count; i++) {
        if (strcmp(s_providers[i].name, name) == 0) {
            AC_LOG_WARN("Provider '%s' already registered, skipping", name);
            return;
        }
    }
    
    s_providers[s_provider_count].name = name;
    s_providers[s_provider_count].ops = ops;
    s_provider_count++;
    
    AC_LOG_DEBUG("Provider registered: %s", name);
}

const ac_llm_ops_t* ac_llm_find_provider_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < s_provider_count; i++) {
        if (strcmp(s_providers[i].name, name) == 0) {
            return s_providers[i].ops;
        }
    }
    
    return NULL;
}

const ac_llm_ops_t* ac_llm_find_provider(const ac_llm_params_t* params) {
    if (!params) {
        return NULL;
    }
    
    /* Ensure built-in providers are registered */
    ac_llm_init_builtin_providers();

    if(params->provider == NULL) {
        AC_LOG_ERROR("Please set llm provider");
    }
     
    /* Strategy 1: Use compatible mode (e.g., "openai" for OpenAI-compatible APIs) */
    if (params->compatible && params->compatible[0] != '\0') {
        const ac_llm_ops_t* ops = ac_llm_find_provider_by_name(params->compatible);
        if (ops) {
            AC_LOG_DEBUG("Using provider: %s (compatible mode)", params->compatible);
            return ops;
        }
    }

    /* Strategy 2: Use explicitly specified provider */
    if (params->provider && params->provider[0] != '\0') {
        const ac_llm_ops_t* ops = ac_llm_find_provider_by_name(params->provider);
        if (ops) {
            AC_LOG_DEBUG("Using provider: %s (explicit)", params->provider);
            return ops;
        }
        AC_LOG_WARN("Provider '%s' not found ", params->provider);
    }
       
    /* No provider found */
    AC_LOG_ERROR("No suitable provider found for model=%s", params->provider);
    return NULL;
}

/*============================================================================
 * Build Request JSON (used by providers)
 *============================================================================*/

char *build_chat_request_json(
    const ac_llm_t *llm,
    const ac_message_t *messages,
    const char *tools
) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* Model */
    cJSON_AddStringToObject(root, "model", llm->params.model);

    /* Messages array */
    cJSON *msgs_arr = cJSON_AddArrayToObject(root, "messages");
    
    /* Add system message if instructions provided */
    if (llm->params.instructions) {
        cJSON *sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", llm->params.instructions);
        cJSON_AddItemToArray(msgs_arr, sys_msg);
    }
    
    /* Add user messages */
    for (const ac_message_t *msg = messages; msg; msg = msg->next) {
        cJSON *msg_obj = ac_message_to_json(msg);
        if (msg_obj) {
            cJSON_AddItemToArray(msgs_arr, msg_obj);
        }
    }

    /* Temperature */
    if (llm->params.temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)llm->params.temperature);
    }

    /* Max tokens */
    if (llm->params.max_tokens > 0) {
        cJSON_AddNumberToObject(root, "max_tokens", llm->params.max_tokens);
    }

    /* Top-p */
    if (llm->params.top_p > 0.0f) {
        cJSON_AddNumberToObject(root, "top_p", (double)llm->params.top_p);
    }

    /* Stream */
    cJSON_AddBoolToObject(root, "stream", 0);

    /* Tools */
    if (tools && strlen(tools) > 0) {
        cJSON *tools_arr = cJSON_Parse(tools);
        if (tools_arr) {
            cJSON_AddItemToObject(root, "tools", tools_arr);
            cJSON_AddStringToObject(root, "tool_choice", "auto");
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

/*============================================================================
 * Chat Completion (Blocking)
 *============================================================================*/

agentc_err_t ac_llm_chat(
    ac_llm_t *llm,
    const ac_message_t *messages,
    const char *tools,
    ac_chat_response_t *response
) {
    if (!llm || !messages || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    if (!llm->ops || !llm->ops->chat) {
        AC_LOG_ERROR("No provider chat function available");
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Delegate to provider implementation */
    return llm->ops->chat(llm->priv, &llm->params, messages, tools, response);
}

/*============================================================================
 * Simple Completion
 *============================================================================*/

agentc_err_t ac_llm_complete(
    ac_llm_t *llm,
    const char *prompt,
    char **response
) {
    if (!llm || !prompt || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    /* Build message list */
    ac_message_t *messages = ac_message_create(AC_ROLE_USER, prompt);
    if (!messages) {
        return AGENTC_ERR_NO_MEMORY;
    }

    /* Make request */
    ac_chat_response_t resp = {0};
    agentc_err_t err = ac_llm_chat(llm, messages, NULL, &resp);

    /* Cleanup messages */
    ac_message_free(messages);

    if (err != AGENTC_OK) {
        ac_chat_response_free(&resp);
        return err;
    }

    /* Return content */
    *response = resp.content;
    resp.content = NULL;  /* Transfer ownership */

    ac_chat_response_free(&resp);
    return AGENTC_OK;
}

/*============================================================================
 * Chat Response Management
 *============================================================================*/

void ac_chat_response_free(ac_chat_response_t *response) {
    if (!response) return;

    AGENTC_FREE(response->id);
    AGENTC_FREE(response->model);
    AGENTC_FREE(response->content);
    AGENTC_FREE(response->finish_reason);
    ac_tool_call_free(response->tool_calls);

    memset(response, 0, sizeof(*response));
}

/*============================================================================
 * Chat Response Parsing
 *============================================================================*/

agentc_err_t ac_chat_response_parse(
    const char *json,
    ac_chat_response_t *response
) {
    if (!json || !response) {
        return AGENTC_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        AC_LOG_ERROR("Failed to parse JSON response");
        return AGENTC_ERR_HTTP;
    }

    /* Check for error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        if (msg && cJSON_IsString(msg)) {
            AC_LOG_ERROR("API error: %s", msg->valuestring);
        }
        cJSON_Delete(root);
        return AGENTC_ERR_HTTP;
    }

    /* Parse id and model */
    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id && cJSON_IsString(id)) {
        response->id = AGENTC_STRDUP(id->valuestring);
    }

    cJSON *model = cJSON_GetObjectItem(root, "model");
    if (model && cJSON_IsString(model)) {
        response->model = AGENTC_STRDUP(model->valuestring);
    }

    /* Parse choices[0] */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        if (first_choice) {
            cJSON *message = cJSON_GetObjectItem(first_choice, "message");
            if (message) {
                /* Content */
                cJSON *content = cJSON_GetObjectItem(message, "content");
                if (content && cJSON_IsString(content)) {
                    response->content = AGENTC_STRDUP(content->valuestring);
                }

                /* Tool calls */
                cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    response->tool_calls = ac_tool_call_parse_json(tool_calls);
                }
            }

            /* Finish reason */
            cJSON *finish_reason = cJSON_GetObjectItem(first_choice, "finish_reason");
            if (finish_reason && cJSON_IsString(finish_reason)) {
                response->finish_reason = AGENTC_STRDUP(finish_reason->valuestring);
            }
        }
    }

    /* Parse usage */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *prompt_tokens = cJSON_GetObjectItem(usage, "prompt_tokens");
        if (prompt_tokens && cJSON_IsNumber(prompt_tokens)) {
            response->prompt_tokens = (int)prompt_tokens->valuedouble;
        }

        cJSON *completion_tokens = cJSON_GetObjectItem(usage, "completion_tokens");
        if (completion_tokens && cJSON_IsNumber(completion_tokens)) {
            response->completion_tokens = (int)completion_tokens->valuedouble;
        }

        cJSON *total_tokens = cJSON_GetObjectItem(usage, "total_tokens");
        if (total_tokens && cJSON_IsNumber(total_tokens)) {
            response->total_tokens = (int)total_tokens->valuedouble;
        }
    }

    cJSON_Delete(root);

    /* Consider success if we have content OR tool_calls */
    if (response->content || response->tool_calls) {
        return AGENTC_OK;
    }

    return AGENTC_ERR_HTTP;
}
