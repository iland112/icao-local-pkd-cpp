# ICAO PKD - Shared Audit Library
# Version: 1.0.0
# Created: 2026-02-03

cmake_minimum_required(VERSION 3.15)

# Library target
add_library(icao-audit INTERFACE)

# Header-only library - no source files to compile
# Note: Header is at shared/icao/audit/audit_log.h, so we need to go up to shared/ directory
target_include_directories(icao-audit INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
    $<INSTALL_INTERFACE:include>
)

# Dependencies
# Note: This is a header-only library. All dependencies are compile-time only.
# The consuming service must provide:
# - PostgreSQL (libpq) for PGconn* and PQexec functions
# - JsonCpp for Json::Value
# - spdlog for logging
# - Drogon for HttpRequestPtr

# We don't need to link anything since we're header-only and the consuming
# target already has all these dependencies

# Alias for consistent naming
add_library(icao::audit ALIAS icao-audit)

# Installation rules
install(TARGETS icao-audit
    EXPORT icao-audit-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ../../icao/audit/audit_log.h
    DESTINATION include/icao/audit
)

install(EXPORT icao-audit-targets
    FILE icao-audit-targets.cmake
    NAMESPACE icao::
    DESTINATION lib/cmake/icao-audit
)

# Generate package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/icao-audit-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/icao-audit-config.cmake
    INSTALL_DESTINATION lib/cmake/icao-audit
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/icao-audit-config.cmake
    DESTINATION lib/cmake/icao-audit
)
