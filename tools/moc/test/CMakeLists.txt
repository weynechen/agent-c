# MOC Test Suite
#
# This builds and runs tests for MOC-generated code

cmake_minimum_required(VERSION 3.16)

# Path to cJSON
set(CJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/cjson")

#============================================================================
# Generate Tool Code Using MOC
#============================================================================

# Define the generated files
set(TOOLS_GEN_H "${CMAKE_CURRENT_BINARY_DIR}/tools_gen.h")
set(TOOLS_GEN_C "${CMAKE_CURRENT_BINARY_DIR}/tools_gen.c")

# Add custom command to run MOC
add_custom_command(
    OUTPUT ${TOOLS_GEN_H} ${TOOLS_GEN_C}
    COMMAND moc -v -o "${CMAKE_CURRENT_BINARY_DIR}/tools_gen" 
            "${CMAKE_CURRENT_SOURCE_DIR}/sample_tools.h"
    DEPENDS moc "${CMAKE_CURRENT_SOURCE_DIR}/sample_tools.h"
    COMMENT "Generating tool wrappers with MOC"
    VERBATIM
)

# Custom target for generated files
add_custom_target(generate_test_tools
    DEPENDS ${TOOLS_GEN_H} ${TOOLS_GEN_C}
)

#============================================================================
# cJSON Library
#============================================================================

add_library(cjson_test STATIC
    ${CJSON_DIR}/cJSON.c
)

target_include_directories(cjson_test PUBLIC
    ${CJSON_DIR}
)

#============================================================================
# Test Executable
#============================================================================

add_executable(test_moc
    test_moc.c
    sample_tools.c
    ${TOOLS_GEN_C}
)

add_dependencies(test_moc generate_test_tools)

target_include_directories(test_moc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CJSON_DIR}
)

target_link_libraries(test_moc PRIVATE
    cjson_test
)

#============================================================================
# CTest Integration
#============================================================================

enable_testing()
add_test(NAME moc_test COMMAND test_moc)
