# HTTP Backend Porting Guide

## Overview

The HTTP backend is platform-specific and needs to be implemented for each target platform. This directory contains HTTP implementations for different platforms.

## Platform Implementations

### POSIX (Linux/macOS)
**Location**: `posix/http/http_curl.c`
**Backend**: libcurl
**Status**: ✅ Complete

Uses the standard libcurl library for HTTP/HTTPS communication.

**Dependencies**:
- libcurl (system package)

### Windows
**Location**: `windows/http/http_curl.c` or `windows/http/http_winhttp.c`
**Backend**: libcurl or WinHTTP
**Status**: ⏳ TODO

Options:
1. Use libcurl (same as POSIX)
2. Use native WinHTTP API

### FreeRTOS/Embedded
**Location**: `freertos/http/http_lwip.c` or custom
**Backend**: lwIP, FreeRTOS+TCP, or custom
**Status**: ⏳ TODO

Options:
1. lwIP HTTP client
2. FreeRTOS+TCP with mbedTLS
3. Custom minimal implementation
4. Mongoose embedded library

## HTTP Backend Interface

All backends must implement these functions from `agentc/http_client.h`:

### Global Initialization
```c
agentc_err_t agentc_http_init(void);
void agentc_http_cleanup(void);
```

### Client Management
```c
agentc_err_t agentc_http_client_create(
    const agentc_http_client_config_t *config,
    agentc_http_client_t **out
);

void agentc_http_client_destroy(agentc_http_client_t *client);
```

### HTTP Requests
```c
agentc_err_t agentc_http_request(
    agentc_http_client_t *client,
    const agentc_http_request_t *request,
    agentc_http_response_t *response
);

agentc_err_t agentc_http_request_stream(
    agentc_http_client_t *client,
    const agentc_http_stream_request_t *request,
    agentc_http_response_t *response
);
```

## Implementation Guide

### 1. Choose Your Backend

For your platform, select an appropriate HTTP library:
- **Full OS**: libcurl, WinHTTP, system HTTP client
- **RTOS**: lwIP, Mongoose, mbedTLS
- **Bare metal**: Custom implementation or lightweight library

### 2. Implement Required Functions

Create a file `port/<your-platform>/http/http_<backend>.c`:

```c
#include <agentc/http_client.h>
#include <agentc/log.h>

/* Your platform's HTTP library */
#include <your_http_library.h>

/* Internal client structure */
struct agentc_http_client {
    /* Platform-specific handle */
    void *native_handle;
    agentc_http_client_config_t config;
};

/* Implement all required functions */
agentc_err_t agentc_http_init(void) {
    /* Initialize your HTTP backend */
    AC_LOG_DEBUG("HTTP backend initialized (<platform>)");
    return AGENTC_OK;
}

/* ... implement other functions ... */
```

### 3. Handle Platform-Specific Features

#### SSL/TLS
- POSIX: libcurl handles SSL automatically
- Embedded: May need mbedTLS or similar
- Windows: WinHTTP or Schannel

#### Timeouts
- Implement proper timeout handling
- Map to platform-specific timeout mechanisms

#### Error Handling
- Map platform errors to agentc error codes:
  - `AGENTC_ERR_NETWORK`: General network error
  - `AGENTC_ERR_TIMEOUT`: Request timeout
  - `AGENTC_ERR_DNS`: DNS resolution failed
  - `AGENTC_ERR_TLS`: SSL/TLS error

### 4. Update Build System

Update `ac_core/CMakeLists.txt`:

```cmake
# HTTP backend sources
if(UNIX)
    list(APPEND AGENTC_CORE_SOURCES port/posix/http/http_curl.c)
elseif(WIN32)
    list(APPEND AGENTC_CORE_SOURCES port/windows/http/http_curl.c)
elseif(FREERTOS)
    list(APPEND AGENTC_CORE_SOURCES port/freertos/http/http_lwip.c)
endif()
```

## Testing

Test your HTTP backend with:

```c
#include <agentc/http_client.h>

int main() {
    /* Initialize */
    agentc_http_init();
    
    /* Create client */
    agentc_http_client_t *client;
    agentc_http_client_create(NULL, &client);
    
    /* Test request */
    agentc_http_request_t req = {
        .method = AGENTC_HTTP_GET,
        .url = "https://httpbin.org/get",
        .timeout_ms = 10000,
    };
    
    agentc_http_response_t resp;
    agentc_err_t err = agentc_http_request(client, &req, &resp);
    
    if (err == AGENTC_OK) {
        printf("Status: %d\n", resp.status_code);
        printf("Body: %s\n", resp.body);
        agentc_http_response_free(&resp);
    }
    
    /* Cleanup */
    agentc_http_client_destroy(client);
    agentc_http_cleanup();
    
    return 0;
}
```

## Minimal Implementation

For resource-constrained platforms, you can implement a minimal HTTP backend that only supports:
- HTTP (not HTTPS)
- GET and POST only
- No streaming
- Basic error handling

This is sufficient for many embedded use cases.

## Example: FreeRTOS with lwIP

```c
/* port/freertos/http/http_lwip.c */
#include <agentc/http_client.h>
#include "lwip/tcp.h"
#include "lwip/dns.h"

struct agentc_http_client {
    struct tcp_pcb *pcb;
    /* ... */
};

agentc_err_t agentc_http_init(void) {
    /* lwIP is already initialized by FreeRTOS */
    return AGENTC_OK;
}

/* Implement other functions using lwIP API */
```

## Reference Implementations

1. **libcurl** (POSIX): `port/posix/http/http_curl.c`
2. **WinHTTP** (Windows): TODO
3. **lwIP** (FreeRTOS): TODO
4. **Mongoose** (Embedded): TODO

## Questions?

See the full HTTP client API documentation in `include/agentc/http_client.h`.
