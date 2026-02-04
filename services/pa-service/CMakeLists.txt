cmake_minimum_required(VERSION 3.20)
project(pa-service VERSION 1.0.0 LANGUAGES CXX)

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

# ICAO Common Library (Shared DN parsing, X.509 utilities)
set(ICAO_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common-lib)
add_subdirectory(${ICAO_COMMON_DIR} ${CMAKE_BINARY_DIR}/common-lib)

# ICAO Shared Libraries (Audit Logging)
# Note: In Docker, shared/ is copied to ./shared/ by Dockerfile line 87
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

# nlohmann/json
find_package(nlohmann_json CONFIG REQUIRED)

# spdlog
find_package(spdlog CONFIG REQUIRED)

# LDAP
find_library(LDAP_LIBRARY ldap REQUIRED)
find_library(LBER_LIBRARY lber REQUIRED)

# UUID
find_library(UUID_LIBRARY uuid REQUIRED)

# =============================================================================
# Source Files
# =============================================================================

# Domain Models
set(DOMAIN_MODELS_SRC
    src/domain/models/pa_verification.cpp
    src/domain/models/sod_data.cpp
    src/domain/models/data_group.cpp
    src/domain/models/certificate_chain_validation.cpp
)

# Repositories
set(REPOSITORIES_SRC
    src/repositories/pa_verification_repository.cpp
    src/repositories/ldap_certificate_repository.cpp
    src/repositories/ldap_crl_repository.cpp
    src/repositories/data_group_repository.cpp
)

# Services
set(SERVICES_SRC
    src/services/sod_parser_service.cpp
    src/services/data_group_parser_service.cpp
    src/services/certificate_validation_service.cpp
    src/services/pa_verification_service.cpp
)

# Common Utilities
set(COMMON_SRC
    src/common/ldap_connection_pool.cpp
    # Database connection pool now provided by shared icao::database library
)

# =============================================================================
# Main Application
# =============================================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${DOMAIN_MODELS_SRC}
    ${REPOSITORIES_SRC}
    ${SERVICES_SRC}
    ${COMMON_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${ICAO_COMMON_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::database       # Shared database connection pool library
    icao::audit          # Shared audit logging library
    icao-common
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
    ${UUID_LIBRARY}
)

# =============================================================================
# Testing (Optional)
# =============================================================================
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    # Test sources
    set(TEST_SRC
        tests/repositories/pa_verification_repository_test.cpp
        tests/repositories/data_group_repository_test.cpp
        tests/repositories/ldap_helpers_test.cpp
    )

    # Create test executable
    add_executable(pa-service-tests
        ${TEST_SRC}
        ${DOMAIN_MODELS_SRC}
        ${REPOSITORIES_SRC}
        ${SERVICES_SRC}
        ${COMMON_SRC}
    )

    target_include_directories(pa-service-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
        ${PostgreSQL_INCLUDE_DIRS}
    )

    target_link_libraries(pa-service-tests PRIVATE
        GTest::GTest
        GTest::Main
        Drogon::Drogon
        OpenSSL::SSL
        OpenSSL::Crypto
        PostgreSQL::PostgreSQL
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        ${LDAP_LIBRARY}
        ${LBER_LIBRARY}
        ${UUID_LIBRARY}
    )

    # Add tests to CTest
    include(GoogleTest)
    gtest_discover_tests(pa-service-tests)

    message(STATUS "Unit tests enabled")
endif()

# =============================================================================
# Build Info
# =============================================================================
message(STATUS "")
message(STATUS "=== PA Service Build Configuration ===")
message(STATUS "CMake version:      ${CMAKE_VERSION}")
message(STATUS "C++ compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix:     ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Build tests:        ${BUILD_TESTS}")
message(STATUS "==========================================")
message(STATUS "")
