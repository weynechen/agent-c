/**
 * @file error.h
 * @brief AgentC Error Codes
 *
 * Common error codes used throughout AgentC library.
 */

#ifndef AGENTC_ERROR_H
#define AGENTC_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    AGENTC_OK = 0,                      /* Success */
    AGENTC_ERR_INVALID_ARG = -1,        /* Invalid argument */
    AGENTC_ERR_NO_MEMORY = -2,          /* Out of memory */
    AGENTC_ERR_MEMORY = -2,             /* Alias for AGENTC_ERR_NO_MEMORY */
    AGENTC_ERR_NETWORK = -3,            /* Network error */
    AGENTC_ERR_TLS = -4,                /* TLS/SSL error */
    AGENTC_ERR_TIMEOUT = -5,            /* Request timeout */
    AGENTC_ERR_DNS = -6,                /* DNS resolution failed */
    AGENTC_ERR_HTTP = -7,               /* HTTP error */
    AGENTC_ERR_NOT_INITIALIZED = -8,    /* Not initialized */
    AGENTC_ERR_BACKEND = -9,            /* Backend error */
    AGENTC_ERR_IO = -10,                /* I/O operation failed */
    AGENTC_ERR_NOT_IMPLEMENTED = -11,   /* Feature not implemented */
    AGENTC_ERR_NOT_FOUND = -12,         /* Resource not found */
} agentc_err_t;

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_ERROR_H */
