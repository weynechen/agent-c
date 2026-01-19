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
 * Debug/Logging
 * 
 * Note: If log.h is included, it will provide more detailed logging macros.
 * These are simple fallback definitions for minimal environments.
 *============================================================================*/

#ifndef AC_LOG_LEVEL
    #define AC_LOG_LEVEL 3  /* 0=off, 1=error, 2=warn, 3=info, 4=debug */
#endif

#ifndef AC_LOG
    #include <stdio.h>
    #define AC_LOG(level, fmt, ...) \
        do { \
            if ((level) <= AC_LOG_LEVEL) { \
                printf("[AGENTC] " fmt "\n", ##__VA_ARGS__); \
            } \
        } while(0)
#endif

/* Only define these if not already defined by log.h */
#ifndef AC_LOG_ERROR
    #define AC_LOG_ERROR(fmt, ...) AC_LOG(1, "ERROR: " fmt, ##__VA_ARGS__)
#endif

#ifndef AC_LOG_WARN
    #define AC_LOG_WARN(fmt, ...)  AC_LOG(2, "WARN: " fmt, ##__VA_ARGS__)
#endif

#ifndef AC_LOG_INFO
    #define AC_LOG_INFO(fmt, ...)  AC_LOG(3, "INFO: " fmt, ##__VA_ARGS__)
#endif

#ifndef AC_LOG_DEBUG
    #define AC_LOG_DEBUG(fmt, ...) AC_LOG(4, "DEBUG: " fmt, ##__VA_ARGS__)
#endif

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

#endif /* AGENTC_PLATFORM_H */
