# External Args Component

cmake_minimum_required(VERSION 3.14)

# Platform wrapper library
add_library(agentc_platform_wrap
    src/platform_wrap.c
)

target_include_directories(agentc_platform_wrap PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Require C11 for platform detection
target_compile_features(agentc_platform_wrap PUBLIC c_std_11)

# Create an alias for consistent naming
add_library(agentc::platform_wrap ALIAS agentc_platform_wrap)

message(STATUS "AgentC external/args: platform_wrap library configured")
