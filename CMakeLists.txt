cmake_minimum_required(VERSION 3.14)
project(agentc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# Library sources
set(AGENTC_SOURCES
    src/agentc.c
    src/http_client.c
    src/llm.c
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
    # Simple command-line chat demo
    add_executable(chat_demo 
        examples/terminal/chat_demo.c
        ${DOTENV_SOURCES}
    )
    target_include_directories(chat_demo PRIVATE
        ${DOTENV_INCLUDE}
        ${CJSON_INCLUDE}
    )
    target_link_libraries(chat_demo PRIVATE agentc)
    if(AGENTC_USE_CURL)
        target_link_libraries(chat_demo PRIVATE CURL::libcurl)
    endif()
    
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
