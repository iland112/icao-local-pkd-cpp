cmake_minimum_required(VERSION 3.20)
project(pkd-relay-service VERSION 2.0.0 LANGUAGES CXX)

# =============================================================================
# C++ Standard and Compiler Settings
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# =============================================================================
# Output Directories
# =============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Dependencies
# =============================================================================

# ICAO Shared Libraries (Audit Logging)
# Note: In Docker, shared/ is copied to ./shared/ by Dockerfile
# For local builds, shared/ is at ../../shared
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shared")
    set(ICAO_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared)
else()
    set(ICAO_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
endif()
add_subdirectory(${ICAO_SHARED_DIR} ${CMAKE_BINARY_DIR}/shared-libs)

# Drogon Web Framework
find_package(Drogon CONFIG REQUIRED)

# OpenSSL
find_package(OpenSSL REQUIRED)

# PostgreSQL
find_package(PostgreSQL REQUIRED)

# spdlog
find_package(spdlog CONFIG REQUIRED)

# LDAP
find_library(LDAP_LIBRARY ldap REQUIRED)
find_library(LBER_LIBRARY lber REQUIRED)

# =============================================================================
# Source Files
# =============================================================================
set(SOURCE_FILES
    src/main.cpp
    src/relay/sync/ldap_operations.cpp
    src/relay/sync/reconciliation_engine.cpp
    # Repository Pattern - Repositories
    src/repositories/sync_status_repository.cpp
    src/repositories/certificate_repository.cpp
    src/repositories/crl_repository.cpp
    src/repositories/reconciliation_repository.cpp
    # Repository Pattern - Services
    src/services/sync_service.cpp
    src/services/reconciliation_service.cpp
)

set(HEADER_FILES
    src/relay/sync/common/types.h
    src/relay/sync/common/config.h
    src/relay/sync/ldap_operations.h
    src/relay/sync/reconciliation_engine.h
    # Repository Pattern - Domain Models
    src/domain/models/sync_status.h
    src/domain/models/reconciliation_summary.h
    src/domain/models/reconciliation_log.h
    src/domain/models/crl.h
    src/domain/models/certificate.h
    # Repository Pattern - Repositories
    src/repositories/sync_status_repository.h
    src/repositories/certificate_repository.h
    src/repositories/crl_repository.h
    src/repositories/reconciliation_repository.h
    # Repository Pattern - Services
    src/services/sync_service.h
    src/services/reconciliation_service.h
)

# =============================================================================
# Main Application
# =============================================================================
add_executable(${PROJECT_NAME}
    ${SOURCE_FILES}
    ${HEADER_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::audit          # Shared audit logging library
    icao::database       # Shared database connection pool library
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    spdlog::spdlog
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
)

# =============================================================================
# Build Info
# =============================================================================
message(STATUS "")
message(STATUS "=== PKD Relay Service Build Configuration ===")
message(STATUS "CMake version:      ${CMAKE_VERSION}")
message(STATUS "C++ compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix:     ${CMAKE_INSTALL_PREFIX}")
message(STATUS "==============================================")
message(STATUS "")
