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
)

# Services
set(SERVICES_SRC
    src/services/sod_parser_service.cpp
    src/services/data_group_parser_service.cpp
    src/services/certificate_validation_service.cpp
    src/services/pa_verification_service.cpp
)

# =============================================================================
# Main Application
# =============================================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${DOMAIN_MODELS_SRC}
    ${REPOSITORIES_SRC}
    ${SERVICES_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    ${LDAP_LIBRARY}
    ${LBER_LIBRARY}
    ${UUID_LIBRARY}
)

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
message(STATUS "==========================================")
message(STATUS "")
