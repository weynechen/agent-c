/**
 * @file pthread_port.h
 * @brief POSIX Threads Portability Layer
 *
 * Provides pthread-compatible API across platforms:
 * - POSIX (Linux/macOS): Native pthread
 * - Windows: Thin wrapper over CRITICAL_SECTION/SRWLock
 * - FreeRTOS: Wrapper over FreeRTOS semaphores
 *
 * Usage:
 *   #include "pthread_port.h"
 *   pthread_mutex_t lock;
 *   pthread_mutex_init(&lock, NULL);
 *   pthread_mutex_lock(&lock);
 *   // critical section
 *   pthread_mutex_unlock(&lock);
 *   pthread_mutex_destroy(&lock);
 */

#ifndef AGENTC_PTHREAD_PORT_H
#define AGENTC_PTHREAD_PORT_H

#include "agentc/platform.h"

/*============================================================================
 * POSIX Platforms (Linux/macOS) - Native pthread
 *============================================================================*/

#if defined(AGENTC_PLATFORM_LINUX) || defined(AGENTC_PLATFORM_MACOS)

#include <pthread.h>

/* Native pthread - no wrapper needed */

/*============================================================================
 * Windows - pthread compatibility layer
 *============================================================================*/

#elif defined(AGENTC_PLATFORM_WINDOWS)

#include <windows.h>

/* Mutex type */
typedef CRITICAL_SECTION pthread_mutex_t;
typedef void* pthread_mutexattr_t;

/* Mutex functions */
static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    InitializeCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    return TryEnterCriticalSection(mutex) ? 0 : -1;
}

/*============================================================================
 * FreeRTOS - pthread compatibility layer
 *============================================================================*/

#elif defined(AGENTC_PLATFORM_FREERTOS)

#include "FreeRTOS.h"
#include "semphr.h"

/* Mutex type */
typedef SemaphoreHandle_t pthread_mutex_t;
typedef void* pthread_mutexattr_t;

/* Mutex functions */
static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    *mutex = xSemaphoreCreateMutex();
    return (*mutex != NULL) ? 0 : -1;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (*mutex) {
        vSemaphoreDelete(*mutex);
        *mutex = NULL;
    }
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    return (xSemaphoreTake(*mutex, portMAX_DELAY) == pdTRUE) ? 0 : -1;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    return (xSemaphoreGive(*mutex) == pdTRUE) ? 0 : -1;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    return (xSemaphoreTake(*mutex, 0) == pdTRUE) ? 0 : -1;
}

/*============================================================================
 * Fallback - Stub implementation (single-threaded)
 *============================================================================*/

#else

#warning "No pthread implementation for this platform, using stubs (not thread-safe)"

typedef int pthread_mutex_t;
typedef void* pthread_mutexattr_t;

static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    *mutex = 0;
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

#endif /* Platform selection */

#endif /* AGENTC_PTHREAD_PORT_H */
