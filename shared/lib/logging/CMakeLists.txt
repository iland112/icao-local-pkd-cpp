# Structured Logging Library
cmake_minimum_required(VERSION 3.16)

set(LIB_NAME icao-logging)

# Header-only library
add_library(${LIB_NAME} INTERFACE)

target_include_directories(${LIB_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/icao/logging>
)

find_package(spdlog REQUIRED)
target_link_libraries(${LIB_NAME} INTERFACE spdlog::spdlog)

add_library(icao::logging ALIAS ${LIB_NAME})

message(STATUS "Structured Logging Library configured (header-only)")
