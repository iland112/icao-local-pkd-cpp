# LDAP Connection Pool Library
# Thread-safe connection pooling for LDAP servers
cmake_minimum_required(VERSION 3.16)

# Library name
set(LIB_NAME icao-ldap)

# Source files
set(LDAP_SOURCES
    ldap_connection_pool.cpp
)

# Create static library
add_library(${LIB_NAME} STATIC ${LDAP_SOURCES})

# Include directories
target_include_directories(${LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/icao/ldap>
)

# Find required packages
find_package(spdlog REQUIRED)

# Find OpenLDAP
find_path(LDAP_INCLUDE_DIR ldap.h)
find_library(LDAP_LIBRARY NAMES ldap)
find_library(LBER_LIBRARY NAMES lber)

if(NOT LDAP_INCLUDE_DIR OR NOT LDAP_LIBRARY OR NOT LBER_LIBRARY)
    message(FATAL_ERROR "OpenLDAP not found. Please install libldap-dev")
endif()

# Link libraries
target_link_libraries(${LIB_NAME} PUBLIC
    spdlog::spdlog
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
)

# Include directories for LDAP
target_include_directories(${LIB_NAME} PRIVATE ${LDAP_INCLUDE_DIR})

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
add_library(icao::ldap ALIAS ${LIB_NAME})

# Installation rules
include(GNUInstallDirs)

install(TARGETS ${LIB_NAME}
    EXPORT icao-ldap-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(FILES
    ldap_connection_pool.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/icao/ldap
)

# Export targets
install(EXPORT icao-ldap-targets
    FILE icao-ldap-targets.cmake
    NAMESPACE icao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-ldap
)

# Create package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/icao-ldap-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/icao-ldap-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-ldap
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/icao-ldap-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-ldap
)

# Display configuration
message(STATUS "LDAP Connection Pool Library configured")
message(STATUS "  - LDAP Library: ${LDAP_LIBRARY}")
message(STATUS "  - LBER Library: ${LBER_LIBRARY}")
message(STATUS "  - Min Pool Size: 2")
message(STATUS "  - Max Pool Size: 10")
