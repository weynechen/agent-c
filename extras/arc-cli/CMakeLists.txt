cmake_minimum_required(VERSION 3.15)
project(minimal_cli VERSION 0.1.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find ArC (parent project root)
set(ARC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Include directories
include_directories(
    ${ARC_ROOT}/libs/ac_core/include
    ${ARC_ROOT}/libs/ac_hosted/include
    ${ARC_ROOT}/libs/ac_hosted/src/sandbox  # Sandbox internal headers
    ${ARC_ROOT}/external/cjson
    ${ARC_ROOT}/external/dotenv
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated files (tools_gen.h)
)

# Windows: Add dirent compatibility layer
if(WIN32)
    include_directories(${ARC_ROOT}/external/dirent-windows)
endif()

#============================================================================
# Determine Build Mode (Standalone vs Parent Project)
#============================================================================

# Check if we're being built as part of the main project
if(TARGET moc)
    set(MINIMAL_CLI_STANDALONE OFF)
    set(MOC_EXECUTABLE $<TARGET_FILE:moc>)
    message(STATUS "Minimal CLI: Building as part of main project")
else()
    set(MINIMAL_CLI_STANDALONE ON)
    # Look for pre-built MOC executable
    find_program(MOC_EXECUTABLE moc
        PATHS
            "${ARC_ROOT}/build/tools/moc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../build/tools/moc"
        NO_DEFAULT_PATH
    )

    if(NOT MOC_EXECUTABLE)
        message(FATAL_ERROR
            "MOC executable not found!\n"
            "Please build the main project first:\n"
            "  cd ${ARC_ROOT}\n"
            "  mkdir -p build && cd build\n"
            "  cmake .. -DARC_BUILD_TOOLS=ON\n"
            "  make moc\n"
            "Then return to build minimal_cli."
        )
    endif()
    message(STATUS "Minimal CLI: Standalone build using MOC at ${MOC_EXECUTABLE}")
endif()

#============================================================================
# MOC Code Generation
#============================================================================

# Input header file for MOC
set(MOC_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/include/builtin_tools.h")

# Output generated files
set(MOC_OUTPUT_BASE "${CMAKE_CURRENT_BINARY_DIR}/tools_gen")
set(MOC_OUTPUT_HEADER "${MOC_OUTPUT_BASE}.h")
set(MOC_OUTPUT_SOURCE "${MOC_OUTPUT_BASE}.c")

# Add custom command to run MOC
add_custom_command(
    OUTPUT ${MOC_OUTPUT_HEADER} ${MOC_OUTPUT_SOURCE}
    COMMAND ${MOC_EXECUTABLE} ${MOC_INPUT} -o ${MOC_OUTPUT_BASE}
    DEPENDS ${MOC_INPUT} moc
    COMMENT "Running MOC on builtin_tools.h"
    VERBATIM
)

# Custom target for MOC generation
add_custom_target(minimal_cli_moc DEPENDS ${MOC_OUTPUT_HEADER} ${MOC_OUTPUT_SOURCE})

# Add dependency on moc target only if building with parent project
if(NOT MINIMAL_CLI_STANDALONE)
    add_dependencies(minimal_cli_moc moc)
endif()

#============================================================================
# Source Files
#============================================================================

set(MINIMAL_CLI_SOURCES
    # Core
    core/minimal_cli.c

    # Tool implementations
    tools/builtin_tools.c

    # MOC-generated wrappers
    ${MOC_OUTPUT_SOURCE}

    # External dependencies
    ${ARC_ROOT}/external/dotenv/dotenv.c

    # Main
    main.c
)

#============================================================================
# Executable
#============================================================================

add_executable(minimal_cli ${MINIMAL_CLI_SOURCES})

# Ensure MOC runs before compilation
add_dependencies(minimal_cli minimal_cli_moc)

#============================================================================
# Link Libraries
#============================================================================

if(MINIMAL_CLI_STANDALONE)
    # Standalone build: need to find/build ac_core and ac_hosted
    # First try to find pre-built libraries
    find_library(AC_CORE_LIB ac_core
        PATHS
            "${ARC_ROOT}/build/libs/ac_core"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libs/ac_core"
        NO_DEFAULT_PATH
    )
    find_library(AC_HOSTED_LIB ac_hosted
        PATHS
            "${ARC_ROOT}/build/libs/ac_hosted"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libs/ac_hosted"
        NO_DEFAULT_PATH
    )

    if(AC_CORE_LIB AND AC_HOSTED_LIB)
        message(STATUS "Found ac_core library: ${AC_CORE_LIB}")
        message(STATUS "Found ac_hosted library: ${AC_HOSTED_LIB}")
        target_link_libraries(minimal_cli ${AC_CORE_LIB} ${AC_HOSTED_LIB})
    else()
        message(FATAL_ERROR
            "ac_core or ac_hosted library not found!\n"
            "Please build the main project first:\n"
            "  cd ${ARC_ROOT}\n"
            "  mkdir -p build && cd build\n"
            "  cmake ..\n"
            "  make ac_core ac_hosted\n"
            "Then return to build minimal_cli."
        )
    endif()
else()
    # Building with parent project
    target_link_libraries(minimal_cli ac_core ac_hosted)
endif()

# Common libraries
target_link_libraries(minimal_cli
    curl
    pthread
    m
)

#============================================================================
# Compiler Options
#============================================================================

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(minimal_cli PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

#============================================================================
# Install
#============================================================================

install(TARGETS minimal_cli DESTINATION bin)

#============================================================================
# Configuration Summary
#============================================================================

message(STATUS "Minimal CLI Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  ArC Root: ${ARC_ROOT}")
message(STATUS "  Standalone Build: ${MINIMAL_CLI_STANDALONE}")
message(STATUS "  MOC Executable: ${MOC_EXECUTABLE}")
message(STATUS "  MOC Input: ${MOC_INPUT}")
message(STATUS "  MOC Output: ${MOC_OUTPUT_BASE}.{h,c}")
