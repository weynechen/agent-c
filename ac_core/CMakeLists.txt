# AgentC Core Library
# This is the minimal, platform-independent core

cmake_minimum_required(VERSION 3.14)

# Core sources (platform-independent)
set(AGENTC_CORE_SOURCES
    src/agentc.c
    src/agent.c
    src/llm/llm.c
    src/llm/openai_api.c
    src/llm/anthropic_api.c
    src/tool.c
    src/memory.c
    src/http_client.c
    src/log.c
)

# Platform-specific port layer (log + HTTP backend)
if(AGENTC_PORT STREQUAL "posix")
    list(APPEND AGENTC_CORE_SOURCES 
        port/posix/log_posix.c
        port/posix/http/http_curl.c
    )
elseif(AGENTC_PORT STREQUAL "windows")
    list(APPEND AGENTC_CORE_SOURCES 
        port/windows/log_windows.c
        port/windows/http/http_curl.c  # Or http_winhttp.c
    )
elseif(AGENTC_PORT STREQUAL "freertos")
    list(APPEND AGENTC_CORE_SOURCES 
        port/freertos/log_freertos.c
        port/freertos/http/http_lwip.c  # Or custom implementation
    )
endif()

# Custom providers (if any)
file(GLOB CUSTOM_PROVIDER_SOURCES src/llm/providers/*.c)
if(CUSTOM_PROVIDER_SOURCES)
    list(APPEND AGENTC_CORE_SOURCES ${CUSTOM_PROVIDER_SOURCES})
    message(STATUS "Found custom LLM providers: ${CUSTOM_PROVIDER_SOURCES}")
endif()

# Built-in dependencies (cJSON)
set(CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/components/cjson)

list(APPEND AGENTC_CORE_SOURCES ${CJSON_DIR}/cJSON.c)

# Create static library
add_library(agentc_core STATIC ${AGENTC_CORE_SOURCES})

# Include directories
target_include_directories(agentc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CJSON_DIR}
)

# Private includes (for internal headers)
target_include_directories(agentc_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/llm
)

# Link dependencies
if(AGENTC_USE_CURL)
    target_link_libraries(agentc_core PRIVATE CURL::libcurl)
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(agentc_core PRIVATE pthread m)
elseif(APPLE)
    target_link_libraries(agentc_core PRIVATE pthread)
elseif(WIN32)
    target_link_libraries(agentc_core PRIVATE ws2_32)
endif()

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agentc_core PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-unused-function
    )
endif()

# Install
install(TARGETS agentc_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

message(STATUS "AgentC Core: profile=${AGENTC_PROFILE}, port=${AGENTC_PORT}")
