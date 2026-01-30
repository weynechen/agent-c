# MOC (Meta-Object Compiler) for ArC
#
# This tool parses C header files with AC_TOOL_META markers and generates
# wrapper functions and tool registration code.

cmake_minimum_required(VERSION 3.16)
project(moc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define relative paths to tree-sitter and tree-sitter-c
set(TREE_SITTER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter")
set(TREE_SITTER_C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-c")

#============================================================================
# Tree-sitter Core Library
#============================================================================

add_library(tree-sitter STATIC
    ${TREE_SITTER_DIR}/lib/src/lib.c
)

target_include_directories(tree-sitter PUBLIC
    ${TREE_SITTER_DIR}/lib/include
)

target_include_directories(tree-sitter PRIVATE
    ${TREE_SITTER_DIR}/lib/src
)

#============================================================================
# Tree-sitter C Language Parser
#============================================================================

add_library(tree-sitter-c STATIC
    ${TREE_SITTER_C_DIR}/src/parser.c
)

target_include_directories(tree-sitter-c PRIVATE
    ${TREE_SITTER_C_DIR}/src
)

target_link_libraries(tree-sitter-c PRIVATE tree-sitter)

#============================================================================
# MOC Executable
#============================================================================

add_executable(moc
    main.c
    moc_parser.c
    moc_comment.c
    moc_codegen.c
    moc_utils.c
)

target_include_directories(moc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TREE_SITTER_DIR}/lib/include
    ${TREE_SITTER_C_DIR}/bindings/c
)

target_link_libraries(moc PRIVATE
    tree-sitter
    tree-sitter-c
)

#============================================================================
# Installation
#============================================================================

install(TARGETS moc
    RUNTIME DESTINATION bin
)

#============================================================================
# Test Suite
#============================================================================

option(MOC_BUILD_TESTS "Build MOC tests" ON)

if(MOC_BUILD_TESTS)
    add_subdirectory(test)
endif()

#============================================================================
# Helper Function for Using MOC in Other Projects
#============================================================================

# This function can be used by other CMakeLists.txt to generate tool code:
#
# Usage:
#   moc_generate_tools(
#       INPUT tools.h
#       OUTPUT_NAME tools_gen
#       OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
#   )
#
# This will:
#   1. Run moc on tools.h
#   2. Generate tools_gen.h and tools_gen.c in OUTPUT_DIR
#   3. Return the generated source file path in MOC_GENERATED_SOURCES
#
function(moc_generate_tools)
    cmake_parse_arguments(MOC "" "INPUT;OUTPUT_NAME;OUTPUT_DIR" "" ${ARGN})

    if(NOT MOC_INPUT)
        message(FATAL_ERROR "moc_generate_tools: INPUT is required")
    endif()

    if(NOT MOC_OUTPUT_NAME)
        set(MOC_OUTPUT_NAME "tools_gen")
    endif()

    if(NOT MOC_OUTPUT_DIR)
        set(MOC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    set(MOC_OUTPUT_H "${MOC_OUTPUT_DIR}/${MOC_OUTPUT_NAME}.h")
    set(MOC_OUTPUT_C "${MOC_OUTPUT_DIR}/${MOC_OUTPUT_NAME}.c")

    add_custom_command(
        OUTPUT ${MOC_OUTPUT_H} ${MOC_OUTPUT_C}
        COMMAND moc -o "${MOC_OUTPUT_DIR}/${MOC_OUTPUT_NAME}" "${MOC_INPUT}"
        DEPENDS moc ${MOC_INPUT}
        COMMENT "Running MOC on ${MOC_INPUT}"
        VERBATIM
    )

    set(MOC_GENERATED_SOURCES ${MOC_OUTPUT_C} PARENT_SCOPE)
    set(MOC_GENERATED_HEADERS ${MOC_OUTPUT_H} PARENT_SCOPE)
endfunction()
