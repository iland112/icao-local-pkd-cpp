# =============================================================================
# icao::config unit tests
# =============================================================================
#
# Build standalone (from repo root):
#   cmake shared/lib/config -DBUILD_CONFIG_TESTS=ON
#   cmake --build .
#   ctest --output-on-failure
#
# Or, to include from a parent build that already has icao-config as a target:
#   add_subdirectory(shared/lib/config/tests)
# =============================================================================

cmake_minimum_required(VERSION 3.15)
project(icao-config-tests VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Guard: standalone vs. sub-directory build
# ---------------------------------------------------------------------------
if(NOT TARGET icao-config)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. icao-config-build)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Google Test
# ---------------------------------------------------------------------------
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.12.1
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
add_executable(icao_config_tests
    test_config_manager.cpp
)

target_include_directories(icao_config_tests PRIVATE
    # Flat layout: config_manager.h lives in shared/lib/config/
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(icao_config_tests PRIVATE
    icao-config
    GTest::gtest_main
)

if(NOT MSVC)
    target_compile_options(icao_config_tests PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ---------------------------------------------------------------------------
# CTest integration
# ---------------------------------------------------------------------------
enable_testing()
include(GoogleTest)
gtest_discover_tests(icao_config_tests)

message(STATUS "icao::config unit tests configured")
