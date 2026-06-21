# H563 UDS OTA Bootloader Example

This example implements a production-style dual-bank OTA bootloader for the
STM32H563 using UDSLib.  The bootloader lives in the first 96 KB of each
flash bank, validates the active-bank application image on every boot, and
falls back to a UDS programming session when no valid image is present.  A
separate host tool (`host/`) drives the full download sequence over
SocketCAN CAN-FD.  Two minimal demo applications (`app/`) serve as App A
(bank 0) and App B (bank 1) upgrade targets.  AES-128-CMAC (mbedTLS 3.6,
RFC 4493) authenticates the 0x27 Security Access exchange before any
reprogramming is permitted.

## Memory Map

The STM32H563 has two 1 MB flash banks.  The bootloader occupies the first
96 KB of whichever bank is active; the application region starts at offset
`+0x18000` within the same bank.  The OTA image header sits at the very
start of the application region, followed by a 0x3F0-byte reserved pad so
the Cortex-M33 vector table (which requires 1 KB alignment on H563) lands at
`+0x400`.

```
Flash address    Bank 0              Bank 1
0x08000000  ┌──────────────┐   0x08100000  ┌──────────────┐
            │  Bootloader  │               │  Bootloader  │
            │   (96 KB)    │               │   (96 KB)    │
0x08018000  ├──────────────┤   0x08118000  ├──────────────┤
+0x000      │ OTA header   │  +0x000       │ OTA header   │
            │  (16 bytes)  │               │  (16 bytes)  │
+0x010      │ Reserved pad │  +0x010       │ Reserved pad │
            │  (0x3F0 B)   │               │  (0x3F0 B)   │
+0x400      │ App vector   │  +0x400       │ App vector   │
            │ table + code │               │ table + code │
            │  (up to      │               │  (up to      │
            │  ~928 KB)    │               │  ~928 KB)    │
0x08100000  └──────────────┘   0x08200000  └──────────────┘
```

Each bank base (`0x08000000` or `0x08100000`) hosts an identical bootloader
image.  The active bank is selected by the H5 FLASH option bytes; the
bootloader reads `FLASH->OPTSR_CUR.SWAP_BANK` at runtime via
`flash_active_bank()` to locate both the active-bank application (to jump
to) and the inactive-bank application region (the OTA download target).

## OTA Sequence

The host flash tool (`host/flash_tool`) drives these nine UDS steps:

1. **0x10 02** — DiagnosticSessionControl(programming): switches to the
   programming session, which gates all reprogramming services.
2. **0x22 F1A0** — ReadDataByIdentifier(active-bank DID): reads a 1-byte
   indicator (`0` or `1`) so the host can compute the inactive-bank base
   address for the download.
3. **0x27 01** — SecurityAccess(requestSeed): bootloader sends a 16-byte
   nonce (fixed demo seed; see Limitations).
4. **0x27 02** — SecurityAccess(sendKey): host replies with
   AES-128-CMAC(DEMO\_SECRET, seed); bootloader verifies constant-time.
5. **0x34** — RequestDownload: specifies the inactive-bank app base address
   and the image size (OTA header + payload).  Bootloader erases the
   inactive app sectors and arms the transfer state.
6. **0x36 × N** — TransferData: image bytes in chunks.  The bootloader
   accumulates them in a 16-byte staging buffer (H5 requires quad-word
   aligned program operations) and flushes full blocks to flash.
7. **0x37** — RequestTransferExit: flushes the final staging buffer (padded
   with `0xFF`).
8. **0x31 01 FF01** — RoutineControl(CheckProgrammingDependencies): runs the
   same `app_is_valid()` check the bootloader uses at boot (magic,
   `image_size`, CRC-32/ISO-HDLC over payload, initial SP in RAM).  Returns
   `0x01` on pass.
9. **0x31 01 FF02** — RoutineControl(ActivateSoftware): calls
   `flash_set_swap_and_reset()`, which sets `SWAP_BANK` in the H5 option
   bytes and issues a system reset.  The bootloader in the new active bank
   validates the image and jumps to it.

## Building

### Prerequisites

- `arm-none-eabi-gcc` 13.x (with `nano.specs` / `nosys.specs`, i.e. newlib-nano)
- `python3` (for `mkimage.py`)
- `gcc` (host) + `libmbedtls-dev` (for the host flash tool;
  Debian/Ubuntu: `sudo apt install libmbedtls-dev`)

### Bootloader

```sh
UDSLIB_DIR=<path-to-udslib-checkout> make -C bootloader
```

Produces `bootloader/build/h563_uds_bootloader.elf`.

`MBEDTLS_DIR` defaults to
`$(UDSLIB_DIR)/zephyr-workspace/modules/crypto/mbedtls-3.6` (the copy
vendored in the udslib Zephyr workspace) and can be overridden:

```sh
UDSLIB_DIR=~/projects/udslib MBEDTLS_DIR=/opt/mbedtls-3.6 make -C bootloader
```

Host unit tests (run on the build machine, no cross-compiler needed):

```sh
UDSLIB_DIR=<path> make -C bootloader cmac-test   # RFC 4493 AES-CMAC vectors
UDSLIB_DIR=<path> make -C bootloader image-test  # OTA image header/CRC logic
```

### Demo Applications (App A / App B)

```sh
make -C app both
```

Produces in `app/`:
- `app_a.elf`, `app_a.bin`, `app_a_image.bin` (version 0x00010000)
- `app_b.elf`, `app_b.bin`, `app_b_image.bin` (version 0x00020000)

Each image is a raw binary wrapped by `mkimage.py` into the OTA format
(0x400-byte header + payload).  To cross-check the image:

```sh
make -C app image-verify   # validates app_b_image.bin via bootloader logic
```

### Host Flash Tool

```sh
make -C host
```

Produces `host/flash_tool`.  Requires `libmbedtls-dev` for AES-CMAC.

To flash App B over real CAN hardware (e.g. PCAN adapter):

```sh
sudo ip link set can0 up type can bitrate 500000 dbitrate 2000000 fd on
host/flash_tool can0 app/app_b_image.bin
```

## Validation

| Gate | How |
|---|---|
| Bootloader compiles | `UDSLIB_DIR=. make -C bootloader` |
| AES-CMAC RFC 4493 vectors | `make -C bootloader cmac-test` (host, no MCU) |
| OTA image header/CRC logic | `make -C bootloader image-test` (host, no MCU) |
| App B image well-formed | `make -C app image-verify` (host, no MCU) |
| Host tool CAN-FD framing | `host/test_vcan.sh` (requires `vcan` kernel module) |
| Full download → swap → boot loop | Validated in the labwired-core STM32H563 simulation (see w1ne/labwired-core#326); requires a simulated H563 environment |
| Live host ↔ ECU over real CAN | Manual: `host/flash_tool can0 app/app_b_image.bin` (requires CAN hardware, e.g. PCAN) |

## Limitations / Not Yet Implemented

**a. No boot-confirmation / auto-rollback of an unhealthy application.**
If App B resets repeatedly before it can signal "healthy" (a boot-confirm
flag in a dedicated flash sector), the bootloader has no mechanism to
automatically roll back to App A.  The current safety guarantee is weaker:
power loss *during* the download leaves the active bank intact (the inactive
bank is only swapped in after a successful CheckProgrammingDependencies
pass), but a valid-but-buggy App B that crashes on every boot will remain
the active image.  Implementing a boot counter / watchdog-triggered rollback
is tracked as a future task in `main.c` (search for `TODO (Task 6)`).

**b. `uart_init` does not configure RCC or GPIO.**
The UART output (`BL-START`, `BL-JUMP`, etc.) works in the labwired-core
simulation because the simulator pre-initializes peripherals, but on real
silicon `uart_init` must additionally enable the USART clock via RCC and
configure the TX/RX alternate-function GPIO pins before the UART is usable.

**c. The 0x27 secret and seed are fixed demo values.**
`DEMO_SECRET` and `DEMO_SEED` in `bootloader/main.c` and
`host/flash_tool.c` are hard-coded example constants.  In a production ECU
the key must not reside in flash in the clear; derive it from an HSM or SHE
key slot.  The seed must be a TRNG-derived per-attempt nonce, not a fixed
value.

**d. The vcan end-to-end host ↔ ECU harness is not wired.**
`host/test_vcan.sh` verifies ISO-TP frame construction on a loopback vcan
interface but does not run a stub ECU responder.  Frame logic is unit-tested;
the real validation path for the full protocol exchange is either the
labwired-core simulation (see above) or real CAN hardware.
