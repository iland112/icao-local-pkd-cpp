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

# ICAO Common Library (Shared DN parsing, X.509 utilities)
set(ICAO_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common-lib)
add_subdirectory(${ICAO_COMMON_DIR} ${CMAKE_BINARY_DIR}/common-lib)

# ICAO Shared Libraries (Database, Audit, LDAP, Config, Exception, Logging)
# Note: In Docker, shared/ is copied to ./shared/ by Dockerfile line 87
# For local builds, shared/ is at ../../shared
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shared")
    set(ICAO_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared)
else()
    set(ICAO_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
endif()
add_subdirectory(${ICAO_SHARED_DIR} ${CMAKE_BINARY_DIR}/shared-libs)

# JsonCpp (for test targets that link directly)
find_package(jsoncpp CONFIG REQUIRED)

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
    src/common/main_utils.cpp         # Utility functions (crypto, file validation, X.509 helpers)
    src/common/certificate_utils.cpp
    src/common/masterlist_processor.cpp
    src/common/x509_metadata_extractor.cpp
    src/common/progress_manager.cpp  # Phase 4.4: Enhanced progress tracking
    src/common/asn1_parser.cpp       # ASN.1/TLV parser for Master List structure
    src/common/ldif_parser.cpp       # v2.2.2: LDIF structure parser for visualization
    src/common/doc9303_checklist.cpp  # v2.17.0: Doc 9303 compliance checklist
    # Database connection pool now provided by shared icao::database library

    # Sprint 2: Link Certificate Validation (NEW)
    src/common/crl_validator.cpp
    src/common/crl_parser.cpp
    src/common/lc_validator.cpp

    # Clean Architecture Layers (Existing)
    src/repositories/ldap_certificate_repository.cpp
    src/services/certificate_service.cpp

    # Repository Layer - main.cpp refactoring (v2.1.3 Phase 1.5)
    src/repositories/upload_repository.cpp
    src/repositories/certificate_repository.cpp
    src/repositories/validation_repository.cpp
    src/repositories/audit_repository.cpp

    src/repositories/ldif_structure_repository.cpp  # v2.2.2: LDIF structure visualization
    src/repositories/user_repository.cpp  # Phase 5.4: AuthHandler migration
    src/repositories/auth_audit_repository.cpp  # Phase 5.4: AuthHandler migration
    src/repositories/crl_repository.cpp  # Phase 6.4: CRL operations
    src/repositories/deviation_list_repository.cpp  # DL deviation data storage

    # Service Layer - main.cpp refactoring (v2.1.3)
    src/services/upload_service.cpp
    src/services/validation_service.cpp
    src/services/audit_service.cpp

    # Provider Adapters (v2.11.0: icao::validation library integration)
    src/adapters/db_csca_provider.cpp
    src/adapters/db_crl_provider.cpp
    src/adapters/ldap_csca_provider.cpp   # v2.31.0: LDAP real-time PA Lookup
    src/adapters/ldap_crl_provider.cpp    # v2.31.0: LDAP real-time PA Lookup

    src/services/ldif_structure_service.cpp  # v2.2.2: LDIF structure visualization
    src/services/ldap_storage_service.cpp   # v2.13.0: LDAP storage operations

    # ICAO Auto Sync Module (NEW - v1.7.0)
    src/infrastructure/http/http_client.cpp
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
    src/handlers/upload_handler.cpp
    src/handlers/upload_stats_handler.cpp
    src/handlers/certificate_handler.cpp
    src/handlers/misc_handler.cpp
    src/repositories/code_master_repository.cpp
    src/handlers/code_master_handler.cpp

    # API Client Module (v2.21.0 - External Client Agent Authentication)
    src/auth/api_key_generator.cpp
    src/auth/personal_info_crypto.cpp
    src/middleware/api_rate_limiter.cpp
    src/repositories/api_client_repository.cpp
    src/handlers/api_client_handler.cpp

    # API Client Request Workflow (v2.31.0 - Public Request + Admin Approval)
    src/repositories/api_client_request_repository.cpp
    src/handlers/api_client_request_handler.cpp

    # Pending DSC Registration Approval (v2.31.0)
    src/repositories/pending_dsc_repository.cpp

    # CSR Management (v2.35.0 - ICAO PKD CSR generation)
    src/repositories/csr_repository.cpp
    src/services/csr_service.cpp
    src/handlers/csr_handler.cpp

    # Infrastructure
    src/infrastructure/service_container.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${ICAO_COMMON_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::database       # Shared database connection pool library
    icao::audit          # Shared audit logging library
    icao::ldap           # Shared LDAP connection pool library (NEW)
    icao::config         # Shared configuration management library (NEW)
    icao::exception      # Shared exception handling library (NEW)
    icao::logging        # Shared structured logging library (NEW)
    icao::certificate-parser  # Certificate parsing (FileDetector, PemParser, DerParser, CertTypeDetector)
    icao::validation         # ICAO validation library (cert_ops, TrustChainBuilder, CrlChecker)
    icao-common
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
    src/common/crl_parser.cpp
)

target_include_directories(test_crl_validator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(test_crl_validator PRIVATE
    icao::database
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
# Auth Module Unit Tests (Sprint 3 - v2.28.0+)
# =============================================================================

# --- test_api_key_generator ---
add_executable(test_api_key_generator
    tests/auth/test_api_key_generator.cpp
    src/auth/api_key_generator.cpp
)

target_include_directories(test_api_key_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_api_key_generator PRIVATE
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_api_key_generator COMMAND test_api_key_generator)

# --- test_password_hash ---
add_executable(test_password_hash
    tests/auth/test_password_hash.cpp
    src/auth/password_hash.cpp
)

target_include_directories(test_password_hash PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_password_hash PRIVATE
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_password_hash COMMAND test_password_hash)

# --- test_personal_info_crypto ---
add_executable(test_personal_info_crypto
    tests/auth/test_personal_info_crypto.cpp
    src/auth/personal_info_crypto.cpp
)

target_include_directories(test_personal_info_crypto PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_personal_info_crypto PRIVATE
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_personal_info_crypto COMMAND test_personal_info_crypto)

# --- test_jwt_service ---
add_executable(test_jwt_service
    tests/auth/test_jwt_service.cpp
    src/auth/jwt_service.cpp
)

target_include_directories(test_jwt_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_jwt_service PRIVATE
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_jwt_service COMMAND test_jwt_service)

# =============================================================================
# ApiRateLimiter Tests (v2.21.0)
# =============================================================================
add_executable(test_api_rate_limiter
    tests/test_api_rate_limiter.cpp
    src/middleware/api_rate_limiter.cpp
)

target_include_directories(test_api_rate_limiter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_api_rate_limiter PRIVATE
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
)

add_test(NAME test_api_rate_limiter COMMAND test_api_rate_limiter)

# =============================================================================
# ProgressManager Tests (v2.28.0)
# =============================================================================
add_executable(test_progress_manager
    tests/test_progress_manager.cpp
    src/common/progress_manager.cpp
    src/common/x509_metadata_extractor.cpp
    src/common/certificate_utils.cpp
    tests/stubs/service_container_stubs.cpp
)

target_include_directories(test_progress_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${ICAO_COMMON_DIR}/include
)

target_link_libraries(test_progress_manager PRIVATE
    icao::database
    icao::certificate-parser
    icao::validation
    icao-common
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_progress_manager COMMAND test_progress_manager)

# =============================================================================
# LcValidator Tests (v2.28.0+)
# =============================================================================
add_executable(test_lc_validator
    tests/test_lc_validator.cpp
    src/common/lc_validator.cpp
    src/common/crl_validator.cpp
    src/common/crl_parser.cpp
    src/common/certificate_utils.cpp
    src/common/x509_metadata_extractor.cpp
    tests/stubs/g_services_stub.cpp
)

target_include_directories(test_lc_validator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${ICAO_COMMON_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(test_lc_validator PRIVATE
    icao::database
    icao::certificate-parser
    icao::validation
    icao-common
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    spdlog::spdlog
)

add_test(NAME test_lc_validator COMMAND test_lc_validator)

# =============================================================================
# ASN.1 Parser Tests (v2.28.0+)
# =============================================================================
add_executable(test_asn1_parser
    tests/test_asn1_parser.cpp
    src/common/asn1_parser.cpp
)

target_include_directories(test_asn1_parser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
)

target_link_libraries(test_asn1_parser PRIVATE
    JsonCpp::JsonCpp
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_asn1_parser COMMAND test_asn1_parser)

# =============================================================================
# LdifStructureService Tests (v2.28.0+)
# DISABLED: LdifStructureRepository::getLdifStructure is not virtual,
# cannot be mocked via inheritance. Needs interface extraction to enable.
# =============================================================================
# add_executable(test_ldif_structure_service ...)
# add_test(NAME test_ldif_structure_service ...)

# =============================================================================
# Doc 9303 Checklist Tests (v2.17.0)
# doc9303_checklist.cpp depends on x509_metadata_extractor.cpp for key size
# extraction; both are included here.  No DB/g_services dependency.
# JsonCpp::JsonCpp is needed because doc9303_checklist.h includes <json/json.h>.
# =============================================================================
add_executable(test_doc9303_checklist
    tests/test_doc9303_checklist.cpp
    src/common/doc9303_checklist.cpp
    src/common/x509_metadata_extractor.cpp
)

target_include_directories(test_doc9303_checklist PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_doc9303_checklist PRIVATE
    JsonCpp::JsonCpp
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_doc9303_checklist COMMAND test_doc9303_checklist)

# =============================================================================
# X.509 Metadata Extractor Tests
# Covers all functions in namespace x509:: (extractMetadata, getVersion,
# extractHashAlgorithm, getPublicKey*, getKeyUsage, isCA, bytesToHex, etc.)
# No DB/g_services dependency.
# =============================================================================
add_executable(test_x509_metadata_extractor
    tests/test_x509_metadata_extractor.cpp
    src/common/x509_metadata_extractor.cpp
)

target_include_directories(test_x509_metadata_extractor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_x509_metadata_extractor PRIVATE
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_test(NAME test_x509_metadata_extractor COMMAND test_x509_metadata_extractor)

# =============================================================================
# Certificate Utils Tests (pure / stateless functions)
# Tests only the stateless utilities (x509NameToString, extractCountryCode,
# computeSha256Fingerprint, isLinkCertificate, etc.).  DB-dependent functions
# (saveCertificateWithDuplicateCheck, trackCertificateDuplicate, etc.) are NOT
# exercised — the test file defines g_services = nullptr as a null stub so
# those code paths are never reached at runtime.
# =============================================================================
add_executable(test_certificate_utils
    tests/test_certificate_utils.cpp
    src/common/certificate_utils.cpp
    src/common/x509_metadata_extractor.cpp
    tests/stubs/service_container_stubs.cpp
)

target_include_directories(test_certificate_utils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${ICAO_COMMON_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(test_certificate_utils PRIVATE
    icao::database
    icao::certificate-parser
    icao::validation
    icao-common
    JsonCpp::JsonCpp
    GTest::gtest
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    spdlog::spdlog
)

add_test(NAME test_certificate_utils COMMAND test_certificate_utils)

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
