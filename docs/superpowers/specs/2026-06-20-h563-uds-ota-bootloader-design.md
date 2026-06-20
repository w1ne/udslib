# STM32H563 UDS Service-Based OTA Bootloader — Design

**Issue:** [#64](https://github.com/w1ne/udslib/issues/64)
**Date:** 2026-06-20
**Status:** Approved (design)

## Goal

Provide a real, on-device UDS service-based OTA bootloader example for the
STM32H563, driven over CAN-FD using the udslib protocol stack. A new firmware
image is delivered with the standard ISO-14229 reprogramming sequence
(programming session → security access → erase → request download → transfer
data → transfer exit → verify → activate → reset), written to real flash,
validated, and activated via the H5 hardware bank swap.

The full firmware-over-CAN loop can be driven by any UDS client with a CAN-FD
interface (e.g. the reporter's PCAN tool). Because no PCAN hardware is available
locally, the loop is validated in the labwired-core H563 simulation, the flash
driver and bank-swap/jump path are validated on a real H563 over SWD, and a
SocketCAN host flash tool is shipped for users who do have a CAN interface.

## Key correctness point: mirrored bootloader + hardware SWAP_BANK

The STM32H5 `SWAP_BANK` option bit remaps the **entire** inactive bank to
`0x08000000` at boot — including whatever sits at the bank's base. Therefore, to
support "Bank 1 = BL + App A, Bank 2 = App B, then swap," the **bootloader is
mirrored at the base of both banks**.

Consequences:

- The OTA path writes **only the app region** of the inactive bank.
- The two bootloader copies are flashed once at the factory over SWD and are
  **never** updated over CAN. The recovery path cannot be bricked remotely.
- After a swap, the (identical) bootloader runs from the now-active bank and
  decides whether to jump to that bank's app or stay for recovery.

## Memory map (per 1 MB bank, bootloader mirrored)

```
Bank 1 @0x08000000           Bank 2 @0x08100000
┌─────────────────┐ +0x00000 ┌─────────────────┐
│ Bootloader      │  96 KB    │ Bootloader      │  ← identical copy (SWD-flashed)
│ (udslib+mbedTLS)│          │ (identical)     │
├─────────────────┤ +0x18000 ├─────────────────┤
│ App A           │          │ App B (OTA dst) │  ← only this region written over CAN
│ + validity hdr  │ ~928 KB  │ + validity hdr  │
└─────────────────┘ +0x100000└─────────────────┘
```

- Device: STM32H563ZI — 2 MB flash (2 × 1 MB banks), 640 KB SRAM.
- Flash: 8 KB sectors, 16-byte (quad-word) program granularity.
- Validity header/footer per app: magic + image length + CRC32, checked by the
  bootloader before jump.
- Confirmed against the connected board over SWD: initial SP `0x200A0000`
  (640 KB SRAM top), reset handler `0x08000400`.

## Components

| Unit | Responsibility | Depends on |
|---|---|---|
| `bootloader/flash_h5.c` | unlock, quad-word program, sector erase, set `SWAP_BANK` + `OBL_LAUNCH` | H5 FLASH registers |
| `bootloader/fdcan_isotp.c` | FDCAN init + glue to `uds_tp_isotp` with `set_can_fd(1)` | udslib TP |
| `bootloader/main.c` | udslib server: `restrict_sessions`, flash service (0x34/36/37/31), 0x27 AES-CMAC, 0x10/0x11/0x3E; validity check; jump-to-app (set MSP, VTOR, branch) | udslib, mbedTLS |
| `app/` | tiny demo app (LED + a 0x22 DID returning "A"/"B"), linked at the app offset, carries a validity footer | — |
| `host/flash_tool.c` | extends the `pro_flash_tool` flow over SocketCAN (vcan in CI, PCAN for the reporter); takes a `.bin`, computes CRC | udslib client |
| `labwired-core/examples/h563-uds-bootloader/` | thin sim harness: builds the bootloader ELF and runs the FDCAN OTA loop end-to-end as a CI smoke test | labwired H563 model |

Each unit has a single clear purpose and a narrow interface: the flash driver
exposes program/erase/swap; the transport exposes send/receive frames; main
wires udslib callbacks to them. They are testable independently.

## OTA data flow

1. `0x10 02` — enter programming session (gated by `restrict_sessions`).
2. `0x27 01` / `0x27 02` — security access seed/key via AES-CMAC (reuses the
   `examples/security_access_mbedtls` path; software AES-CMAC, no HW dependency).
3. `0x31` — erase routine over the inactive bank's app region.
4. `0x34` RequestDownload(inactive app base, size) → `maxNumberOfBlockLength`.
5. `0x36` TransferData × N — 64-byte CAN-FD frames, ISO-TP segmented.
6. `0x37` RequestTransferExit.
7. `0x31` — verify-CRC routine over the written image.
8. `0x31` — activate routine: set `SWAP_BANK`.
9. `0x11` — ECU reset → boots the new bank.

## Error handling & rollback

- **CRC mismatch / bad transfer** → negative response, no swap; App A keeps running.
- **Power loss mid-transfer** → inactive bank invalid, active bank untouched →
  still boots App A; retry the OTA.
- **App B boots but is unhealthy** → A/B *confirm* flag: App B must set a
  "boot OK" flag within N boots; if it does not, the bootloader clears
  `SWAP_BANK` and falls back to App A.
- **Security** → the bootloader region is never CAN-writable; programming session
  + 0x27 are required for every reprogramming service.

## Transport

CAN-FD (64-byte frames). udslib's ISO-TP layer (`src/transport/uds_tp_isotp.c`)
already supports CAN-FD: `use_can_fd`/`tx_dl`, DLC alignment to 64, and FD
SF/FF/CF framing. Enabled via `uds_tp_isotp_set_can_fd(1)`. No TP changes needed.

## Validation matrix

| Aspect | Where | How |
|---|---|---|
| Transport + bank swap + jump | labwired H563 sim | CI smoke test: full loop, assert App B runs after swap; bad-CRC test asserts rollback |
| Flash driver (program/erase/read-back), `SWAP_BANK` + reset | real H563 | SWD via probe-rs / openocd |
| Host flash tool | CI | driven against `vcan0` |
| Real firmware over real CAN wire | reporter only | documented as reporter-validated (no local PCAN) |

## Repo layout

```
udslib/examples/h563_uds_bootloader/
  bootloader/        firmware (udslib + mbedTLS)
  app/               tiny demo app image
  host/              SocketCAN flash tool
  README.md
labwired-core/examples/h563-uds-bootloader/
  thin sim harness → builds the udslib bootloader and runs the OTA loop
```

## Out of scope (this slice)

- F103 bxCAN (classic CAN) variant — follow-up phase once H563 is proven.
- Updating the bootloader itself over CAN (intentionally not supported).
- Production key management / secure boot chain (the example shows the hook;
  real key provisioning is the integrator's responsibility).

## Open item to confirm during planning

The exact toolchain/build pattern used by the existing
`labwired-core/examples/h563-uds-ecu`, so the bootloader firmware builds and
sim-runs the same way.
