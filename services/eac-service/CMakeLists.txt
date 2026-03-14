cmake_minimum_required(VERSION 3.20)
project(eac-service VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# C++ Standard and Compiler Settings
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
# ICAO Shared Libraries
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shared")
    set(ICAO_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared)
else()
    set(ICAO_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
endif()
add_subdirectory(${ICAO_SHARED_DIR} ${CMAKE_BINARY_DIR}/shared-libs)

# =============================================================================
# Dependencies
# =============================================================================
find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# =============================================================================
# Source Files
# =============================================================================
set(SOURCE_FILES
    src/main.cpp
    src/infrastructure/service_container.cpp
    src/repositories/cvc_certificate_repository.cpp
    src/services/cvc_service.cpp
    src/services/eac_chain_validator.cpp
    src/handlers/eac_upload_handler.cpp
    src/handlers/eac_certificate_handler.cpp
    src/handlers/eac_statistics_handler.cpp
)

# =============================================================================
# Main Application
# =============================================================================
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    icao::database
    icao::cvc-parser
    icao::audit
    icao::config
    icao::exception
    icao::logging
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    spdlog::spdlog
)

# =============================================================================
# Build Info
# =============================================================================
message(STATUS "")
message(STATUS "=== EAC Service Build Configuration ===")
message(STATUS "CMake version:      ${CMAKE_VERSION}")
message(STATUS "C++ compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
message(STATUS "")
