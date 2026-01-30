# LibUDS Architecture

This document describes the design philosophy and structural organization of the LibUDS stack.

## 1. Design Philosophy

LibUDS is built on three core pillars:
1.  **Strict Isolation**: The core logic is isolated from I/O and OS.
2.  **Zero Resource Ownership**: The library does not allocate memory or create threads.
3.  **Dependency Injection**: All platform-specific functionality is "injected" at runtime via function pointers.

## 2. Component Diagram
LibUDS is split into three main areas:

1.  **Core SDU Engine**: Handles UDS service dispatching. It can act as both a **Server** (responding to requests) and a **Client** (sending requests via `uds_client_request`).
2.  **State Manager**: Tracks active sessions, security levels, and protocol timers (S3, P2).
3.  **Transport Abstraction**: Spliced layer that either hooks into OS sockets (Zephyr/Linux) or uses the internal ISO-TP fallback.

```mermaid
graph TD
    App[User Application] -->|uds_client_request| Core
    App -->|uds_init/process| Core
    Core[LibUDS Core] -->|SDU Send| TP[Transport Layer]
    TP -->|CAN Frame| HW[Hardware/HAL]
    HW -->|CAN Frame| TP
    TP -->|SDU Input| Core
    Core -->|Callback| App
```

## 3. The "Spliced Layer" Concept

One of the most complex parts of UDS is the Transport Layer (ISO 15765-2). 
LibUDS solves this by treating the Transport Layer as a "pluggable module".

### SDU vs PDU
- **SDU (Service Data Unit)**: A complete UDS message (e.g., `[0x10, 0x03]`).
- **PDU (Protocol Data Unit)**: An individual CAN frame (e.g., `[0x02, 0x10, 0x03, 0x00...]`).

The `libuds` core logic strictly consumes and produces **SDUs**. 

If the underlying OS (like Zephyr or Linux) provides a native ISO-TP stack, LibUDS talks to it directly at the SDU level. This avoids redundant reassembly logic and saves significant RAM/Flash.

If the OS is "dumb" (Bare Metal), LibUDS provides the `uds_tp_isotp.c` fallback which handles reassembly internally, converting the Core's SDUs into raw CAN PDUs.

## 4. Memory Management

To ensure MISRA-C compliance and high reliability, LibUDS:
- **Never calls `malloc()` or `free()`**.
- Uses caller-provided buffers for RX and TX operations.
- Uses fixed-size message structures.

## 5. Non-Blocking Design (Tickless)

The `uds_process()` function is the heart of the stack. It is designed to be called in a loop but never blocks. It uses the `get_time_ms()` callback to check if internal timers (S3, P2, P2*) have expired. 

This allows `libuds` to be integrated into any scheduling model:
- **Super Loop**: Call it once per loop.
- **RTOS Task**: Call it periodically with `vTaskDelay` or `k_sleep`.
- **Interrupt Mode**: Call it when a timer hardware triggers.
