/**
 * @file agentc.c
 * @brief AgentC global initialization and utilities
 * 
 * Note: As of the refactoring, HTTP backend initialization (e.g., curl_global_init)
 * is now managed automatically via reference counting in http_client_create/destroy.
 * 
 * This file now provides:
 * - Optional initialization hook (for future extensions)
 * - Version information
 * - Error code to string conversion
 */

#include "agentc.h"

static int s_initialized = 0;

agentc_err_t ac_init(void) {
    if (s_initialized) {
        return AGENTC_OK;
    }

    /* 
     * Note: HTTP backend is now auto-initialized by providers.
     * This function is kept for future extensions and backward compatibility.
     */

    s_initialized = 1;
    AC_LOG_INFO("AgentC %s initialized", AGENTC_VERSION_STRING);
    return AGENTC_OK;
}

void ac_cleanup(void) {
    if (!s_initialized) {
        return;
    }

    /* 
     * Note: HTTP backend is now auto-cleaned up when last client is destroyed.
     * This function is kept for future extensions and backward compatibility.
     */

    s_initialized = 0;
    AC_LOG_INFO("AgentC cleaned up");
}

const char *ac_version(void) {
    return AGENTC_VERSION_STRING;
}

const char *ac_strerror(agentc_err_t err) {
    switch (err) {
        case AGENTC_OK:                  return "Success";
        case AGENTC_ERR_INVALID_ARG:     return "Invalid argument";
        case AGENTC_ERR_NO_MEMORY:       return "Out of memory";
        case AGENTC_ERR_NETWORK:         return "Network error";
        case AGENTC_ERR_TLS:             return "TLS/SSL error";
        case AGENTC_ERR_TIMEOUT:         return "Request timeout";
        case AGENTC_ERR_DNS:             return "DNS resolution failed";
        case AGENTC_ERR_HTTP:            return "HTTP error";
        case AGENTC_ERR_NOT_INITIALIZED: return "Not initialized";
        case AGENTC_ERR_BACKEND:         return "Backend error";
        case AGENTC_ERR_IO:              return "I/O operation failed";
        case AGENTC_ERR_NOT_IMPLEMENTED: return "Feature not implemented";
        case AGENTC_ERR_NOT_FOUND:       return "Resource not found";
        case AGENTC_ERR_NOT_CONNECTED:   return "Not connected";
        default:                         return "Unknown error";
    }
}
