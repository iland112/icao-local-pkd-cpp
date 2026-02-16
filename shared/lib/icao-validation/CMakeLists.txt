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

# Print configuration summary
message(STATUS "=================================")
message(STATUS "ICAO Validation Library")
message(STATUS "=================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
message(STATUS "=================================")
