# H563 UDS OTA Bootloader Example

Dual-bank OTA bootloader for the STM32H563 using UDSLib.

## Layout

| Region | Address | Size |
|---|---|---|
| Bootloader | `0x0800_0000` | 96 KB |
| App bank 1 (active) | `0x0801_8000` | 928 KB |
| App bank 2 (OTA dest) | `0x0811_8000` | 928 KB |

## Build

```
UDSLIB_DIR=<path-to-udslib-checkout> make -C bootloader
```

Produces `bootloader/build/h563_uds_bootloader.elf`.

Requires `arm-none-eabi-gcc` (tested with 13.2) with newlib-nano (`nano.specs` /
`nosys.specs`). The firmware links mbedTLS against the real C library, so the
standard `<string.h>`/`<stdlib.h>` headers and `memcpy`/`malloc`/`calloc` come
from newlib; `bootloader/syscalls.c` supplies the `_sbrk` heap glue.

## Tasks

- [x] Task 1: scaffold — partitioned linker, UART boot message `BL-START`
- [ ] Task 2: STM32H563 flash driver (erase/write sector)
- [ ] Task 3: UDS server (0x10/0x27/0x34/0x36/0x37 services)
- [ ] Task 4: bank-swap and jump-to-app
- [ ] Task 5: demo application (built separately, flashed to bank 1)
