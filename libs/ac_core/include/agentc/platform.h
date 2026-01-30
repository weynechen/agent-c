/**
 * @file platform.h
 * @brief AgentC Platform Detection and Abstraction
 *
 * Detects target platform and defines backend selection.
 */

#ifndef AGENTC_PLATFORM_H
#define AGENTC_PLATFORM_H
/*============================================================================
 * Platform Detection
 *============================================================================*/

/* Linux */
#if defined(__linux__) && !defined(AGENTC_PLATFORM_LINUX)
    #define AGENTC_PLATFORM_LINUX 1
#endif

/* Windows */
#if defined(_WIN32) || defined(_WIN64)
    #define AGENTC_PLATFORM_WINDOWS 1
#endif

/* macOS */
#if defined(__APPLE__) && defined(__MACH__)
    #define AGENTC_PLATFORM_MACOS 1
#endif

/* ESP-IDF (ESP32) */
#if defined(ESP_PLATFORM) || defined(IDF_VER)
    #define AGENTC_PLATFORM_ESP32 1
    #define AGENTC_PLATFORM_EMBEDDED 1
#endif

/* Zephyr RTOS */
#if defined(__ZEPHYR__)
    #define AGENTC_PLATFORM_ZEPHYR 1
    #define AGENTC_PLATFORM_EMBEDDED 1
#endif

/* FreeRTOS (generic) */
#if defined(FREERTOS) || defined(configUSE_PREEMPTION)
    #define AGENTC_PLATFORM_FREERTOS 1
    #define AGENTC_PLATFORM_EMBEDDED 1
#endif

/* STM32 bare metal or HAL */
#if defined(STM32F4) || defined(STM32F7) || defined(STM32H7) || defined(USE_HAL_DRIVER)
    #define AGENTC_PLATFORM_STM32 1
    #define AGENTC_PLATFORM_EMBEDDED 1
#endif

/*============================================================================
 * HTTP Backend Selection
 *
 * Override with -DAGENTC_HTTP_BACKEND_xxx=1
 *============================================================================*/

#if !defined(AGENTC_HTTP_BACKEND_CURL) && !defined(AGENTC_HTTP_BACKEND_MONGOOSE)

    /* Auto-select based on platform */
    #if defined(AGENTC_PLATFORM_EMBEDDED)
        #define AGENTC_HTTP_BACKEND_MONGOOSE 1
    #else
        /* Desktop: default to libcurl */
        #define AGENTC_HTTP_BACKEND_CURL 1
    #endif

#endif

/*============================================================================
 * TLS Backend Selection (for mongoose)
 *
 * Override with -DAGENTC_TLS_BACKEND_xxx=1
 *============================================================================*/

#if defined(AGENTC_HTTP_BACKEND_MONGOOSE)

    #if !defined(AGENTC_TLS_MBEDTLS) && !defined(AGENTC_TLS_OPENSSL) && !defined(AGENTC_TLS_WOLFSSL)
        /* Auto-select TLS backend */
        #if defined(AGENTC_PLATFORM_ESP32) || defined(AGENTC_PLATFORM_STM32)
            #define AGENTC_TLS_MBEDTLS 1
        #else
            #define AGENTC_TLS_OPENSSL 1
        #endif
    #endif

#endif

/*============================================================================
 * Memory Configuration
 *
 * Platform-specific memory limits. Override with -DAGENTC_xxx=value
 *
 * Embedded platforms use smaller defaults to conserve RAM.
 * Desktop platforms use larger defaults for better performance.
 *============================================================================*/

#if defined(AGENTC_PLATFORM_EMBEDDED)

    /* Embedded platform defaults (conserve memory) */
    #ifndef AGENTC_SESSION_ARENA_SIZE
        #define AGENTC_SESSION_ARENA_SIZE       (256 * 1024)    /* 256KB */
    #endif
    #ifndef AGENTC_AGENT_ARENA_SIZE
        #define AGENTC_AGENT_ARENA_SIZE         (128 * 1024)    /* 128KB */
    #endif
    #ifndef AGENTC_ARRAY_INITIAL_CAPACITY
        #define AGENTC_ARRAY_INITIAL_CAPACITY   4               /* Small initial */
    #endif
    #ifndef AGENTC_ARENA_MIN_BLOCK_SIZE
        #define AGENTC_ARENA_MIN_BLOCK_SIZE     (4 * 1024)      /* 4KB */
    #endif
    #ifndef AGENTC_ARENA_GROWTH_FACTOR
        #define AGENTC_ARENA_GROWTH_FACTOR      2
    #endif

#else /* Desktop platforms (Linux/Windows/macOS) */

    /* Desktop platform defaults (optimize for performance) */
    #ifndef AGENTC_SESSION_ARENA_SIZE
        #define AGENTC_SESSION_ARENA_SIZE       (4 * 1024 * 1024)   /* 4MB */
    #endif
    #ifndef AGENTC_AGENT_ARENA_SIZE
        #define AGENTC_AGENT_ARENA_SIZE         (1024 * 1024)       /* 1MB */
    #endif
    #ifndef AGENTC_ARRAY_INITIAL_CAPACITY
        #define AGENTC_ARRAY_INITIAL_CAPACITY   16
    #endif
    #ifndef AGENTC_ARENA_MIN_BLOCK_SIZE
        #define AGENTC_ARENA_MIN_BLOCK_SIZE     (4 * 1024)          /* 4KB */
    #endif
    #ifndef AGENTC_ARENA_GROWTH_FACTOR
        #define AGENTC_ARENA_GROWTH_FACTOR      2
    #endif

#endif /* Platform selection */

/*============================================================================
 * Memory Allocation
 *
 * Allow custom allocators for embedded systems
 *============================================================================*/

#ifndef AGENTC_MALLOC
    #include <stdlib.h>
    #define AGENTC_MALLOC(size)       malloc(size)
    #define AGENTC_REALLOC(ptr, size) realloc(ptr, size)
    #define AGENTC_FREE(ptr)          free(ptr)
    #define AGENTC_CALLOC(n, size)    calloc(n, size)
#endif

/*============================================================================
 * String Functions
 *============================================================================*/

#ifndef AGENTC_STRDUP
    #include <string.h>
    #if defined(_WIN32)
        #define AGENTC_STRDUP(s) _strdup(s)
    #else
        #define AGENTC_STRDUP(s) strdup(s)
    #endif
#endif

#if defined(_WIN32)
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp
#endif

/*============================================================================
 * Logging Platform Handler
 * 
 * The logging macros (AC_LOG_ERROR, AC_LOG_WARN, AC_LOG_INFO, AC_LOG_DEBUG)
 * are defined in log.h. Each platform only needs to implement the handler
 * function below to output log messages.
 * 
 * Platform implementations:
 * - POSIX: port/posix/log_posix.c (file logging with colors)
 * - Windows: port/windows/log_windows.c (console with colors)
 * - FreeRTOS: port/freertos/log_freertos.c (UART/Serial)
 * 
 * Note: The actual function signature uses ac_log_level_t and va_list,
 * which are defined in log.h. The forward declaration here is minimal.
 *============================================================================*/

/*============================================================================
 * Static Assertions
 *============================================================================*/

#ifndef AGENTC_STATIC_ASSERT
    #if __STDC_VERSION__ >= 201112L
        #define AGENTC_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
    #else
        #define AGENTC_STATIC_ASSERT(cond, msg) \
            typedef char agentc_static_assert_##__LINE__[(cond) ? 1 : -1]
    #endif
#endif

/*============================================================================
 * Platform Time Functions
 * 
 * These functions must be implemented in the port layer.
 *============================================================================*/

#include <stdint.h>

/**
 * @brief Get current timestamp in milliseconds since Unix epoch
 * 
 * Platform implementations:
 * - POSIX: port/posix/log_posix.c (gettimeofday)
 * - Windows: port/windows/log_windows.c (GetSystemTimeAsFileTime)
 * - FreeRTOS: port/freertos/log_freertos.c (xTaskGetTickCount)
 * 
 * @return Milliseconds since Unix epoch (Jan 1, 1970)
 */
uint64_t ac_platform_timestamp_ms(void);

#endif /* AGENTC_PLATFORM_H */
