cmake_minimum_required(VERSION 3.15)
project(code-agentc VERSION 0.1.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find AgentC
# Assuming agentc is built in ../../build
set(AGENTC_DIR "${CMAKE_SOURCE_DIR}/../..")
include_directories(
    ${AGENTC_DIR}/ac_core/include
    ${AGENTC_DIR}/ac_hosted/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link directories
link_directories(
    ${AGENTC_DIR}/build/ac_core
    ${AGENTC_DIR}/build/ac_hosted
)

# Source files
set(CODE_AGENTC_SOURCES
    # Core
    core/code_agentc.c
    core/rules.c
    core/skills.c
    core/mcp_client.c
    
    # Tools
    tools/tools.c
    tools/file_tools.c
    
    # Main
    main.c
)

# Executable
add_executable(code-agentc ${CODE_AGENTC_SOURCES})

# Link libraries
target_link_libraries(code-agentc
    agentc_core
    agentc_hosted
    curl
    pthread
    m
)

# Install
install(TARGETS code-agentc DESTINATION bin)

# Print configuration
message(STATUS "Code-AgentC Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  AgentC Dir: ${AGENTC_DIR}")
