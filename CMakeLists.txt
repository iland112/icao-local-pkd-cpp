cmake_minimum_required(VERSION 3.20)

project(icao-local-pkd
    VERSION 1.0.0
    DESCRIPTION "ICAO Local PKD Management and Passive Authentication System"
    LANGUAGES CXX
)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Dependencies
# =============================================================================

# Find packages
find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# OpenLDAP (system library)
find_library(LDAP_LIBRARY ldap REQUIRED)
find_library(LBER_LIBRARY lber REQUIRED)

# UUID (system library)
find_library(UUID_LIBRARY uuid REQUIRED)

# =============================================================================
# Source Files
# =============================================================================

# Shared Kernel
set(SHARED_SOURCES
    src/shared/domain/ValueObject.cpp
    src/shared/domain/Entity.cpp
    src/shared/domain/AggregateRoot.cpp
    src/shared/exception/DomainException.cpp
    src/shared/exception/ApplicationException.cpp
    src/shared/exception/InfrastructureException.cpp
    src/shared/util/HashUtil.cpp
    src/shared/util/Base64Util.cpp
    src/shared/util/UuidUtil.cpp
    src/shared/progress/ProgressService.cpp
)

# Configuration
set(CONFIG_SOURCES
    src/config/AppConfig.cpp
    src/config/DatabaseConfig.cpp
    src/config/LdapConfig.cpp
)

# File Upload Context
set(FILEUPLOAD_SOURCES
    src/fileupload/domain/model/UploadedFile.cpp
    src/fileupload/domain/model/UploadId.cpp
    src/fileupload/domain/model/FileHash.cpp
    src/fileupload/application/usecase/UploadLdifFileUseCase.cpp
    src/fileupload/application/usecase/UploadMasterListUseCase.cpp
    src/fileupload/infrastructure/controller/FileUploadController.cpp
    src/fileupload/infrastructure/repository/PostgresUploadedFileRepository.cpp
    src/fileupload/infrastructure/adapter/LocalFileStorageAdapter.cpp
)

# File Parsing Context
set(FILEPARSING_SOURCES
    src/fileparsing/domain/model/ParsedCertificate.cpp
    src/fileparsing/domain/model/CrlData.cpp
    src/fileparsing/application/usecase/ParseLdifUseCase.cpp
    src/fileparsing/application/usecase/ParseMasterListUseCase.cpp
    src/fileparsing/infrastructure/adapter/OpenSslLdifParser.cpp
    src/fileparsing/infrastructure/adapter/OpenSslMasterListParser.cpp
)

# Certificate Validation Context
set(CERTVALIDATION_SOURCES
    src/certificatevalidation/domain/model/Certificate.cpp
    src/certificatevalidation/domain/model/ValidationResult.cpp
    src/certificatevalidation/domain/service/TrustChainValidator.cpp
    src/certificatevalidation/domain/service/CrlChecker.cpp
    src/certificatevalidation/application/usecase/ValidateCertificatesUseCase.cpp
    src/certificatevalidation/infrastructure/repository/PostgresCertificateRepository.cpp
    src/certificatevalidation/infrastructure/adapter/OpenSslValidationAdapter.cpp
)

# LDAP Integration Context
set(LDAP_SOURCES
    src/ldapintegration/domain/model/LdapEntry.cpp
    src/ldapintegration/domain/model/DistinguishedName.cpp
    src/ldapintegration/application/usecase/UploadToLdapUseCase.cpp
    src/ldapintegration/application/usecase/LdapHealthCheckUseCase.cpp
    src/ldapintegration/infrastructure/adapter/OpenLdapAdapter.cpp
    src/ldapintegration/infrastructure/adapter/OpenLdapCscaAdapter.cpp
    src/ldapintegration/infrastructure/adapter/OpenLdapCrlAdapter.cpp
)

# Passive Authentication Context
set(PA_SOURCES
    src/passiveauthentication/domain/model/PassportData.cpp
    src/passiveauthentication/domain/model/SecurityObjectDocument.cpp
    src/passiveauthentication/domain/model/DataGroup.cpp
    src/passiveauthentication/domain/model/CrlCheckResult.cpp
    src/passiveauthentication/domain/service/CrlVerificationService.cpp
    src/passiveauthentication/application/usecase/PerformPassiveAuthenticationUseCase.cpp
    src/passiveauthentication/application/usecase/GetPaHistoryUseCase.cpp
    src/passiveauthentication/infrastructure/controller/PassiveAuthenticationController.cpp
    src/passiveauthentication/infrastructure/repository/PostgresPassportDataRepository.cpp
    src/passiveauthentication/infrastructure/adapter/OpenSslSodParser.cpp
    src/passiveauthentication/infrastructure/adapter/Dg1MrzParser.cpp
    src/passiveauthentication/infrastructure/adapter/Dg2FaceImageParser.cpp
    src/passiveauthentication/infrastructure/cache/CrlCacheService.cpp
)

# All sources (for future - currently just main.cpp for hello world)
set(ALL_SOURCES
    src/main.cpp
)

# =============================================================================
# Main Executable
# =============================================================================

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    ${PostgreSQL_LIBRARIES}
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
    ${UUID_LIBRARY}
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    fmt::fmt
)

# =============================================================================
# Testing
# =============================================================================

option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)

    # Unit tests executable
    add_executable(unit_tests
        tests/unit/main_test.cpp
    )

    target_include_directories(unit_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(unit_tests PRIVATE
        Catch2::Catch2WithMain
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(unit_tests)
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/
    DESTINATION include/${PROJECT_NAME}
    FILES_MATCHING PATTERN "*.hpp"
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== ICAO Local PKD Build Configuration ===")
message(STATUS "CMake version:      ${CMAKE_VERSION}")
message(STATUS "C++ compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix:     ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Build tests:        ${BUILD_TESTS}")
message(STATUS "===========================================")
message(STATUS "")
