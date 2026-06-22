# CMake for dtc_clear example
add_executable(dtc_clear_demo main.c)
target_link_libraries(dtc_clear_demo uds)
target_include_directories(dtc_clear_demo PRIVATE ../../src/core)
