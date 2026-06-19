# CMake for dtc_store example
add_executable(dtc_store_demo main.c)
target_link_libraries(dtc_store_demo uds)
target_include_directories(dtc_store_demo PRIVATE ../../src/core)
