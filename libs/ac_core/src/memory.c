/**
 * @file memory.c
 * @brief Memory management implementation
 */

#include "agentc/memory.h"
#include "agentc/llm.h"
#include "agentc/platform.h"
#include <string.h>

/*============================================================================
 * Internal Structures
 *============================================================================*/

struct ac_memory {
    char *session_id;
    size_t max_messages;
    size_t max_tokens;
    
    /* Message list */
    ac_message_t *messages;
    size_t count;
    
    /* Persistent storage (reserved) */
    char *db_path;
    int enable_persistence;
};

/*============================================================================
 * Helper Functions
 *============================================================================*/

static ac_message_t *clone_message(const ac_message_t *msg) {
    if (!msg) return NULL;
    
    ac_message_t *clone = AGENTC_CALLOC(1, sizeof(ac_message_t));
    if (!clone) return NULL;
    
    clone->role = msg->role;
    clone->content = msg->content ? AGENTC_STRDUP(msg->content) : NULL;
    clone->name = msg->name ? AGENTC_STRDUP(msg->name) : NULL;
    clone->tool_call_id = msg->tool_call_id ? AGENTC_STRDUP(msg->tool_call_id) : NULL;
    
    // Clone tool_calls if present
    if (msg->tool_calls) {
        clone->tool_calls = ac_tool_call_clone(msg->tool_calls);
    }
    
    return clone;
}

/*============================================================================
 * API Implementation
 *============================================================================*/

ac_memory_t *ac_memory_create(const ac_memory_config_t *config) {
    ac_memory_t *memory = AGENTC_CALLOC(1, sizeof(ac_memory_t));
    if (!memory) return NULL;
    
    if (config) {
        if (config->session_id) {
            memory->session_id = AGENTC_STRDUP(config->session_id);
        }
        memory->max_messages = config->max_messages;
        memory->max_tokens = config->max_tokens;
        
        if (config->db_path) {
            memory->db_path = AGENTC_STRDUP(config->db_path);
        }
        memory->enable_persistence = config->enable_persistence;
    }
    
    AC_LOG_DEBUG("Memory created: session=%s", 
                     memory->session_id ? memory->session_id : "(none)");
    
    return memory;
}

void ac_memory_destroy(ac_memory_t *memory) {
    if (!memory) return;
    
    AGENTC_FREE(memory->session_id);
    AGENTC_FREE(memory->db_path);
    
    ac_message_free(memory->messages);
    
    AGENTC_FREE(memory);
    AC_LOG_DEBUG("Memory destroyed");
}

agentc_err_t ac_memory_add(ac_memory_t *memory, const ac_message_t *message) {
    if (!memory || !message) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    // Clone the message
    ac_message_t *new_msg = clone_message(message);
    if (!new_msg) {
        return AGENTC_ERR_NO_MEMORY;
    }
    
    // Append to list
    if (!memory->messages) {
        memory->messages = new_msg;
    } else {
        ac_message_t *last = memory->messages;
        while (last->next) {
            last = last->next;
        }
        last->next = new_msg;
    }
    
    memory->count++;
    
    // TODO: Implement max_messages limit
    // TODO: Implement max_tokens limit
    
    AC_LOG_DEBUG("Message added to memory: role=%s, count=%zu", 
                     ac_role_to_string(message->role), memory->count);
    
    return AGENTC_OK;
}

const ac_message_t *ac_memory_get_messages(ac_memory_t *memory) {
    if (!memory) return NULL;
    return memory->messages;
}

size_t ac_memory_count(ac_memory_t *memory) {
    if (!memory) return 0;
    return memory->count;
}

void ac_memory_clear(ac_memory_t *memory) {
    if (!memory) return;
    
    ac_message_free(memory->messages);
    memory->messages = NULL;
    memory->count = 0;
    
    AC_LOG_DEBUG("Memory cleared");
}

ac_message_t *ac_memory_get_last_n(ac_memory_t *memory, size_t n) {
    if (!memory || n == 0) return NULL;
    
    // Count total messages
    size_t total = memory->count;
    if (total == 0) return NULL;
    
    // Calculate skip count
    size_t skip = (total > n) ? (total - n) : 0;
    
    // Find starting point
    ac_message_t *curr = memory->messages;
    for (size_t i = 0; i < skip && curr; i++) {
        curr = curr->next;
    }
    
    // Clone remaining messages
    ac_message_t *head = NULL;
    ac_message_t *tail = NULL;
    
    while (curr) {
        ac_message_t *clone = clone_message(curr);
        if (!clone) {
            ac_message_free(head);
            return NULL;
        }
        
        if (!head) {
            head = clone;
            tail = clone;
        } else {
            tail->next = clone;
            tail = clone;
        }
        
        curr = curr->next;
    }
    
    return head;
}

agentc_err_t ac_memory_save(ac_memory_t *memory) {
    if (!memory) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    // TODO: Implement persistent storage with SQLite
    AC_LOG_WARN("Persistent storage not implemented yet");
    
    return AGENTC_ERR_BACKEND;
}

agentc_err_t ac_memory_load(ac_memory_t *memory) {
    if (!memory) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    // TODO: Implement persistent storage with SQLite
    AC_LOG_WARN("Persistent storage not implemented yet");
    
    return AGENTC_ERR_BACKEND;
}
