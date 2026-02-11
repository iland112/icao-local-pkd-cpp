cmake_minimum_required(VERSION 3.20)
project(monitoring-service CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Drogon CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(CURL REQUIRED)

# PostgreSQL - use system or vcpkg package
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)
link_directories(${LIBPQ_LIBRARY_DIRS})

# Source files
set(SOURCES
    src/main.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    spdlog::spdlog
    JsonCpp::JsonCpp
    ${LIBPQ_LIBRARIES}
    CURL::libcurl
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${LIBPQ_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
