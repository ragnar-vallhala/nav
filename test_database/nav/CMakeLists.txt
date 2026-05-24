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

# --- Packaging and Installation Setup ---

# 1. System-Wide Path Installation Target
install(TARGETS nav DESTINATION bin)

# 2. CPack Generator Config (For Debian Package Support)
set(CPACK_PACKAGE_NAME "nav")
set(CPACK_PACKAGE_VERSION "0.0.1")
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
