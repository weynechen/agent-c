cmake_minimum_required(VERSION 3.14)
project(arc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Build profiles
set(ARC_PROFILE "hosted" CACHE STRING "Build profile: minimal, hosted, or embedded")
set_property(CACHE ARC_PROFILE PROPERTY STRINGS minimal hosted embedded)

# Build options
option(ARC_BUILD_TOOLS "Build development tools" OFF)
option(ARC_BUILD_EXAMPLES "Build example programs" ON)
option(ARC_BUILD_TESTS "Build unit tests" OFF)
option(ARC_BUILD_EXTRAS "Build extra applications" ON)

# Dependency management options
option(ARC_USE_SYSTEM_DEPS "Prefer system-installed dependencies (if OFF, will auto-download)" ON)
option(ARC_FORCE_LOCAL_DEPS "Force using FetchContent for all dependencies" OFF)

# Platform detection
if(UNIX AND NOT APPLE)
    set(ARC_PORT "posix" CACHE STRING "Port target: posix, windows, freertos")
elseif(WIN32)
    set(ARC_PORT "windows" CACHE STRING "Port target: posix, windows, freertos")
elseif(APPLE)
    set(ARC_PORT "posix" CACHE STRING "Port target: posix, windows, freertos")
else()
    set(ARC_PORT "freertos" CACHE STRING "Port target: posix, windows, freertos")
endif()

# HTTP backend selection
option(ARC_USE_CURL "Use libcurl backend (default on desktop)" ON)
option(ARC_USE_MONGOOSE "Use mongoose backend (for embedded)" OFF)

# FetchContent setup
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Find dependencies - Hybrid approach for curl
if(ARC_USE_CURL)
    set(CURL_FOUND FALSE)

    # Strategy 1: Try to find system curl (unless forced to use local)
    if(ARC_USE_SYSTEM_DEPS AND NOT ARC_FORCE_LOCAL_DEPS)
        find_package(CURL QUIET)
        if(CURL_FOUND)
            message(STATUS "Using system libcurl: ${CURL_LIBRARIES}")
        endif()
    endif()

    # Strategy 2: If not found or forced local, use FetchContent
    if(NOT CURL_FOUND OR ARC_FORCE_LOCAL_DEPS)
        message(STATUS "System libcurl not found, downloading via FetchContent...")
        FetchContent_Declare(
            curl
            URL https://github.com/curl/curl/releases/download/curl-8_18_0/curl-8.18.0.tar.gz
            URL_HASH SHA256=e9274a5f8ab5271c0e0e6762d2fce194d5f98acc568e4ce816845b2dcc0cf88f
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )

        # Configure curl options - minimal build
        set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

        # SSL backends
        set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
        set(CURL_USE_SCHANNEL ${WIN32} CACHE BOOL "" FORCE)
        set(CURL_USE_SECTRANSP ${APPLE} CACHE BOOL "" FORCE)
        set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
        set(CURL_USE_BEARSSL OFF CACHE BOOL "" FORCE)
        set(CURL_USE_WOLFSSL OFF CACHE BOOL "" FORCE)

        # Disable optional features
        set(HTTP_ONLY ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_SMB ON CACHE BOOL "" FORCE)

        # Disable compression and extra dependencies
        set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
        set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
        set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
        set(ENABLE_UNIX_SOCKETS OFF CACHE BOOL "" FORCE)

        # Disable HTTP/2 and HTTP/3
        set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
        set(USE_NGTCP2 OFF CACHE BOOL "" FORCE)
        set(USE_QUICHE OFF CACHE BOOL "" FORCE)

        # Disable IDN support
        set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)

        # Disable PSL (Public Suffix List) - this is causing the error
        set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

        # Disable docs and manual
        set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
        set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
        set(ENABLE_MANUAL OFF CACHE BOOL "" FORCE)

        # Disable IPv6 if needed (keep it ON for now)
        # set(ENABLE_IPV6 OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(curl)

        # Create alias for consistent usage
        if(NOT TARGET CURL::libcurl)
            if(TARGET libcurl_static)
                add_library(CURL::libcurl ALIAS libcurl_static)
                set(CURL_FOUND TRUE)
                message(STATUS "Built libcurl_static from source via FetchContent")
            elseif(TARGET libcurl)
                # Check if libcurl is already an ALIAS
                get_target_property(libcurl_aliased libcurl ALIASED_TARGET)
                if(libcurl_aliased)
                    # libcurl is an ALIAS, use it directly or find the real target
                    set(CURL_FOUND TRUE)
                    message(STATUS "Using libcurl (alias) from FetchContent")
                    # Create our alias pointing to the aliased target
                    add_library(CURL::libcurl ALIAS ${libcurl_aliased})
                else()
                    # libcurl is a real target, create alias
                    add_library(CURL::libcurl ALIAS libcurl)
                    set(CURL_FOUND TRUE)
                    message(STATUS "Built libcurl from source via FetchContent")
                endif()
            endif()
        else()
            set(CURL_FOUND TRUE)
            message(STATUS "CURL::libcurl target already exists")
        endif()
    endif()

    if(CURL_FOUND)
        add_definitions(-DARC_HTTP_BACKEND_CURL=1)
    else()
        message(FATAL_ERROR "Could not find or build libcurl")
    endif()
endif()

# Add subdirectories
add_subdirectory(libs/ac_core)

if(ARC_PROFILE STREQUAL "hosted")
    add_subdirectory(libs/ac_hosted)
    add_subdirectory(external/args)
endif()

# Always build tools if examples are enabled (MOC is needed for examples)
if(ARC_BUILD_TOOLS OR ARC_BUILD_EXAMPLES)
    add_subdirectory(tools)
endif()

if(ARC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ARC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(ARC_BUILD_EXTRAS AND ARC_PROFILE STREQUAL "hosted")
    add_subdirectory(extras/minimal_cli)
endif()

# Install
install(DIRECTORY libs/ac_core/include/ DESTINATION include)
if(ARC_PROFILE STREQUAL "hosted")
    install(DIRECTORY libs/ac_hosted/include/ DESTINATION include)
endif()
