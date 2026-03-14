cmake_minimum_required(VERSION 3.15)
project(icao-cvc-parser VERSION 1.0.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(OpenSSL REQUIRED)

# Library source files
set(CVC_PARSER_SOURCES
    src/tlv.cpp
    src/cvc_parser.cpp
    src/chat_decoder.cpp
    src/cvc_signature.cpp
)

# Create static library
add_library(icao-cvc-parser STATIC ${CVC_PARSER_SOURCES})

# Set library alias for consistent naming
add_library(icao::cvc-parser ALIAS icao-cvc-parser)

# Include directories
target_include_directories(icao-cvc-parser
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link OpenSSL (for signature verification)
target_link_libraries(icao-cvc-parser
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Compiler warnings
if(MSVC)
    target_compile_options(icao-cvc-parser PRIVATE /W4)
else()
    target_compile_options(icao-cvc-parser PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS icao-cvc-parser
    EXPORT icao-cvc-parser-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT icao-cvc-parser-targets
    FILE icao-cvc-parser-targets.cmake
    NAMESPACE icao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-cvc-parser
)

# =============================================================================
# Testing (Optional)
# =============================================================================
option(BUILD_CVC_PARSER_TESTS "Build icao::cvc-parser unit tests" OFF)

if(BUILD_CVC_PARSER_TESTS)
    enable_testing()

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
        tests/test_tlv.cpp
        tests/test_cvc_parser.cpp
        tests/test_chat_decoder.cpp
        tests/test_cvc_signature.cpp
    )

    add_executable(icao_cvc_parser_tests ${TEST_SOURCES})

    target_include_directories(icao_cvc_parser_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(icao_cvc_parser_tests PRIVATE
        icao-cvc-parser
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(icao_cvc_parser_tests)

    message(STATUS "icao::cvc-parser unit tests enabled")
endif()

# Print configuration summary
message(STATUS "=================================")
message(STATUS "ICAO CVC Parser Library")
message(STATUS "=================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
message(STATUS "=================================")
