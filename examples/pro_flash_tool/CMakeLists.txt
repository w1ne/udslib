# CMake for the reprogramming demo (in-process flash tool <-> ECU)
add_executable(pro_flash_tool main.c)
target_link_libraries(pro_flash_tool uds)
target_include_directories(pro_flash_tool PRIVATE ../../src/core)
