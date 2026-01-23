cmake_minimum_required(VERSION 3.15)
project(minimal_cli VERSION 0.1.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find AgentC (parent project root)
set(AGENTC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Include directories
include_directories(
    ${AGENTC_ROOT}/libs/ac_core/include
    ${AGENTC_ROOT}/libs/ac_hosted/include
    ${AGENTC_ROOT}/external/cjson
    ${AGENTC_ROOT}/external/dotenv
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link directories
link_directories(
    ${CMAKE_BINARY_DIR}/libs/ac_core
    ${CMAKE_BINARY_DIR}/libs/ac_hosted
)

# Source files
set(MINIMAL_CLI_SOURCES
    # Core
    core/minimal_cli.c
    
    # External dependencies
    ${AGENTC_ROOT}/external/dotenv/dotenv.c
    
    # Main
    main.c
)

# Executable
add_executable(minimal_cli ${MINIMAL_CLI_SOURCES})

# Link libraries
target_link_libraries(minimal_cli
    ac_core
    curl
    pthread
    m
)

# Compiler flags
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(minimal_cli PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

# Install
install(TARGETS minimal_cli DESTINATION bin)

# Print configuration
message(STATUS "Minimal CLI Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  AgentC Root: ${AGENTC_ROOT}")
