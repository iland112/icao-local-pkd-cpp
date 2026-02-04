# ICAO PKD - Shared Database Library
# Version: 1.0.0
# Created: 2026-02-04
#
# Provides thread-safe PostgreSQL connection pooling for all ICAO PKD services

cmake_minimum_required(VERSION 3.15)

# Find PostgreSQL
find_package(PostgreSQL REQUIRED)

# Find spdlog
find_package(spdlog CONFIG REQUIRED)

# Library target (compiled library, not header-only)
add_library(icao-database STATIC
    db_connection_pool.cpp
)

# Include directories
target_include_directories(icao-database PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/icao/database>
)

# Link dependencies
target_link_libraries(icao-database PUBLIC
    PostgreSQL::PostgreSQL
    spdlog::spdlog
)

# C++ standard
target_compile_features(icao-database PUBLIC cxx_std_17)

# Alias for consistent naming
add_library(icao::database ALIAS icao-database)

# Installation rules
install(TARGETS icao-database
    EXPORT icao-database-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES
    db_connection_pool.h
    DESTINATION include/icao/database
)

install(EXPORT icao-database-targets
    FILE icao-database-targets.cmake
    NAMESPACE icao::
    DESTINATION lib/cmake/icao-database
)

# Generate package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/icao-database-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/icao-database-config.cmake
    INSTALL_DESTINATION lib/cmake/icao-database
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/icao-database-config.cmake
    DESTINATION lib/cmake/icao-database
)
