# STM32F103 + bxCAN UDS ECU (CubeMX / HAL)

A buildable **STM32CubeMX / HAL** project for the STM32F103C8 ("Blue Pill")
that answers a diagnostic tester over **classical CAN** and runs the standard
UDS reprogramming sequence on real silicon.

This is the on-target counterpart to [`../pro_flash_tool`](../pro_flash_tool):
that example runs the tester and the ECU together in one host process so you can
read the 17-step flow byte by byte; this one is the **ECU half**, ported out of
bare metal onto the ST HAL with bxCAN as the transport. The LibUDS core and the
service callbacks are the same — only the transport and the flash driver are
real here.

## What it does

- ISO-TP (ISO 15765-2) over bxCAN, OBD-II IDs **0x7E0** (tester→ECU) /
  **0x7E8** (ECU→tester), 500 kbit/s, CAN1 on **PA11/PA12**.
- Serves the reprogramming services a flash tool drives: `0x10` session control,
  `0x85` DTC setting, `0x28` communication control, `0x27` security access,
  `0x2E` programming fingerprint (DID 0xF15A), `0x31` erase / checkMemory,
  `0x34`/`0x36`/`0x37` download, `0x11` ECU reset.
- Writes the incoming image into the **top 8 KB of flash**
  (`0x0800E000`–`0x0800FFFF`) with the real `HAL_FLASH` half-word programmer;
  the running firmware is fenced below by the linker (56 KB).
- Verifies the image with the F103 **hardware CRC unit** (`HAL_CRC_Calculate`).
- Defers `NVIC_SystemReset` until the `0x51` response has physically left the
  CAN mailboxes — the TX-complete gate that real ECUs need (see the `fn_reset`
  note in `uds_config.h`).

The UDS logic lives in `Core/Src/uds_ecu_app.c`, kept out of `main.c` so
regenerating from the `.ioc` never overwrites it.

## Layout

```
f103_cubemx_uds_ecu.ioc      CubeMX project (re-openable / regenerable)
Core/Inc, Core/Src           CubeMX scaffold + uds_ecu_app.{c,h}
Core/Startup                 CMSIS startup (startup_stm32f103xb.s)
STM32F103C8TX_FLASH.ld       linker (56 KB app fence, 20 KB RAM)
Makefile                     bare-metal arm-none-eabi build
```

## Build

The ST HAL / CMSIS trees are **not** vendored into udslib. Get them either way:

**A. From CubeMX** — open `f103_cubemx_uds_ecu.ioc`, *Generate Code*. CubeMX
drops a `Drivers/` tree next to the Makefile and the defaults just work:

```bash
make
```

**B. From ST's repos** — clone the three and point the Makefile at them:

```bash
git clone --depth 1 https://github.com/STMicroelectronics/stm32f1xx_hal_driver
git clone --depth 1 https://github.com/STMicroelectronics/cmsis_device_f1
git clone --depth 1 https://github.com/STMicroelectronics/cmsis_core
make HAL_DIR=$PWD/stm32f1xx_hal_driver \
     CMSIS_DEV=$PWD/cmsis_device_f1 \
     CMSIS_CORE=$PWD/cmsis_core/CMSIS/Core/Include
```

Output is `build/f103_cubemx_uds_ecu.{elf,bin}` (~17 KB flash, ~5 KB RAM).
Flash the `.bin` to a Blue Pill with an ST-Link (`st-flash write build/*.bin
0x08000000`) and drive it with [`../pro_flash_tool`](../pro_flash_tool)'s
sequence from any CAN tester (SocketCAN, a PCAN tool, etc.).

## Test

`test/` holds a host regression test that compiles the **real firmware source**
(`Core/Src/uds_ecu_app.c`, unchanged) against a small HAL shim — CAN becomes
in-memory frame FIFOs, flash a RAM array, the CRC unit the STM32 CRC-32 — and
drives all 17 reprogramming steps through a real ISO-TP tester. It needs no ARM
toolchain or ST HAL, so it runs with the rest of the unit tests:

```bash
cmake -S . -B build && cmake --build build --target f103_cubemx_uds_host_test
ctest --test-dir build -R f103_cubemx_uds_host
```

It checks every step returns a positive response and that the side effects
actually happen: the erase wipes the window, the transferred bytes land in
flash, checkMemory returns a CRC, and ECUReset fires `NVIC_SystemReset`.

## Scope

This shows the **diagnostic sequence** on real hardware — not a production
bootloader. A shipping design splits into a separate bootloader and application
with dual banks, rollback, and a signed image check; the H563 example
(`../h563_uds_bootloader`) demonstrates that side. Erase/program here writes a
plain flash region so the flow stays readable.
