cmake_minimum_required(VERSION 3.15)
project(icao-validation VERSION 1.0.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(OpenSSL REQUIRED)

# Library source files
set(ICAO_VALIDATION_SOURCES
    src/cert_ops.cpp
    src/extension_validator.cpp
    src/algorithm_compliance.cpp
    src/trust_chain_builder.cpp
    src/crl_checker.cpp
)

# Create static library
add_library(icao-validation STATIC ${ICAO_VALIDATION_SOURCES})

# Set library alias for consistent naming
add_library(icao::validation ALIAS icao-validation)

# Include directories
target_include_directories(icao-validation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link OpenSSL
target_link_libraries(icao-validation
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Compiler warnings
if(MSVC)
    target_compile_options(icao-validation PRIVATE /W4)
else()
    target_compile_options(icao-validation PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS icao-validation
    EXPORT icao-validation-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT icao-validation-targets
    FILE icao-validation-targets.cmake
    NAMESPACE icao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-validation
)

# =============================================================================
# Testing (Optional)
# =============================================================================
option(BUILD_VALIDATION_TESTS "Build icao::validation unit tests" OFF)

if(BUILD_VALIDATION_TESTS)
    enable_testing()

    # Try find_package first (vcpkg), fallback to FetchContent
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    set(TEST_SOURCES
        tests/test_cert_ops.cpp
        tests/test_extension_validator.cpp
        tests/test_algorithm_compliance.cpp
        tests/test_trust_chain_builder.cpp
        tests/test_crl_checker.cpp
    )

    add_executable(icao_validation_tests ${TEST_SOURCES})

    target_include_directories(icao_validation_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(icao_validation_tests PRIVATE
        icao-validation
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(icao_validation_tests)

    message(STATUS "icao::validation unit tests enabled")
endif()

# Print configuration summary
message(STATUS "=================================")
message(STATUS "ICAO Validation Library")
message(STATUS "=================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
message(STATUS "=================================")
