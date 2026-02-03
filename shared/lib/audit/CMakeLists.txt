# ICAO PKD - Shared Audit Library
# Version: 1.0.0
# Created: 2026-02-03

cmake_minimum_required(VERSION 3.15)

# Library target
add_library(icao-audit INTERFACE)

# Header-only library - no source files to compile
target_include_directories(icao-audit INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

# Dependencies
find_package(PkgConfig REQUIRED)

# PostgreSQL libpq
pkg_check_modules(LIBPQ REQUIRED libpq)
target_include_directories(icao-audit INTERFACE ${LIBPQ_INCLUDE_DIRS})
target_link_libraries(icao-audit INTERFACE ${LIBPQ_LIBRARIES})

# JsonCpp
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
target_include_directories(icao-audit INTERFACE ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(icao-audit INTERFACE ${JSONCPP_LIBRARIES})

# spdlog
find_package(spdlog REQUIRED)
target_link_libraries(icao-audit INTERFACE spdlog::spdlog)

# Drogon
find_package(Drogon REQUIRED)
target_link_libraries(icao-audit INTERFACE Drogon::Drogon)

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

install(FILES audit_log.h
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
