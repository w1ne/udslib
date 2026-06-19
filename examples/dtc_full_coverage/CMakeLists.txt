# CMake for dtc_full_coverage example
add_executable(dtc_full_coverage_demo main.c)
target_link_libraries(dtc_full_coverage_demo uds)
target_include_directories(dtc_full_coverage_demo PRIVATE ../../src/core)
