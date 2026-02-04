# ICAO PKD - Shared Libraries
# Top-level CMakeLists.txt for all shared libraries

cmake_minimum_required(VERSION 3.15)
project(icao-shared VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_AUDIT_LIB "Build audit logging library" ON)
option(BUILD_DATABASE_LIB "Build database connection pool library" ON)
option(BUILD_ICAO9303_LIB "Build ICAO 9303 parser library" ON)
option(BUILD_LDAP_LIB "Build LDAP connection pool library" ON)
option(BUILD_CONFIG_LIB "Build configuration management library" ON)
option(BUILD_EXCEPTION_LIB "Build exception handling library" ON)
option(BUILD_LOGGING_LIB "Build structured logging library" ON)
option(BUILD_CERTIFICATE_PARSER_LIB "Build certificate parser library" ON)

# Add subdirectories
if(BUILD_AUDIT_LIB)
    add_subdirectory(lib/audit)
endif()

if(BUILD_DATABASE_LIB)
    add_subdirectory(lib/database)
endif()

if(BUILD_ICAO9303_LIB)
    add_subdirectory(lib/icao9303)
endif()

if(BUILD_LDAP_LIB)
    add_subdirectory(lib/ldap)
endif()

if(BUILD_CONFIG_LIB)
    add_subdirectory(lib/config)
endif()

if(BUILD_EXCEPTION_LIB)
    add_subdirectory(lib/exception)
endif()

if(BUILD_LOGGING_LIB)
    add_subdirectory(lib/logging)
endif()

if(BUILD_CERTIFICATE_PARSER_LIB)
    add_subdirectory(lib/certificate-parser)
endif()

# Display configuration summary
message(STATUS "")
message(STATUS "=== ICAO Shared Libraries Configuration ===")
message(STATUS "  Audit Library:       ${BUILD_AUDIT_LIB}")
message(STATUS "  Database Pool:       ${BUILD_DATABASE_LIB}")
message(STATUS "  ICAO 9303 Parser:    ${BUILD_ICAO9303_LIB}")
message(STATUS "  LDAP Pool:           ${BUILD_LDAP_LIB}")
message(STATUS "  Configuration:       ${BUILD_CONFIG_LIB}")
message(STATUS "  Exception Handling:  ${BUILD_EXCEPTION_LIB}")
message(STATUS "  Structured Logging:  ${BUILD_LOGGING_LIB}")
message(STATUS "  Certificate Parser:  ${BUILD_CERTIFICATE_PARSER_LIB}")
message(STATUS "==========================================")
message(STATUS "")
