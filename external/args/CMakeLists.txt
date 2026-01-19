# Platform initialization and argument parsing utilities for hosted environments

cmake_minimum_required(VERSION 3.14)

# Platform initialization library
add_library(agentc_platform_init
    src/platform_init.c
)

target_include_directories(agentc_platform_init PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set C standard
target_compile_features(agentc_platform_init PUBLIC c_std_11)

# Add namespace alias
add_library(agentc::platform_init ALIAS agentc_platform_init)

message(STATUS "AgentC external/args: platform_init library configured")
