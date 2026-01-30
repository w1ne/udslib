zephyr_library()

# Core UDS stack (always included)
zephyr_library_sources(
    ../src/core/uds_core.c
    uds_zephyr_port.c
)

# Zephyr-specific ISO-TP shim
if(CONFIG_LIBUDS_TRANSPORT_NATIVE)
    zephyr_library_sources(
        uds_zephyr_isotp.c
    )
endif()

# Transport layer: Internal fallback (only if not using Zephyr native)
if(CONFIG_LIBUDS_TRANSPORT_FALLBACK)
    zephyr_library_sources(
        ../src/transport/uds_tp_isotp.c
        uds_zephyr_tp_fallback.c
    )
endif()

# Include directories
zephyr_include_directories(../include)

# Compiler flags
zephyr_library_compile_options(
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
)

# Optional: Enable dead code elimination
zephyr_library_compile_options(
    -ffunction-sections
    -fdata-sections
)
