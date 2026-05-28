cmake_minimum_required(VERSION 3.20)
project(Nav VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus cli11 nlohmann_json)

# --- Embedded project templates ---
# Each template is read into a CMake variable and inlined into a generated
# header via raw string literals (delimiter NAVTPL). Templates must NOT contain
# the literal sequence )NAVTPL" or the generated header will fail to parse.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/nav.toml.in"      NAV_TPL_TOML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/dot_config.in"    NAV_TPL_CONFIG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/main.c.in"        NAV_TPL_MAIN_C)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/cmakelists.txt.in" NAV_TPL_CMAKELISTS)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/embedded.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/nav/templates/embedded.hpp"
    @ONLY
)

# Core library
add_library(nav_core STATIC
    src/core/execution_context.cpp
    src/core/toolchain.cpp
    src/core/config.cpp
    src/core/project_name.cpp
    src/core/board.cpp
    src/core/serial.cpp
    src/core/semver.cpp
    src/core/index.cpp
    src/core/lockfile.cpp
    src/core/resolver.cpp
    src/core/commands/create.cpp
    src/core/commands/build.cpp
    src/core/commands/upload.cpp
    src/core/commands/monitor.cpp
    src/core/commands/clean.cpp
    src/core/commands/check.cpp
    src/core/commands/update.cpp
    src/core/commands/registry.cpp
    src/core/commands/board.cpp
)

target_include_directories(nav_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nav_core
    PUBLIC tomlplusplus::tomlplusplus
    PRIVATE nlohmann_json::nlohmann_json)

# CLI executable
add_executable(nav
    src/cli/main.cpp
)

target_link_libraries(nav PRIVATE nav_core CLI11::CLI11)
target_compile_definitions(nav PRIVATE NAV_VERSION="${PROJECT_VERSION}")

# --- Tests ---
option(NAV_BUILD_TESTS "Build the Nav test suite" OFF)
if(NAV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Packaging and Installation Setup ---

# 1. System-Wide Path Installation Target
install(TARGETS nav DESTINATION bin)

# 1a. Bundled board catalog.
install(DIRECTORY share/nav/boards/ DESTINATION share/nav/boards)

# 2. CPack Generator Config (For Debian Package Support)
set(CPACK_PACKAGE_NAME "nav")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "NAVROBOTEC PRIVATE LIMITED")
set(CPACK_PACKAGE_CONTACT "support@navrobotec.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Embedded Development Orchestrator for firmware and hardware deployment")

# Force DEB as main backend generator
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ragnar-vallhala <support@navrobotec.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "git, cmake, python3, build-essential")

include(CPack)
