# ICAO 9303 Parser Library
# SOD, Data Group, and MRZ parsing for ICAO 9303 compliant passports
cmake_minimum_required(VERSION 3.16)

# Library name
set(LIB_NAME icao-icao9303)

# Source files
set(ICAO9303_SOURCES
    sod_parser.cpp
    dg_parser.cpp
)

# Create static library
add_library(${LIB_NAME} STATIC ${ICAO9303_SOURCES})

# Include directories
target_include_directories(${LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/models>
    $<INSTALL_INTERFACE:include/icao/icao9303>
)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)

# Optional: JPEG2000 → JPEG conversion (for DG2 face images)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(OPENJP2 QUIET libopenjp2)
    pkg_check_modules(LIBJPEG QUIET libjpeg)
endif()

if(OPENJP2_FOUND AND LIBJPEG_FOUND)
    target_compile_definitions(${LIB_NAME} PRIVATE HAS_OPENJPEG)
    target_include_directories(${LIB_NAME} PRIVATE ${OPENJP2_INCLUDE_DIRS} ${LIBJPEG_INCLUDE_DIRS})
    message(STATUS "  - JPEG2000 conversion: ENABLED (OpenJPEG + libjpeg)")
else()
    message(STATUS "  - JPEG2000 conversion: DISABLED (install libopenjp2-dev libjpeg-dev to enable)")
endif()

# Link libraries
target_link_libraries(${LIB_NAME} PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    # Note: jsoncpp is linked by parent service (pa-service, pkd-management)
)

if(OPENJP2_FOUND AND LIBJPEG_FOUND)
    target_link_libraries(${LIB_NAME} PRIVATE ${OPENJP2_LIBRARIES} ${LIBJPEG_LIBRARIES})
endif()

# Compiler features
target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)

# Compiler options
target_compile_options(${LIB_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Create alias for namespaced usage
add_library(icao::icao9303 ALIAS ${LIB_NAME})

# Installation rules
include(GNUInstallDirs)

install(TARGETS ${LIB_NAME}
    EXPORT icao-icao9303-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(FILES
    sod_parser.h
    dg_parser.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/icao/icao9303
)

install(DIRECTORY models/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/icao/icao9303/models
    FILES_MATCHING PATTERN "*.h"
)

# Export targets
install(EXPORT icao-icao9303-targets
    FILE icao-icao9303-targets.cmake
    NAMESPACE icao::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-icao9303
)

# Create package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/icao-icao9303-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/icao-icao9303-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-icao9303
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/icao-icao9303-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/icao-icao9303
)

# =============================================================================
# Testing (Optional)
# =============================================================================
option(BUILD_ICAO9303_TESTS "Build icao::icao9303 unit tests" OFF)

if(BUILD_ICAO9303_TESTS)
    enable_testing()

    # jsoncpp is required by the parsers (normally linked by parent service)
    find_package(jsoncpp CONFIG REQUIRED)

    # Try find_package first (vcpkg), fallback to FetchContent
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    set(ICAO9303_TEST_SOURCES
        tests/test_sod_parser.cpp
        tests/test_dg_parser.cpp
    )

    add_executable(icao_icao9303_tests ${ICAO9303_TEST_SOURCES})

    target_include_directories(icao_icao9303_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(icao_icao9303_tests PRIVATE
        ${LIB_NAME}
        JsonCpp::JsonCpp
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(icao_icao9303_tests)

    message(STATUS "  - Unit tests: ENABLED (BUILD_ICAO9303_TESTS=ON)")
endif()

# Display configuration
message(STATUS "ICAO 9303 Parser Library configured")
message(STATUS "  - SOD Parser: ENABLED")
message(STATUS "  - Data Group Parser: ENABLED")
message(STATUS "  - MRZ Parser: ENABLED (via DG1)")
if(BUILD_ICAO9303_TESTS)
    message(STATUS "  - Tests: ENABLED")
else()
    message(STATUS "  - Tests: DISABLED (use -DBUILD_ICAO9303_TESTS=ON to enable)")
endif()
