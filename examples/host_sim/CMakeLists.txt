# CMake for host_sim example
add_executable(uds_host_sim main.c)
target_link_libraries(uds_host_sim uds)
target_include_directories(uds_host_sim PRIVATE ../../src/core)
