/**
 * @file error.h
 * @brief ArC Error Codes
 *
 * Common error codes used throughout ArC library.
 */

#ifndef ARC_ERROR_H
#define ARC_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    ARC_OK = 0,                      /* Success */
    ARC_ERR_INVALID_ARG = -1,        /* Invalid argument */
    ARC_ERR_NO_MEMORY = -2,          /* Out of memory */
    ARC_ERR_MEMORY = -2,             /* Alias for ARC_ERR_NO_MEMORY */
    ARC_ERR_NETWORK = -3,            /* Network error */
    ARC_ERR_TLS = -4,                /* TLS/SSL error */
    ARC_ERR_TIMEOUT = -5,            /* Request timeout */
    ARC_ERR_DNS = -6,                /* DNS resolution failed */
    ARC_ERR_HTTP = -7,               /* HTTP error */
    ARC_ERR_NOT_INITIALIZED = -8,    /* Not initialized */
    ARC_ERR_BACKEND = -9,            /* Backend error */
    ARC_ERR_IO = -10,                /* I/O operation failed */
    ARC_ERR_NOT_IMPLEMENTED = -11,   /* Feature not implemented */
    ARC_ERR_NOT_FOUND = -12,         /* Resource not found */
    ARC_ERR_NOT_CONNECTED = -13,     /* Not connected */
    ARC_ERR_PROTOCOL = -14,          /* Protocol error */
    ARC_ERR_PARSE = -15,             /* Parse error */
    ARC_ERR_RESPONSE_TOO_LARGE = -16, /* Response size exceeds limit */
    ARC_ERR_INVALID_STATE = -17,     /* Invalid state for operation */
} arc_err_t;

/*============================================================================
 * Error String (declared in arc.h, implemented in arc.c)
 *============================================================================*/

/**
 * @brief Get error description string
 *
 * @param err  Error code
 * @return Static string describing the error
 */
const char *ac_strerror(arc_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* ARC_ERROR_H */
