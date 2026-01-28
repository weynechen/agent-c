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
        case AGENTC_ERR_PROTOCOL:        return "Protocol error";
        case AGENTC_ERR_PARSE:           return "Parse error";
        default:                         return "Unknown error";
    }
}
