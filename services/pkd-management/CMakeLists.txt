cmake_minimum_required(VERSION 3.20)
project(pkd-management VERSION 1.0.0 LANGUAGES CXX)

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

# libzip (for certificate export)
find_package(libzip CONFIG REQUIRED)

# =============================================================================
# Main Application
# =============================================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/processing_strategy.cpp
    src/ldif_processor.cpp

    # Common Utilities (NEW - v2.0.0)
    src/common/certificate_utils.cpp
    src/common/masterlist_processor.cpp
    src/common/x509_metadata_extractor.cpp
    src/common/progress_manager.cpp  # Phase 4.4: Enhanced progress tracking
    src/common/asn1_parser.cpp       # ASN.1/TLV parser for Master List structure

    # Sprint 2: Link Certificate Validation (NEW)
    src/common/crl_validator.cpp
    src/common/lc_validator.cpp

    # Clean Architecture Layers (Existing)
    src/repositories/ldap_certificate_repository.cpp
    src/services/certificate_service.cpp

    # Repository Layer - main.cpp refactoring (v2.1.3 Phase 1.5)
    src/repositories/upload_repository.cpp
    src/repositories/certificate_repository.cpp
    src/repositories/validation_repository.cpp
    src/repositories/audit_repository.cpp
    src/repositories/statistics_repository.cpp

    # Service Layer - main.cpp refactoring (v2.1.3)
    src/services/upload_service.cpp
    src/services/validation_service.cpp
    src/services/audit_service.cpp
    src/services/statistics_service.cpp

    # ICAO Auto Sync Module (NEW - v1.7.0)
    src/infrastructure/http/http_client.cpp
    src/infrastructure/notification/email_sender.cpp
    src/utils/html_parser.cpp
    src/repositories/icao_version_repository.cpp
    src/services/icao_sync_service.cpp
    src/handlers/icao_handler.cpp

    # Authentication Module (NEW - Phase 3)
    src/auth/password_hash.cpp
    src/auth/jwt_service.cpp
    src/middleware/auth_middleware.cpp
    src/middleware/permission_filter.cpp
    src/handlers/auth_handler.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    libzip::zip
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
    ${UUID_LIBRARY}
)

# =============================================================================
# Authentication Module Tests (Optional)
# =============================================================================
add_executable(test_auth
    src/auth/test_auth.cpp
    src/auth/password_hash.cpp
    src/auth/jwt_service.cpp
)

target_include_directories(test_auth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(test_auth PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

# =============================================================================
# LDAP DN Migration Tests (Sprint 1)
# =============================================================================
# Enable testing
enable_testing()

# Find GTest
find_package(GTest CONFIG REQUIRED)

add_executable(test_ldap_dn
    tests/ldap_dn_test.cpp
)

target_include_directories(test_ldap_dn PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_ldap_dn PRIVATE
    GTest::gtest
    GTest::gtest_main
)

# Register test with CTest
add_test(NAME ldap_dn_test COMMAND test_ldap_dn)

# =============================================================================
# CRL Validator Tests (Sprint 2)
# =============================================================================
add_executable(test_crl_validator
    tests/crl_validator_test.cpp
    src/common/crl_validator.cpp
)

target_include_directories(test_crl_validator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(test_crl_validator PRIVATE
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    spdlog::spdlog
)

# Register test with CTest
add_test(NAME crl_validator_test COMMAND test_crl_validator)

# =============================================================================
# Build Info
# =============================================================================
message(STATUS "")
message(STATUS "=== PKD Management Service Build Configuration ===")
message(STATUS "CMake version:      ${CMAKE_VERSION}")
message(STATUS "C++ compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix:     ${CMAKE_INSTALL_PREFIX}")
message(STATUS "===================================================")
message(STATUS "")
