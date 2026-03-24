# =============================================================================
# icao::database unit tests
# =============================================================================
#
# Build standalone (from repo root):
#   cmake shared/lib/database -DBUILD_DATABASE_TESTS=ON
#   cmake --build .
#   ctest --output-on-failure
#
# Or enable from a parent CMakeLists.txt that already has icao-database as a
# sub-directory target:
#   add_subdirectory(shared/lib/database/tests)
# =============================================================================

cmake_minimum_required(VERSION 3.15)
project(icao-database-tests VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Guard: this file can be included from the parent database CMakeLists.txt
# (when BUILD_DATABASE_TESTS=ON is set on the outer cmake invocation) or
# built as a standalone project.  In the standalone case we bring in the
# icao-database library ourselves.
# ---------------------------------------------------------------------------
if(NOT TARGET icao-database)
    # Standalone build: pull in the parent library
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. icao-database-build)
endif()

# C++ standard must match the library (cxx_std_17 required by icao-database)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Google Test
# Try find_package first (vcpkg / system install), fallback to FetchContent
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
add_executable(icao_database_tests
    test_query_helpers.cpp
)

target_include_directories(icao_database_tests PRIVATE
    # Allow #include "query_helpers.h" directly (library uses flat layout)
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(icao_database_tests PRIVATE
    icao-database
    GTest::gtest_main
)

# Compiler warnings — same flags used by the library itself
if(NOT MSVC)
    target_compile_options(icao_database_tests PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ---------------------------------------------------------------------------
# CTest integration
# ---------------------------------------------------------------------------
enable_testing()
include(GoogleTest)
gtest_discover_tests(icao_database_tests)

message(STATUS "icao::database unit tests configured")
