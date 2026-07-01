cmake_minimum_required(VERSION 3.20)
project(Nav VERSION 1.1.0 LANGUAGES CXX)

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
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/nav_lib.toml.in"   NAV_TPL_TOML_LIB)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/cmakelists_lib.txt.in" NAV_TPL_CMAKELISTS_LIB)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/lib_header.h.in"   NAV_TPL_LIB_HEADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/templates/lib_source.c.in"   NAV_TPL_LIB_SOURCE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/embedded.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/nav/templates/embedded.hpp"
    @ONLY
)

# --- Embedded tool/board registry ---
# data/*.json is the single source of truth for the toolchain + board registry.
# It is baked into the binary (delimiter NAVCFG) so nav needs no external file;
# on first run nav also writes it to ~/.nav/ for the user to extend. The JSON
# must NOT contain the literal sequence )NAVCFG".
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/toolchain.json" NAV_CFG_TOOLCHAIN)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/boards.json"    NAV_CFG_BOARDS)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/embedded_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/nav/config/embedded_config.hpp"
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
    src/core/platform.cpp
    src/core/cache.cpp
    src/core/navhal.cpp
    src/core/registry.cpp
    src/core/provision.cpp
    src/core/deps.cpp
    src/core/libdeps.cpp
    src/core/navconfig.cpp
    src/core/commands/create.cpp
    src/core/commands/build.cpp
    src/core/commands/upload.cpp
    src/core/commands/monitor.cpp
    src/core/commands/clean.cpp
    src/core/commands/check.cpp
    src/core/commands/update.cpp
    src/core/commands/board.cpp
    src/core/commands/lib.cpp
    src/core/commands/config.cpp
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

# On Windows (MinGW) link the C/C++/pthread runtimes statically so the shipped
# nav.exe is fully self-contained — no MSYS2/MinGW DLLs need to travel with it.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(nav PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# --- Tests ---
option(NAV_BUILD_TESTS "Build the Nav test suite" OFF)
if(NAV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Packaging and Installation Setup ---

# 1. System-Wide Path Installation Target
install(TARGETS nav DESTINATION bin)

# 1a. Bundled glyph font. Shipped with the app (not installed system-wide) so a
# user whose terminal lacks the box-drawing/✔ glyphs can install it themselves;
# nav falls back to plain output at runtime when such glyphs aren't supported.
install(DIRECTORY share/nav/fonts/ DESTINATION share/nav/fonts)

# 2. CPack metadata (shared across all generators).
set(CPACK_PACKAGE_NAME "nav")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "NAVROBOTEC PRIVATE LIMITED")
set(CPACK_PACKAGE_CONTACT "support@navrobotec.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Embedded Development Orchestrator for firmware and hardware deployment")

if(WIN32)
    # Windows analogue of the .deb: an NSIS Setup.exe that installs nav.exe +
    # the board catalog, registers an uninstaller, and (opt-in) prepends the
    # install bin dir to PATH. A portable ZIP is produced alongside it.
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "nav")
    set(CPACK_NSIS_PACKAGE_NAME "Nav ${PROJECT_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME "Nav — Embedded Development Orchestrator")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/ragnar-vallhala/nav")
    set(CPACK_NSIS_CONTACT "support@navrobotec.com")
    # Always add nav to the system (all-users) PATH — including silent (/S) and
    # blind "click-through" installs — instead of CPack's opt-in PATH page that
    # defaults to off. A bundled helper does the edit (idempotent, broadcasts the
    # change); the uninstaller removes it. CPACK_NSIS_MODIFY_PATH is left off so
    # there's no conflicting/confusing PATH page.
    set(CPACK_NSIS_MODIFY_PATH OFF)
    install(FILES scripts/windows/AddToPath.ps1 DESTINATION .)
    # Call the helper by relative name from $INSTDIR (SetOutPath makes that the
    # working dir) so the NSIS command carries no backslashes or quotes — both of
    # which corrupt the CPackConfig.cmake round-trip.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
"SetOutPath $INSTDIR
  nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File AddToPath.ps1'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
"SetOutPath $INSTDIR
  nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File AddToPath.ps1 -Remove'")
else()
    # Debian/Ubuntu package.
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ragnar-vallhala <support@navrobotec.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "git, cmake, python3, build-essential")
endif()

include(CPack)
