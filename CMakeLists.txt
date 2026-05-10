cmake_minimum_required(VERSION 3.20)
project(Nav VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Core library
add_library(nav_core STATIC
    src/core/execution_context.cpp
    src/core/command.cpp
    src/core/toolchain.cpp
)

target_include_directories(nav_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# CLI executable
add_executable(nav
    src/cli/main.cpp
)

target_link_libraries(nav PRIVATE nav_core)
