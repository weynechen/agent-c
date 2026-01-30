/**
 * @file time_posix.c
 * @brief POSIX platform time implementation
 */

#include "arc/platform.h"
#include <sys/time.h>

/**
 * @brief Get current timestamp in milliseconds since Unix epoch
 */
uint64_t ac_platform_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}
