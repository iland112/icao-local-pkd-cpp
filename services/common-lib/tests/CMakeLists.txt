cmake_minimum_required(VERSION 3.15)

# Find Google Test
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, downloading from GitHub...")

    # Download and build Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
endif()

# Enable testing
enable_testing()

# Test sources
set(TEST_SOURCES
    test_dn_parser.cpp
    test_dn_components.cpp
    test_certificate_parser.cpp
    test_metadata_extractor.cpp
    test_string_utils.cpp
    test_time_utils.cpp
)

# Create test executable
add_executable(icao_common_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(icao_common_tests
    PRIVATE
        icao-common
        GTest::gtest_main
        GTest::gmock
)

# Include directories
target_include_directories(icao_common_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(icao_common_tests)

# Add custom target for running tests
add_custom_target(run_tests
    COMMAND icao_common_tests
    DEPENDS icao_common_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests..."
)
