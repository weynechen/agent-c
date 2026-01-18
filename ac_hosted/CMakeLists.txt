# AgentC Hosted Components
# Additional features for full OS environments (Linux/Windows/macOS)

cmake_minimum_required(VERSION 3.14)

# Hosted sources
set(AGENTC_HOSTED_SOURCES
    src/rules/rules.c
    src/skills/skills.c
    src/mcp/mcp.c
)

# Component: dotenv
set(DOTENV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/components/dotenv)
add_library(agentc_dotenv STATIC ${DOTENV_DIR}/dotenv.c)
target_include_directories(agentc_dotenv PUBLIC ${DOTENV_DIR})

# Component: Markdown renderer
set(MARKDOWN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/markdown)
set(MARKDOWN_SOURCES
    ${MARKDOWN_DIR}/md_utils.c
    ${MARKDOWN_DIR}/md_parser.c
    ${MARKDOWN_DIR}/md_renderer.c
    ${MARKDOWN_DIR}/md_stream.c
)

# PCRE2 dependency for Markdown
set(PCRE2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/components/pcre2/src)
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
    ${MARKDOWN_DIR}
    ${PCRE2_DIR}
)
target_compile_definitions(agentc_markdown PRIVATE
    HAVE_CONFIG_H
    PCRE2_CODE_UNIT_WIDTH=8
)

# Hosted library (combining all hosted features)
add_library(agentc_hosted STATIC ${AGENTC_HOSTED_SOURCES})
target_include_directories(agentc_hosted PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ac_core/include
)
target_link_libraries(agentc_hosted PUBLIC
    agentc_core
)

# Install libraries
install(TARGETS agentc_hosted agentc_dotenv agentc_markdown
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install headers
install(DIRECTORY include/agentc
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

message(STATUS "AgentC Hosted: rules, skills, mcp, markdown, dotenv built")
