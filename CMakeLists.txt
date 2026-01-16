cmake_minimum_required(VERSION 3.14)
project(agentc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Options
option(AGENTC_BUILD_EXAMPLES "Build example programs" ON)
option(AGENTC_USE_CURL "Use libcurl backend (default on desktop)" ON)
option(AGENTC_USE_MONGOOSE "Use mongoose backend (for embedded)" OFF)

# Find dependencies
if(AGENTC_USE_CURL)
    find_package(CURL REQUIRED)
    add_definitions(-DAGENTC_HTTP_BACKEND_CURL=1)
endif()

# Third-party libraries
set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)

# cJSON
set(CJSON_SOURCES ${THIRDPARTY_DIR}/cjson/cJSON.c)
set(CJSON_INCLUDE ${THIRDPARTY_DIR}/cjson)

# dotenv
set(DOTENV_SOURCES ${THIRDPARTY_DIR}/dotenv/dotenv.c)
set(DOTENV_INCLUDE ${THIRDPARTY_DIR}/dotenv)

# PCRE2 (for Markdown parsing)
set(PCRE2_DIR ${THIRDPARTY_DIR}/pcre2/src)
set(PCRE2_INCLUDE ${PCRE2_DIR})
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

# Markdown rendering library
set(MARKDOWN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/render/markdown)
set(MARKDOWN_SOURCES
    ${MARKDOWN_DIR}/md_utils.c
    ${MARKDOWN_DIR}/md_parser.c
    ${MARKDOWN_DIR}/md_renderer.c
    ${MARKDOWN_DIR}/md_stream.c
)

# Library sources
set(AGENTC_SOURCES
    src/agentc.c
    src/http_client.c
    src/llm.c
    src/tool.c
    src/memory.c
    src/agent.c
    ${CJSON_SOURCES}
)

if(AGENTC_USE_CURL)
    list(APPEND AGENTC_SOURCES src/backend/curl_backend.c)
endif()

# Create library
add_library(agentc STATIC ${AGENTC_SOURCES})

target_include_directories(agentc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CJSON_INCLUDE}
)

if(AGENTC_USE_CURL)
    target_link_libraries(agentc PRIVATE CURL::libcurl)
endif()

# Examples
if(AGENTC_BUILD_EXAMPLES)
    # Simple command-line chat demo with Markdown rendering
    add_executable(chat_demo 
        examples/terminal/chat_demo.c
        ${DOTENV_SOURCES}
        ${MARKDOWN_SOURCES}
        ${PCRE2_SOURCES}
    )
    target_include_directories(chat_demo PRIVATE
        ${DOTENV_INCLUDE}
        ${CJSON_INCLUDE}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PCRE2_INCLUDE}
    )
    target_compile_definitions(chat_demo PRIVATE
        HAVE_CONFIG_H
        PCRE2_CODE_UNIT_WIDTH=8
    )
    target_link_libraries(chat_demo PRIVATE agentc)
    if(AGENTC_USE_CURL)
        target_link_libraries(chat_demo PRIVATE CURL::libcurl)
    endif()
    
    # Tool calling / ReACT Agent demo
    add_executable(chat_tools
        examples/terminal/chat_tools.c
        ${DOTENV_SOURCES}
    )
    target_include_directories(chat_tools PRIVATE
        ${DOTENV_INCLUDE}
        ${CJSON_INCLUDE}
    )
    if(NOT WIN32)
        target_link_libraries(chat_tools PRIVATE m)
    endif()
    target_link_libraries(chat_tools PRIVATE agentc)
    if(AGENTC_USE_CURL)
        target_link_libraries(chat_tools PRIVATE CURL::libcurl)
    endif()
    
    # Markdown rendering demo
    add_executable(chat_markdown
        examples/terminal/chat_markdown.c
        ${MARKDOWN_SOURCES}
        ${PCRE2_SOURCES}
    )
    target_include_directories(chat_markdown PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PCRE2_INCLUDE}
    )
    target_compile_definitions(chat_markdown PRIVATE
        HAVE_CONFIG_H
        PCRE2_CODE_UNIT_WIDTH=8
    )
    
    # TUI chat demo with Notcurses (WeChat-style interface)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(NOTCURSES notcurses)
    endif()
    
    if(NOTCURSES_FOUND)
        add_executable(chat_tui
            examples/terminal/chat_tui.c
            ${DOTENV_SOURCES}
        )
        target_include_directories(chat_tui PRIVATE
            ${DOTENV_INCLUDE}
            ${CJSON_INCLUDE}
            ${NOTCURSES_INCLUDE_DIRS}
        )
        target_link_libraries(chat_tui PRIVATE 
            agentc 
            ${NOTCURSES_LIBRARIES}
        )
        target_link_directories(chat_tui PRIVATE ${NOTCURSES_LIBRARY_DIRS})
        if(AGENTC_USE_CURL)
            target_link_libraries(chat_tui PRIVATE CURL::libcurl)
        endif()
        message(STATUS "Building chat_tui with Notcurses support")
    else()
        message(STATUS "Notcurses not found, skipping chat_tui build")
        message(STATUS "  Install with: sudo apt install libnotcurses-dev")
    endif()
endif()

# Install
install(TARGETS agentc
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
