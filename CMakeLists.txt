cmake_minimum_required(VERSION 3.10)
project(LibUDS C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Include directories
include_directories(include)

# Core Library
set(LIBUDS_SOURCES
    src/core/uds_core.c
    src/transport/uds_tp_isotp.c
)
add_library(uds STATIC ${LIBUDS_SOURCES})

# Examples
add_subdirectory(examples/host_sim)

# Testing
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
