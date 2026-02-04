# ICAO 9303 Parser Library
# SOD, Data Group, and MRZ parsing for ICAO 9303 compliant passports
cmake_minimum_required(VERSION 3.16)

# Library name
set(LIB_NAME icao-icao9303)

# Source files
set(ICAO9303_SOURCES
    sod_parser.cpp
    dg_parser.cpp
)

# Create static library
add_library(${LIB_NAME} STATIC ${ICAO9303_SOURCES})

# Include directories
target_include_directories(${LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/models>
    $<INSTALL_INTERFACE:include/icao/icao9303>
)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)

# Link libraries
target_link_libraries(${LIB_NAME} PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    # Note: jsoncpp is linked by parent service (pa-service, pkd-management)
)

# Compiler features
target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)

# Compiler options
target_compile_options(${LIB_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Create alias for namespaced usage
add_library(icao::icao9303 ALIAS ${LIB_NAME})

# Installation rules
include(GNUInstallDirs)

install(TARGETS ${LIB_NAME}
    EXPORT icao-icao9303-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(FILES
    sod_parser.h
    dg_parser.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/icao/icao9303
)

install(DIRECTORY models/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/icao/icao9303/models
    FILES_MATCHING PATTERN "*.h"
)

# Export targets
install(EXPORT icao-icao9303-targets
    FILE icao-icao9303-targets.cmake
    NAMESPACE icao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-icao9303
)

# Create package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/icao-icao9303-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/icao-icao9303-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-icao9303
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/icao-icao9303-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-icao9303
)

# Display configuration
message(STATUS "ICAO 9303 Parser Library configured")
message(STATUS "  - SOD Parser: ENABLED")
message(STATUS "  - Data Group Parser: ENABLED")
message(STATUS "  - MRZ Parser: ENABLED (via DG1)")
