/**
 * @file agentc.c
 * @brief AgentC global initialization
 */

#include "agentc.h"

static int s_initialized = 0;

agentc_err_t ac_init(void) {
    if (s_initialized) {
        return AGENTC_OK;
    }

    agentc_err_t err = agentc_http_init();
    if (err != AGENTC_OK) {
        return err;
    }

    s_initialized = 1;
    AC_LOG_INFO("AgentC %s initialized", AGENTC_VERSION_STRING);
    return AGENTC_OK;
}

void ac_cleanup(void) {
    if (!s_initialized) {
        return;
    }

    agentc_http_cleanup();
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
        default:                         return "Unknown error";
    }
}
