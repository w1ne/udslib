# pro_flash_tool — the 17-step UDS reprogramming sequence

An end-to-end ECU reprogramming demo: a flash "tool" drives a LibUDS ECU
server through the **canonical 17-step OEM flash flow** that every automotive
bootloader follows (ISO 14229-1 services, ISO 14229-3 reprogramming sequence).

Tool and ECU run in a single process, so you can read the exact request and
response bytes a real diagnostic tester would exchange over CAN, without any
hardware. The program returns non-zero if any step fails, so it doubles as a CI
smoke test.

## Build & run

Built by the top-level CMake:

```bash
cmake -S . -B build && cmake --build build --target pro_flash_tool
./build/examples/pro_flash_tool/pro_flash_tool
```

Expected tail:

```
[14] ECUReset(hardReset)            OK
[15] DiagnosticSession(extended)    OK
[16] CommunicationControl(enable)   OK
[17] ControlDTCSetting(on)          OK
=== Reprogramming sequence complete ===
```

## The 17 steps

| # | Request | Service | Why |
|--:|---------|---------|-----|
| 1 | `10 03` | DiagnosticSessionControl → extended | Leave the default session; unlock the maintenance services. |
| 2 | `85 02` | ControlDTCSetting → OFF | Stop the DTC engine so flashing noise is not logged as faults. |
| 3 | `28 03 01` | CommunicationControl → disableRxAndTx | Silence the ECU's periodic application messages on the bus. |
| 4 | `10 02` | DiagnosticSessionControl → programming | Enter the programming session; reprogramming services unlock here. |
| 5 | `27 01` | SecurityAccess → requestSeed | Ask the ECU for a seed. |
| 6 | `27 02` | SecurityAccess → sendKey | Answer with the computed key (demo: `key = seed ^ 0xFF`). |
| 7 | `2E F15A …` | WriteDataByIdentifier → fingerprint | Record who is flashing and when, before erasing anything. |
| 8 | `31 01 FF00` | RoutineControl → eraseMemory | Erase the application flash region. |
| 9 | `34 …` | RequestDownload | Announce target address + size; ECU sizes its receive buffer. |
| 10 | `36 01 …` | TransferData #1 | First image block. |
| 11 | `36 02 …` | TransferData #2 | Next image block (repeat until the image is sent). |
| 12 | `37` | RequestTransferExit | Tell the ECU the transfer is finished. |
| 13 | `31 01 0202` | RoutineControl → checkMemory | Verify the image (CRC / programming-dependency check). |
| 14 | `11 01` | ECUReset → hardReset | Reboot into the application just written. |
| 15 | `10 03` | DiagnosticSessionControl → extended | Re-establish a diagnostic session after the reboot. |
| 16 | `28 00 01` | CommunicationControl → enableRxAndTx | Bring the ECU's normal CAN traffic back. |
| 17 | `85 01` | ControlDTCSetting → ON | Resume fault logging. |

Steps 1–3 are the "pre-programming" preamble, 4–7 unlock and stamp the ECU,
8–13 move and verify the image, and 14–17 activate it and restore normal
operation.

## Mapping to a real STM32F103 + bxCAN target

This demo wires the tester and the ECU together in one process. On real
hardware the same 17 requests arrive over the CAN bus and are dispatched by the
identical LibUDS core; only the transport and the service callbacks change.
For a buildable STM32F103 firmware that links this library and answers UDS over
the **bxCAN** peripheral, see `examples/f103-uds-ecu/` in the companion
`labwired-core` repository (bare-metal startup + ISO-TP over bxCAN).

What each demo callback becomes on an F103 ECU:

| Demo callback | On STM32F103 + bxCAN |
|---------------|----------------------|
| `ecu_send()` | Pack the UDS PDU into ISO-TP frames and push them to the bxCAN TX mailboxes (`CAN1->sTxMailBox`). |
| `uds_input_sdu()` | Called from the bxCAN RX FIFO interrupt after ISO-TP reassembly (`CAN1->sFIFOMailBox`). |
| `ecu_comm_control()` | Enable/disable your periodic application TX (e.g. the lamp status frames) — leave the diagnostic IDs running. |
| `ecu_write_fingerprint()` | Persist the fingerprint to a known flash page / backup SRAM. |
| `ecu_routine()` erase | `FLASH->KEYR` unlock, then page erase of the application region (F103 pages are 1 KB / 2 KB). |
| `ecu_transfer()` | Half-word program (`FLASH_CR_PG`) into the application region as blocks arrive. |
| `ecu_routine()` checksum | CRC over the written flash (the F103 has a hardware CRC unit at `CRC_BASE`). |
| `ecu_reset()` | `NVIC_SystemReset()` — but only **after** the 0x51 response has physically left the bus (see the `fn_reset` note in `uds_config.h`). |

On a CubeMX/HAL project the wiring is the same: generate a project with **CAN
activated** (PA11/PA12 for CAN1), feed `HAL_CAN_GetRxMessage()` output into the
ISO-TP layer, and transmit with `HAL_CAN_AddTxMessage()`. The 17-step logic
above is transport-agnostic — it lives entirely in the LibUDS core and your
service callbacks.

> Erase, program, and reset are the application-specific, safety-critical parts
> of a production bootloader (dual-bank, rollback, signature checks). This
> example shows the diagnostic sequence, not a production flash driver.
