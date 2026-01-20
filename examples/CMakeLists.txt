# AgentC Examples

cmake_minimum_required(VERSION 3.14)

# Get cJSON include dir
set(CJSON_INCLUDE ${CMAKE_SOURCE_DIR}/external/cjson)

# Example 1: Simple chat demo with Markdown rendering
add_executable(chat_demo hosted/chat_demo.c)
target_link_libraries(chat_demo PRIVATE
    ac_core::ac_core
    agentc_markdown
    agentc_dotenv
    agentc_platform_init
)
target_include_directories(chat_demo PRIVATE
    ${CJSON_INCLUDE}
    ${CMAKE_SOURCE_DIR}/libs/ac_hosted/src
    ${CMAKE_SOURCE_DIR}/external/args/include
)
if(AGENTC_USE_CURL)
  target_link_libraries(chat_demo PRIVATE CURL::libcurl)
endif()

# Example 2: Tool calling / ReACT Agent demo
add_executable(chat_tools hosted/chat_tools.c)
target_link_libraries(chat_tools PRIVATE
    ac_core::ac_core
    agentc_dotenv
)
target_include_directories(chat_tools PRIVATE
    ${CJSON_INCLUDE}
)
if(NOT WIN32)
  target_link_libraries(chat_tools PRIVATE m)
endif()
if(AGENTC_USE_CURL)
  target_link_libraries(chat_tools PRIVATE CURL::libcurl)
endif()

# Example 3: Markdown rendering demo
add_executable(chat_markdown hosted/chat_markdown.c)
target_link_libraries(chat_markdown PRIVATE agentc_markdown)
target_include_directories(chat_markdown PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/ac_hosted/src
)

# Example 4: TUI chat demo with Notcurses (WeChat-style interface)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(NOTCURSES notcurses)
endif()

if(NOTCURSES_FOUND)
  add_executable(chat_tui hosted/chat_tui.c)
  target_link_libraries(chat_tui PRIVATE
        ac_core::ac_core
        agentc_dotenv
        ${NOTCURSES_LIBRARIES}
    )
  target_include_directories(chat_tui PRIVATE
        ${CJSON_INCLUDE}
        ${NOTCURSES_INCLUDE_DIRS}
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

message(STATUS "AgentC Examples: chat_demo, chat_tools, chat_markdown${NOTCURSES_FOUND}")
