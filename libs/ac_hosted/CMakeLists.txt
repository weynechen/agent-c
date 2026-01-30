# AgentC Hosted Components
# Additional features for full OS environments (Linux/Windows/macOS)

cmake_minimum_required(VERSION 3.14)

#============================================================================
# Platform Detection for Sandbox
#============================================================================

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(AGENTC_SANDBOX_PLATFORM "linux")
    set(AGENTC_SANDBOX_SOURCE src/sandbox/sandbox_linux.c)
    message(STATUS "Sandbox: Linux platform detected (Landlock+Seccomp)")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(AGENTC_SANDBOX_PLATFORM "macos")
    set(AGENTC_SANDBOX_SOURCE src/sandbox/sandbox_macos.c)
    message(STATUS "Sandbox: macOS platform detected (Seatbelt)")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(AGENTC_SANDBOX_PLATFORM "windows")
    set(AGENTC_SANDBOX_SOURCE src/sandbox/sandbox_fallback.c)
    message(STATUS "Sandbox: Windows platform detected (Software filtering)")
else()
    set(AGENTC_SANDBOX_PLATFORM "fallback")
    set(AGENTC_SANDBOX_SOURCE src/sandbox/sandbox_fallback.c)
    message(STATUS "Sandbox: Unknown platform (Software filtering)")
endif()

# Hosted sources
# Note: mcp and tools have been moved to ac_core
# skills will be redesigned separately
set(AGENTC_HOSTED_SOURCES
    src/rules/rules.c
    src/sandbox/sandbox_common.c
    ${AGENTC_SANDBOX_SOURCE}
    src/trace/trace_json_exporter.c
    src/http_pool/http_pool.c
)

# Component: dotenv
set(DOTENV_DIR ${CMAKE_SOURCE_DIR}/external/dotenv)
add_library(agentc_dotenv STATIC ${DOTENV_DIR}/dotenv.c)
target_include_directories(agentc_dotenv PUBLIC
    $<BUILD_INTERFACE:${DOTENV_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Component: Markdown renderer
set(MARKDOWN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/markdown)
set(MARKDOWN_SOURCES
    ${MARKDOWN_DIR}/md_utils.c
    ${MARKDOWN_DIR}/md_parser.c
    ${MARKDOWN_DIR}/md_renderer.c
    ${MARKDOWN_DIR}/md_stream.c
)

# PCRE2 dependency for Markdown
set(PCRE2_DIR ${CMAKE_SOURCE_DIR}/external/pcre2/src)
set(PCRE2_SOURCES
    ${PCRE2_DIR}/pcre2_auto_possess.c
    ${PCRE2_DIR}/pcre2_chartables.c
    ${PCRE2_DIR}/pcre2_chkdint.c
    ${PCRE2_DIR}/pcre2_compile.c
    ${PCRE2_DIR}/pcre2_compile_cgroup.c
    ${PCRE2_DIR}/pcre2_compile_class.c
    ${PCRE2_DIR}/pcre2_config.c
    ${PCRE2_DIR}/pcre2_context.c
    ${PCRE2_DIR}/pcre2_convert.c
    ${PCRE2_DIR}/pcre2_dfa_match.c
    ${PCRE2_DIR}/pcre2_error.c
    ${PCRE2_DIR}/pcre2_extuni.c
    ${PCRE2_DIR}/pcre2_find_bracket.c
    ${PCRE2_DIR}/pcre2_jit_compile.c
    ${PCRE2_DIR}/pcre2_maketables.c
    ${PCRE2_DIR}/pcre2_match.c
    ${PCRE2_DIR}/pcre2_match_data.c
    ${PCRE2_DIR}/pcre2_match_next.c
    ${PCRE2_DIR}/pcre2_newline.c
    ${PCRE2_DIR}/pcre2_ord2utf.c
    ${PCRE2_DIR}/pcre2_pattern_info.c
    ${PCRE2_DIR}/pcre2_script_run.c
    ${PCRE2_DIR}/pcre2_serialize.c
    ${PCRE2_DIR}/pcre2_string_utils.c
    ${PCRE2_DIR}/pcre2_study.c
    ${PCRE2_DIR}/pcre2_substitute.c
    ${PCRE2_DIR}/pcre2_substring.c
    ${PCRE2_DIR}/pcre2_tables.c
    ${PCRE2_DIR}/pcre2_ucd.c
    ${PCRE2_DIR}/pcre2_valid_utf.c
    ${PCRE2_DIR}/pcre2_xclass.c
)

add_library(agentc_markdown STATIC ${MARKDOWN_SOURCES} ${PCRE2_SOURCES})
target_include_directories(agentc_markdown PUBLIC
    $<BUILD_INTERFACE:${MARKDOWN_DIR}>
    $<BUILD_INTERFACE:${PCRE2_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libs/ac_core/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(agentc_markdown PUBLIC
    ac_core::ac_core
)
target_compile_definitions(agentc_markdown PRIVATE
    HAVE_CONFIG_H
    PCRE2_CODE_UNIT_WIDTH=8
)

# CRITICAL: Define PCRE2_STATIC for static linking on Windows
# This must be PUBLIC so that consuming targets also see it
target_compile_definitions(agentc_markdown PUBLIC
    PCRE2_STATIC
)

# Hosted library (combining all hosted features)
add_library(ac_hosted STATIC ${AGENTC_HOSTED_SOURCES})
add_library(ac_hosted::ac_hosted ALIAS ac_hosted)

target_include_directories(ac_hosted PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Private includes for accessing ac_core port layer and sandbox internals
target_include_directories(ac_hosted PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/ac_core/port    # HTTP client and other port abstractions
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sandbox  # Sandbox internal headers
)

# Windows: Add dirent compatibility layer
if(WIN32)
    target_include_directories(ac_hosted PRIVATE
        ${CMAKE_SOURCE_DIR}/external/dirent-windows
    )
endif()

target_link_libraries(ac_hosted PUBLIC
    ac_core::ac_core
)

# Install libraries
install(TARGETS ac_hosted agentc_dotenv agentc_markdown
    EXPORT ac_hosted-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(EXPORT ac_hosted-targets
    FILE ac_hostedTargets.cmake
    NAMESPACE ac_hosted::
    DESTINATION lib/cmake/ac_hosted
)

# Install headers
install(DIRECTORY include/agentc
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

message(STATUS "AgentC Hosted: rules, sandbox (${AGENTC_SANDBOX_PLATFORM}), markdown, dotenv built")
