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

# nlohmann/json (for upload module)
find_package(nlohmann_json CONFIG REQUIRED)

# ICAO Common Library (for upload module)
# ICAO Common Library — check Docker path first, then local dev path
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/common-lib")
    set(ICAO_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common-lib)
else()
    set(ICAO_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common-lib)
endif()
if(EXISTS "${ICAO_COMMON_DIR}/CMakeLists.txt")
    add_subdirectory(${ICAO_COMMON_DIR} ${CMAKE_BINARY_DIR}/common-lib)
endif()

# libzip (for certificate export in upload module)
find_package(libzip CONFIG QUIET)

# UUID (for upload ID generation)
find_library(UUID_LIBRARY uuid)

# LDAP
find_library(LDAP_LIBRARY ldap REQUIRED)
find_library(LBER_LIBRARY lber REQUIRED)

# =============================================================================
# Source Files
# =============================================================================
set(SOURCE_FILES
    src/main.cpp
    # Infrastructure
    src/infrastructure/service_container.cpp
    # Common
    src/common/notification_manager.cpp
    # Handlers
    src/handlers/health_handler.cpp
    src/handlers/notification_handler.cpp
    src/handlers/icao_ldap_handler.cpp
    # ICAO PKD LDAP Sync
    src/relay/icao-ldap/icao_ldap_client.cpp
    src/relay/icao-ldap/icao_ldap_sync_service.cpp
    src/relay/icao-ldap/icao_ldap_cert_utils.cpp
    # Repositories (shared with ICAO LDAP sync + upload module)
    src/domain/models/validation_result.cpp
    src/repositories/certificate_repository.cpp
    src/repositories/crl_repository.cpp
    src/repositories/validation_repository.cpp
    # Adapters (used by ICAO LDAP sync for trust chain validation)
    src/adapters/relay_csca_provider.cpp
    src/adapters/relay_crl_provider.cpp
    # Config DB loader
    src/relay/sync/common/config_db.cpp

    # Upload Module (moved from pkd-management — LDIF/ML import, ICAO version detection)
    src/upload/upload_services.cpp
    src/upload/processing_strategy.cpp
    src/upload/ldif_processor.cpp
    # Upload handlers
    src/upload/handlers/upload_handler.cpp
    src/upload/handlers/upload_stats_handler.cpp
    src/upload/handlers/icao_handler.cpp
    # Upload services
    src/upload/services/upload_service.cpp
    src/upload/services/validation_service.cpp
    src/upload/services/ldif_structure_service.cpp
    src/upload/services/ldap_storage_service.cpp
    src/upload/services/icao_sync_service.cpp
    # Upload repositories
    src/upload/repositories/upload_repository.cpp
    src/upload/repositories/certificate_repository.cpp
    src/upload/repositories/crl_repository.cpp
    src/upload/repositories/validation_repository.cpp
    src/upload/repositories/ldif_structure_repository.cpp
    src/upload/repositories/deviation_list_repository.cpp
    src/upload/repositories/icao_version_repository.cpp
    # Upload common utilities
    src/upload/common/certificate_utils.cpp
    src/upload/common/x509_metadata_extractor.cpp
    src/upload/common/masterlist_processor.cpp
    src/upload/common/progress_manager.cpp
    src/upload/common/ldif_parser.cpp
    src/upload/common/asn1_parser.cpp
    src/upload/common/doc9303_checklist.cpp
    src/upload/common/crl_validator.cpp
    src/upload/common/crl_parser.cpp
    src/upload/common/lc_validator.cpp
    src/upload/common/main_utils.cpp
    src/upload/common/html_parser.cpp
    src/upload/common/db_csca_provider.cpp
    src/upload/common/db_crl_provider.cpp
    # Upload infrastructure
    src/upload/infrastructure/http/http_client.cpp
    src/upload/icao_handler_factory.cpp
    src/upload/ldap_storage_factory.cpp
)

set(HEADER_FILES
    # Infrastructure
    src/infrastructure/service_container.h
    # Common
    src/common/notification_manager.h
    # Handlers
    src/handlers/health_handler.h
    src/handlers/icao_ldap_handler.h
    # ICAO PKD LDAP Sync
    src/relay/icao-ldap/icao_ldap_types.h
    src/relay/icao-ldap/icao_ldap_client.h
    src/relay/icao-ldap/icao_ldap_sync_service.h
    # Shared repositories (ICAO LDAP sync + upload module)
    src/domain/models/crl.h
    src/domain/models/certificate.h
    src/domain/models/validation_result.h
    src/repositories/certificate_repository.h
    src/repositories/crl_repository.h
    src/repositories/validation_repository.h
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
    ${ICAO_COMMON_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::database       # Shared database connection pool library
    icao::audit          # Shared audit logging library
    icao::ldap           # Shared LDAP connection pool library (NEW - v2.4.3)
    icao::config         # Shared configuration management library (NEW - v2.4.3)
    icao::exception      # Shared exception handling library (NEW - v2.4.3)
    icao::logging        # Shared structured logging library (NEW - v2.4.3)
    icao::validation         # Shared ICAO 9303 validation library (trust chain, CRL)
    icao::certificate-parser # Certificate parsing (for upload module)
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
)

# Optional libraries (upload module)
if(TARGET icao-common)
    target_link_libraries(${PROJECT_NAME} PRIVATE icao-common)
endif()
if(libzip_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE libzip::zip)
endif()
if(UUID_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${UUID_LIBRARY})
endif()

# =============================================================================
# Unit Tests (GTest)
# =============================================================================
enable_testing()
find_package(GTest CONFIG REQUIRED)

# Common include directories reused by all test targets
set(RELAY_TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
    ${ICAO_COMMON_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

# Common link libraries reused by OpenSSL-dependent test targets
set(RELAY_TEST_LIBS_BASE
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
)

# =============================================================================
# test_upload_config
# Tests AppConfig (upload_config.h) — pure header-only struct, no external deps.
# =============================================================================
add_executable(test_upload_config
    tests/test_upload_config.cpp
)

target_include_directories(test_upload_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_upload_config PRIVATE
    ${RELAY_TEST_LIBS_BASE}
)

add_test(NAME test_upload_config COMMAND test_upload_config)

# =============================================================================
# test_icao_ldap_cert_utils
# Tests extractCountryFromCert() (promoted from anonymous namespace).
# Requires OpenSSL to construct test certificates in-memory.
# =============================================================================
add_executable(test_icao_ldap_cert_utils
    tests/test_icao_ldap_cert_utils.cpp
    src/relay/icao-ldap/icao_ldap_cert_utils.cpp
)

target_include_directories(test_icao_ldap_cert_utils PRIVATE
    ${RELAY_TEST_INCLUDES}
)

target_link_libraries(test_icao_ldap_cert_utils PRIVATE
    ${RELAY_TEST_LIBS_BASE}
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_test(NAME test_icao_ldap_cert_utils COMMAND test_icao_ldap_cert_utils)

# =============================================================================
# test_ldap_storage_service
# Tests LdapStorageService DN-building and RFC 4514 escaping (no network).
# Also tests getLdapReadConnection() with empty host list (returns nullptr).
# Requires: ldap_storage_service.cpp, icao-common (DN parser), OpenSSL, LDAP.
# =============================================================================
add_executable(test_ldap_storage_service
    tests/test_ldap_storage_service.cpp
    src/upload/services/ldap_storage_service.cpp
    tests/stubs/g_upload_services_stub.cpp
)

target_include_directories(test_ldap_storage_service PRIVATE
    ${RELAY_TEST_INCLUDES}
)

target_link_libraries(test_ldap_storage_service PRIVATE
    ${RELAY_TEST_LIBS_BASE}
    icao::database
    icao::ldap
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
    spdlog::spdlog
)

if(TARGET icao-common)
    target_link_libraries(test_ldap_storage_service PRIVATE icao-common)
endif()

add_test(NAME test_ldap_storage_service COMMAND test_ldap_storage_service)

# =============================================================================
# test_upload_services
# Tests UploadServiceContainer initialization, accessor null-checks, and shutdown.
# Uses upload_module_stubs.cpp so that real repository/service .cpp files are NOT
# compiled — avoiding Drogon, JsonCpp, and PostgreSQL linker dependencies.
# =============================================================================
add_executable(test_upload_services
    tests/test_upload_services.cpp
    src/upload/upload_services.cpp
    tests/stubs/upload_module_stubs.cpp
)

target_include_directories(test_upload_services PRIVATE
    ${RELAY_TEST_INCLUDES}
)

target_link_libraries(test_upload_services PRIVATE
    ${RELAY_TEST_LIBS_BASE}
    icao::database
    icao::ldap
    icao::validation
    icao::certificate-parser  # brings in dl_parser.h include path for deviation_list_repository.h
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

if(TARGET icao-common)
    target_link_libraries(test_upload_services PRIVATE icao-common)
endif()

add_test(NAME test_upload_services COMMAND test_upload_services)

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
