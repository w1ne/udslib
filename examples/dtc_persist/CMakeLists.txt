# CMake for dtc_persist example
add_executable(dtc_persist_demo main.c)
target_link_libraries(dtc_persist_demo uds)
target_include_directories(dtc_persist_demo PRIVATE ../../src/core)
