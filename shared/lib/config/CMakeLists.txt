# Configuration Management Library
cmake_minimum_required(VERSION 3.16)

set(LIB_NAME icao-config)

add_library(${LIB_NAME} STATIC config_manager.cpp)

target_include_directories(${LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/icao/config>
)

find_package(spdlog REQUIRED)
target_link_libraries(${LIB_NAME} PUBLIC spdlog::spdlog)

target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)

add_library(icao::config ALIAS ${LIB_NAME})

message(STATUS "Configuration Management Library configured")
