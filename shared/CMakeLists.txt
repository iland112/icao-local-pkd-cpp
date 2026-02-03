# ICAO PKD - Shared Libraries
# Top-level CMakeLists.txt for all shared libraries

cmake_minimum_required(VERSION 3.15)
project(icao-shared VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_AUDIT_LIB "Build audit logging library" ON)

# Add subdirectories
if(BUILD_AUDIT_LIB)
    add_subdirectory(lib/audit)
endif()

# Future shared libraries:
# add_subdirectory(lib/database)
# add_subdirectory(lib/ldap)
# add_subdirectory(lib/config)
# add_subdirectory(lib/exception)
# add_subdirectory(lib/logging)
