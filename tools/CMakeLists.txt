# ArC Development Tools

cmake_minimum_required(VERSION 3.14)

# MOC (Meta-Object Compiler)
# Parses AC_TOOL_META marked functions and generates wrapper code
add_subdirectory(moc)

message(STATUS "ArC Tools: MOC enabled")
