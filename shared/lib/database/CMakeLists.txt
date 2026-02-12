# ICAO PKD - Shared Database Library
# Version: 2.0.0
# Created: 2026-02-04
# Updated: 2026-02-04
#
# Provides thread-safe database connection pooling for PostgreSQL and Oracle
# Supports strategy pattern for database-agnostic connection management

cmake_minimum_required(VERSION 3.15)

# Find PostgreSQL
find_package(PostgreSQL REQUIRED)

# Find spdlog
find_package(spdlog CONFIG REQUIRED)

# Find jsoncpp (for Query Executor)
find_package(jsoncpp CONFIG REQUIRED)

# Oracle support (can be disabled for postgres-only builds, e.g., ARM64 luckfox)
option(ENABLE_ORACLE "Enable Oracle database support" ON)

# Conditionally include Oracle source files
if(ENABLE_ORACLE)
    set(ORACLE_SOURCES oracle_connection_pool.cpp oracle_query_executor.cpp)
else()
    set(ORACLE_SOURCES)
    message(STATUS "Oracle database support DISABLED (postgres-only build)")
endif()

# Library target (compiled library, not header-only)
add_library(icao-database STATIC
    db_connection_pool.cpp
    db_connection_pool_factory.cpp
    ${ORACLE_SOURCES}
    postgresql_query_executor.cpp
    query_executor_factory.cpp
)

# Include directories
target_include_directories(icao-database PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/otl>
    $<INSTALL_INTERFACE:include/icao/database>
)

# Oracle SDK include path (PRIVATE to avoid conflicts with OpenLDAP)
if(ENABLE_ORACLE AND DEFINED ENV{ORACLE_HOME})
    target_include_directories(icao-database PRIVATE
        $ENV{ORACLE_HOME}/sdk/include
    )

    # Oracle Instant Client library linking
    target_link_directories(icao-database PUBLIC
        $ENV{ORACLE_HOME}
    )
    target_link_libraries(icao-database PUBLIC clntsh)
    target_compile_definitions(icao-database PUBLIC ENABLE_ORACLE=1)
endif()

# Link dependencies
target_link_libraries(icao-database PUBLIC
    PostgreSQL::PostgreSQL
    spdlog::spdlog
    JsonCpp::JsonCpp
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

set(DATABASE_HEADERS
    db_connection_interface.h
    db_connection_pool.h
    db_connection_pool_factory.h
    i_query_executor.h
    postgresql_query_executor.h
)
if(ENABLE_ORACLE)
    list(APPEND DATABASE_HEADERS oracle_connection_pool.h oracle_query_executor.h)
endif()
install(FILES ${DATABASE_HEADERS} DESTINATION include/icao/database)

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
