cmake_minimum_required(VERSION 3.15)
project(arc_coder VERSION 0.1.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find ArC (parent project root)
set(ARC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Include directories
include_directories(
    ${ARC_ROOT}/libs/ac_core/include
    ${ARC_ROOT}/libs/ac_hosted/include
    ${ARC_ROOT}/libs/ac_hosted/src/sandbox
    ${ARC_ROOT}/external/cjson
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated files
)

# Windows: Add dirent compatibility layer
if(WIN32)
    include_directories(${ARC_ROOT}/external/dirent-windows)
endif()

#============================================================================
# Prompt Embedding - Convert .txt files to C strings
#============================================================================

# Include the prompt generation script
set(PROMPT_GEN_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_prompts.cmake")
set(PROMPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/prompts")
set(PROMPT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/prompts_gen.c")
set(PROMPT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/prompts_gen.h")

# Collect all prompt files
file(GLOB SYSTEM_PROMPT_FILES "${PROMPT_DIR}/system/*.txt")
file(GLOB TOOL_PROMPT_FILES "${PROMPT_DIR}/tools/*.txt")

# Custom command to generate prompt C files
add_custom_command(
    OUTPUT ${PROMPT_OUTPUT} ${PROMPT_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DPROMPT_DIR=${PROMPT_DIR}
        -DOUTPUT_C=${PROMPT_OUTPUT}
        -DOUTPUT_H=${PROMPT_HEADER}
        -P ${PROMPT_GEN_SCRIPT}
    DEPENDS
        ${PROMPT_GEN_SCRIPT}
        ${SYSTEM_PROMPT_FILES}
        ${TOOL_PROMPT_FILES}
    COMMENT "Generating embedded prompts from txt files"
    VERBATIM
)

# Custom target for prompt generation
add_custom_target(arc_coder_prompts DEPENDS ${PROMPT_OUTPUT} ${PROMPT_HEADER})

#============================================================================
# Determine Build Mode (Standalone vs Parent Project)
#============================================================================

if(TARGET moc)
    set(CODE_ARC_STANDALONE OFF)
    set(MOC_EXECUTABLE $<TARGET_FILE:moc>)
    message(STATUS "Code ArC: Building as part of main project")
else()
    set(CODE_ARC_STANDALONE ON)
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
        )
    endif()
    message(STATUS "Code ArC: Standalone build using MOC at ${MOC_EXECUTABLE}")
endif()

#============================================================================
# MOC Code Generation for Tools
#============================================================================

set(MOC_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/include/code_tools.h")
set(MOC_OUTPUT_BASE "${CMAKE_CURRENT_BINARY_DIR}/code_tools_gen")
set(MOC_OUTPUT_HEADER "${MOC_OUTPUT_BASE}.h")
set(MOC_OUTPUT_SOURCE "${MOC_OUTPUT_BASE}.c")

# In standalone mode, moc is an external executable, not a target
if(CODE_ARC_STANDALONE)
    add_custom_command(
        OUTPUT ${MOC_OUTPUT_HEADER} ${MOC_OUTPUT_SOURCE}
        COMMAND ${MOC_EXECUTABLE} ${MOC_INPUT} -o ${MOC_OUTPUT_BASE}
        DEPENDS ${MOC_INPUT}
        COMMENT "Running MOC on code_tools.h"
        VERBATIM
    )
    add_custom_target(arc_coder_moc DEPENDS ${MOC_OUTPUT_HEADER} ${MOC_OUTPUT_SOURCE})
else()
    # Building with parent project - moc is a target
    add_custom_command(
        OUTPUT ${MOC_OUTPUT_HEADER} ${MOC_OUTPUT_SOURCE}
        COMMAND ${MOC_EXECUTABLE} ${MOC_INPUT} -o ${MOC_OUTPUT_BASE}
        DEPENDS ${MOC_INPUT} moc
        COMMENT "Running MOC on code_tools.h"
        VERBATIM
    )
    add_custom_target(arc_coder_moc DEPENDS ${MOC_OUTPUT_HEADER} ${MOC_OUTPUT_SOURCE})
    add_dependencies(arc_coder_moc moc)
endif()

#============================================================================
# Source Files
#============================================================================

set(CODE_ARC_SOURCES
    # Core
    src/code_agent.c
    src/prompt_loader.c
    src/code_tools_enhanced.c

    # Tools
    src/tools/tool_bash.c
    src/tools/tool_read.c
    src/tools/tool_write.c
    src/tools/tool_edit.c
    src/tools/tool_ls.c
    src/tools/tool_grep.c

    # MOC-generated
    ${MOC_OUTPUT_SOURCE}

    # Prompt-generated
    ${PROMPT_OUTPUT}

    # Main
    main.c
)

#============================================================================
# Executable
#============================================================================

add_executable(arc_coder ${CODE_ARC_SOURCES})

# Ensure dependencies run before compilation
add_dependencies(arc_coder arc_coder_moc arc_coder_prompts)

#============================================================================
# Link Libraries
#============================================================================

if(CODE_ARC_STANDALONE)
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
    find_library(ARC_DOTENV_LIB arc_dotenv
        PATHS
            "${ARC_ROOT}/build/libs/ac_hosted"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../build/libs/ac_hosted"
        NO_DEFAULT_PATH
    )

    if(AC_CORE_LIB AND AC_HOSTED_LIB AND ARC_DOTENV_LIB)
        message(STATUS "Found ac_core: ${AC_CORE_LIB}")
        message(STATUS "Found ac_hosted: ${AC_HOSTED_LIB}")
        message(STATUS "Found arc_dotenv: ${ARC_DOTENV_LIB}")
        # Order matters: ac_hosted depends on ac_core and arc_dotenv
        target_link_libraries(arc_coder ${AC_HOSTED_LIB} ${ARC_DOTENV_LIB} ${AC_CORE_LIB})
    else()
        message(FATAL_ERROR
            "ac_core, ac_hosted or arc_dotenv not found!\n"
            "Build the main project first."
        )
    endif()
else()
    # Order matters: ac_hosted depends on ac_core
    target_link_libraries(arc_coder ac_hosted ac_core)
endif()

# Common libraries
target_link_libraries(arc_coder
    curl
    pthread
    m
)

#============================================================================
# Compiler Options
#============================================================================

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(arc_coder PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

#============================================================================
# Install
#============================================================================

install(TARGETS arc_coder DESTINATION bin)

#============================================================================
# Summary
#============================================================================

message(STATUS "Code ArC Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  ArC Root: ${ARC_ROOT}")
message(STATUS "  Standalone: ${CODE_ARC_STANDALONE}")
message(STATUS "  Prompt Dir: ${PROMPT_DIR}")
