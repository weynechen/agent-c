# AgentC Examples

cmake_minimum_required(VERSION 3.14)

# Get cJSON include dir
set(CJSON_INCLUDE ${CMAKE_SOURCE_DIR}/external/cjson)

# Example 0: Minimal hello world
add_executable(hello hosted/hello.c)
target_link_libraries(hello PRIVATE
    ac_core::ac_core
    agentc_dotenv
)
target_include_directories(hello PRIVATE
    ${CJSON_INCLUDE}
)
if(AGENTC_USE_CURL)
  target_link_libraries(hello PRIVATE CURL::libcurl)
endif()

# Example 1: Simple chat demo with Markdown rendering
add_executable(chat_demo hosted/chat_demo.c)
target_link_libraries(chat_demo PRIVATE
    ac_core::ac_core
    agentc_markdown
    agentc_dotenv
    agentc_platform_wrap
)
target_include_directories(chat_demo PRIVATE
    ${CJSON_INCLUDE}
    ${CMAKE_SOURCE_DIR}/libs/ac_hosted/src
    ${CMAKE_SOURCE_DIR}/external/args/include
)
if(AGENTC_USE_CURL)
  target_link_libraries(chat_demo PRIVATE CURL::libcurl)
endif()

# Example 2: Tool calling / ReACT Agent demo with MOC integration
# First, generate tool wrappers using MOC
set(DEMO_TOOLS_H ${CMAKE_CURRENT_SOURCE_DIR}/hosted/demo_tools.h)
set(DEMO_TOOLS_GEN_H ${CMAKE_CURRENT_BINARY_DIR}/demo_tools_gen.h)
set(DEMO_TOOLS_GEN_C ${CMAKE_CURRENT_BINARY_DIR}/demo_tools_gen.c)

add_custom_command(
    OUTPUT ${DEMO_TOOLS_GEN_H} ${DEMO_TOOLS_GEN_C}
    COMMAND moc -o "${CMAKE_CURRENT_BINARY_DIR}/demo_tools_gen" "${DEMO_TOOLS_H}"
    DEPENDS moc ${DEMO_TOOLS_H}
    COMMENT "Generating tool wrappers with MOC for chat_tools"
    VERBATIM
)

add_custom_target(chat_tools_gen DEPENDS ${DEMO_TOOLS_GEN_H} ${DEMO_TOOLS_GEN_C})

add_executable(chat_tools 
    hosted/chat_tools.c
    hosted/demo_tools.c
    ${DEMO_TOOLS_GEN_C}
)
add_dependencies(chat_tools chat_tools_gen)

target_link_libraries(chat_tools PRIVATE
    ac_core::ac_core
    agentc_dotenv
    agentc_platform_wrap
)
target_include_directories(chat_tools PRIVATE
    ${CJSON_INCLUDE}
    ${CMAKE_SOURCE_DIR}/external/args/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hosted
    ${CMAKE_CURRENT_BINARY_DIR}
)
if(NOT WIN32)
  target_link_libraries(chat_tools PRIVATE m)
endif()
if(AGENTC_USE_CURL)
  target_link_libraries(chat_tools PRIVATE CURL::libcurl)
endif()

# Example 3: MCP Integration demo
add_executable(chat_mcp 
    hosted/chat_mcp.c
    hosted/demo_tools.c
    ${DEMO_TOOLS_GEN_C}
)
add_dependencies(chat_mcp chat_tools_gen)

target_link_libraries(chat_mcp PRIVATE
    ac_core::ac_core
    agentc_dotenv
    agentc_platform_wrap
)
target_include_directories(chat_mcp PRIVATE
    ${CJSON_INCLUDE}
    ${CMAKE_SOURCE_DIR}/external/args/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hosted
    ${CMAKE_CURRENT_BINARY_DIR}
)
if(NOT WIN32)
  target_link_libraries(chat_mcp PRIVATE m)
endif()
if(AGENTC_USE_CURL)
  target_link_libraries(chat_mcp PRIVATE CURL::libcurl)
endif()

# Example 5: Markdown rendering demo
add_executable(chat_markdown hosted/chat_markdown.c)
target_link_libraries(chat_markdown PRIVATE
    ac_core::ac_core
    agentc_markdown
)
target_include_directories(chat_markdown PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/ac_hosted/src
)

# Example 6: TUI chat demo with Notcurses (WeChat-style interface)
# NOTE: chat_tui needs to be rewritten to use the new Session/Agent API
# Temporarily disabled until rewrite is complete
# find_package(PkgConfig QUIET)
# if(PkgConfig_FOUND)
#   pkg_check_modules(NOTCURSES notcurses)
# endif()
# 
# if(NOTCURSES_FOUND)
#   add_executable(chat_tui hosted/chat_tui.c)
#   ...
# endif()
message(STATUS "chat_tui: Temporarily disabled (needs API rewrite)")

message(STATUS "AgentC Examples: hello, chat_demo, chat_tools, chat_mcp, chat_markdown")
