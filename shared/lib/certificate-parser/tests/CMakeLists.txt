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
    test_pem_parser.cpp
    test_der_parser.cpp
    test_file_detector.cpp
    test_cert_type_detector.cpp
    test_cert_validator.cpp
)

# Create test executable
add_executable(icao_certificate_parser_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(icao_certificate_parser_tests
    PRIVATE
        icao-certificate-parser
        GTest::gtest_main
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Include directories
target_include_directories(icao_certificate_parser_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(icao_certificate_parser_tests)

# Add custom target for running tests
add_custom_target(run_certificate_parser_tests
    COMMAND icao_certificate_parser_tests
    DEPENDS icao_certificate_parser_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running certificate-parser unit tests..."
)
