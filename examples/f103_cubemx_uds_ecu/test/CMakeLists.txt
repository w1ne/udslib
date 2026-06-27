# Host regression test for the F103 CubeMX UDS ECU example.
#
# Compiles the REAL firmware source (../Core/Src/uds_ecu_app.c) unchanged against
# a tiny host HAL shim (CAN -> in-memory FIFOs, FLASH -> RAM, CRC -> STM32 CRC32)
# and drives all 17 reprogramming steps through a real ISO-TP tester. No ARM
# toolchain or ST HAL needed - it runs anywhere the unit tests do.
#
# The shim include dir comes first so its main.h / stm32f1xx_hal.h shadow the
# example's CubeMX headers.
add_executable(f103_cubemx_uds_host_test
    test_17step.c
    ../Core/Src/uds_ecu_app.c)

target_include_directories(f103_cubemx_uds_host_test BEFORE PRIVATE
    shim
    ../Core/Inc)
target_include_directories(f103_cubemx_uds_host_test PRIVATE ../../../src/core)

target_link_libraries(f103_cubemx_uds_host_test uds)

add_test(NAME f103_cubemx_uds_host COMMAND f103_cubemx_uds_host_test)
