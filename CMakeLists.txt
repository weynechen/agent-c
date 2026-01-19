cmake_minimum_required(VERSION 3.14)
project(agentc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Build profiles
set(AGENTC_PROFILE "hosted" CACHE STRING "Build profile: minimal, hosted, or embedded")
set_property(CACHE AGENTC_PROFILE PROPERTY STRINGS minimal hosted embedded)

# Build options
option(AGENTC_BUILD_TOOLS "Build development tools" OFF)
option(AGENTC_BUILD_EXAMPLES "Build example programs" ON)
option(AGENTC_BUILD_TESTS "Build unit tests" OFF)

# Platform detection
if(UNIX AND NOT APPLE)
    set(AGENTC_PORT "posix" CACHE STRING "Port target: posix, windows, freertos")
elseif(WIN32)
    set(AGENTC_PORT "windows" CACHE STRING "Port target: posix, windows, freertos")
elseif(APPLE)
    set(AGENTC_PORT "posix" CACHE STRING "Port target: posix, windows, freertos")
else()
    set(AGENTC_PORT "freertos" CACHE STRING "Port target: posix, windows, freertos")
endif()

# HTTP backend selection
option(AGENTC_USE_CURL "Use libcurl backend (default on desktop)" ON)
option(AGENTC_USE_MONGOOSE "Use mongoose backend (for embedded)" OFF)

# Find dependencies
if(AGENTC_USE_CURL)
    find_package(CURL REQUIRED)
    add_definitions(-DAGENTC_HTTP_BACKEND_CURL=1)
endif()

# Add subdirectories
add_subdirectory(libs/ac_core)

if(AGENTC_PROFILE STREQUAL "hosted")
    add_subdirectory(libs/ac_hosted)
    add_subdirectory(extras/args)
endif()

if(AGENTC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(AGENTC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(AGENTC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Install
install(DIRECTORY libs/ac_core/include/ DESTINATION include)
if(AGENTC_PROFILE STREQUAL "hosted")
    install(DIRECTORY libs/ac_hosted/include/ DESTINATION include)
endif()
