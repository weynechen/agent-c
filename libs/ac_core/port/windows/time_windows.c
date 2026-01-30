/**
 * @file time_windows.c
 * @brief Windows platform time implementation
 */

#include "arc/platform.h"

#ifdef _WIN32
#include <windows.h>

/**
 * @brief Get current timestamp in milliseconds since Unix epoch
 */
uint64_t ac_platform_timestamp_ms(void) {
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* FILETIME is in 100-nanosecond intervals since Jan 1, 1601 */
    /* Convert to milliseconds since Unix epoch (Jan 1, 1970) */
    return (uint64_t)((uli.QuadPart - 116444736000000000ULL) / 10000);
}

#else
/* Non-Windows fallback (should not be compiled) */
uint64_t ac_platform_timestamp_ms(void) {
    return 0;
}
#endif
