/**
 * @file arc.c
 * @brief ArC global initialization and utilities
 *
 * Note: As of the refactoring, HTTP backend initialization (e.g., curl_global_init)
 * is now managed automatically via reference counting in http_client_create/destroy.
 *
 * This file now provides:
 * - Optional initialization hook (for future extensions)
 * - Version information
 * - Error code to string conversion
 */

#include "arc.h"

const char *ac_version(void) {
    return ARC_VERSION_STRING;
}

const char *ac_strerror(arc_err_t err) {
    switch (err) {
        case ARC_OK:                  return "Success";
        case ARC_ERR_INVALID_ARG:     return "Invalid argument";
        case ARC_ERR_NO_MEMORY:       return "Out of memory";
        case ARC_ERR_NETWORK:         return "Network error";
        case ARC_ERR_TLS:             return "TLS/SSL error";
        case ARC_ERR_TIMEOUT:         return "Request timeout";
        case ARC_ERR_DNS:             return "DNS resolution failed";
        case ARC_ERR_HTTP:            return "HTTP error";
        case ARC_ERR_NOT_INITIALIZED: return "Not initialized";
        case ARC_ERR_BACKEND:         return "Backend error";
        case ARC_ERR_IO:              return "I/O operation failed";
        case ARC_ERR_NOT_IMPLEMENTED: return "Feature not implemented";
        case ARC_ERR_NOT_FOUND:       return "Resource not found";
        case ARC_ERR_NOT_CONNECTED:   return "Not connected";
        case ARC_ERR_PROTOCOL:        return "Protocol error";
        case ARC_ERR_PARSE:           return "Parse error";
        case ARC_ERR_RESPONSE_TOO_LARGE: return "Response size exceeds limit";
        case ARC_ERR_INVALID_STATE:   return "Invalid state for operation";
        default:                         return "Unknown error";
    }
}
