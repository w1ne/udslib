cmake_minimum_required(VERSION 3.10)
project(UDSLib C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Code Coverage Support
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
endif()

# Include directories
include_directories(include)
include_directories(src/core)

# Core Library
set(UDSLIB_SOURCES
    src/core/uds_core.c
    src/services/uds_service_session.c
    src/services/uds_service_data.c
    src/services/uds_service_security.c
    src/services/uds_service_maintenance.c
    src/services/uds_service_flash.c
    src/services/uds_service_mem.c
    src/transport/uds_tp_isotp.c
)
add_library(udslib STATIC ${UDSLIB_SOURCES})
add_library(udslib_shared SHARED ${UDSLIB_SOURCES})
set_target_properties(udslib udslib_shared PROPERTIES OUTPUT_NAME udslib)

# Temporary compatibility aliases (old target names)
add_library(uds ALIAS udslib)
add_library(uds_shared ALIAS udslib_shared)

# Examples
add_subdirectory(examples/host_sim)

# Testing
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
