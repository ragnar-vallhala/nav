include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(nav_tests
    test_main.cpp
    test_mock_execution_context.cpp
    test_config.cpp
    test_toolchain.cpp
    test_create.cpp
    test_update.cpp
    test_execution_context.cpp
    test_ui.cpp
    test_board.cpp
    test_serial.cpp
    test_semver.cpp
    test_index.cpp
    test_lockfile.cpp
    test_resolver.cpp
    test_http_index.cpp
)

target_include_directories(nav_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nav_tests PRIVATE
    nav_core
    GTest::gtest
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(nav_tests)
