# Exception Handling Library
cmake_minimum_required(VERSION 3.16)

set(LIB_NAME icao-exception)

# Header-only library
add_library(${LIB_NAME} INTERFACE)

target_include_directories(${LIB_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/icao/exception>
)

add_library(icao::exception ALIAS ${LIB_NAME})

message(STATUS "Exception Handling Library configured (header-only)")
