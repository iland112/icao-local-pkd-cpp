cmake_minimum_required(VERSION 3.15)
project(icao-certificate-parser VERSION 1.0.0 LANGUAGES CXX)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(OpenSSL REQUIRED)

# Library source files
set(CERTIFICATE_PARSER_SOURCES
    src/file_detector.cpp
    src/cert_type_detector.cpp
    src/dl_parser.cpp
    src/pem_parser.cpp
    src/der_parser.cpp
    src/cert_validator.cpp
)

# Create static library
add_library(icao-certificate-parser STATIC ${CERTIFICATE_PARSER_SOURCES})

# Set library alias for consistent naming
add_library(icao::certificate-parser ALIAS icao-certificate-parser)

# Include directories
target_include_directories(icao-certificate-parser
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link OpenSSL
target_link_libraries(icao-certificate-parser
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Compiler warnings
if(MSVC)
    target_compile_options(icao-certificate-parser PRIVATE /W4)
else()
    target_compile_options(icao-certificate-parser PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Installation rules
include(GNUInstallDirs)

install(TARGETS icao-certificate-parser
    EXPORT icao-certificate-parser-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT icao-certificate-parser-targets
    FILE icao-certificate-parser-targets.cmake
    NAMESPACE icao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-certificate-parser
)

# Generate and install package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/icao-certificate-parser-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/icao-certificate-parser-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-certificate-parser
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/icao-certificate-parser-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/icao-certificate-parser-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/icao-certificate-parser-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-certificate-parser
)

# Optional: Build tests
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "=================================")
message(STATUS "ICAO Certificate Parser Library")
message(STATUS "=================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=================================")
