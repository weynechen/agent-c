# External Args Component

cmake_minimum_required(VERSION 3.14)

# Platform wrapper library
add_library(arc_platform_wrap
    src/platform_wrap.c
)

target_include_directories(arc_platform_wrap PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Require C11 for platform detection
target_compile_features(arc_platform_wrap PUBLIC c_std_11)

# Create an alias for consistent naming
add_library(arc::platform_wrap ALIAS arc_platform_wrap)

message(STATUS "ArC external/args: platform_wrap library configured")
