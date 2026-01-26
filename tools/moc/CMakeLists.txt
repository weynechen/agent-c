cmake_minimum_required(VERSION 3.16)
project(tree-sitter-minimal C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define relative paths to tree-sitter and tree-sitter-c
set(TREE_SITTER_DIR "${CMAKE_SOURCE_DIR}/tree-sitter")
set(TREE_SITTER_C_DIR "${CMAKE_SOURCE_DIR}/tree-sitter-c")

# Build tree-sitter core library as a static library
add_library(tree-sitter STATIC
    ${TREE_SITTER_DIR}/lib/src/lib.c
)

target_include_directories(tree-sitter PUBLIC
    ${TREE_SITTER_DIR}/lib/include
)

target_include_directories(tree-sitter PRIVATE
    ${TREE_SITTER_DIR}/lib/src
)

# Build tree-sitter-c parser as a static library
add_library(tree-sitter-c STATIC
    ${TREE_SITTER_C_DIR}/src/parser.c
)

target_include_directories(tree-sitter-c PRIVATE
    ${TREE_SITTER_C_DIR}/src
)

target_link_libraries(tree-sitter-c PRIVATE tree-sitter)

# Build the example executable
add_executable(c_parser_example main.c)

target_include_directories(c_parser_example PRIVATE
    ${TREE_SITTER_DIR}/lib/include
    ${TREE_SITTER_C_DIR}/bindings/c
)

target_link_libraries(c_parser_example PRIVATE
    tree-sitter
    tree-sitter-c
)
