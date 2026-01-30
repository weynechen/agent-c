/**
 * @file time_freertos.c
 * @brief FreeRTOS platform time implementation
 */

#include "arc/platform.h"

/**
 * @brief Weak symbol for platform-specific epoch offset
 *
 * Users should provide this value if they need accurate timestamps.
 * This is the Unix timestamp (in ms) when the device booted.
 */
__attribute__((weak)) uint64_t platform_boot_epoch_ms = 0;

/**
 * @brief Weak symbol for platform-specific tick to milliseconds conversion
 *
 * Default assumes 1 tick = 1 ms (configTICK_RATE_HZ = 1000).
 * Override this function for different tick rates.
 *
 * Example implementation:
 *   uint64_t platform_get_tick_ms(void) {
 *       return xTaskGetTickCount() * portTICK_PERIOD_MS;
 *   }
 */
__attribute__((weak)) uint64_t platform_get_tick_ms(void) {
    return 0;
}

/**
 * @brief Get current timestamp in milliseconds since Unix epoch
 *
 * For embedded systems, this returns (boot_epoch + ticks_since_boot).
 * If platform_boot_epoch_ms is not set, returns relative time only.
 */
uint64_t ac_platform_timestamp_ms(void) {
    return platform_boot_epoch_ms + platform_get_tick_ms();
}
