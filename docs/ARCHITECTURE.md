# UDSLib Architecture

UDSLib separates core diagnostic logic from the input/output layer and the operating system.

## 1. Design Principles

Three rules define the library's structure:

1.  **Strict Isolation**: Logic runs independently of I/O and OS specifics.
2.  **No Resource Ownership**: The library does not allocate memory or create threads.
3.  **Dependency Injection**: Platform-specific functions are injected at runtime via function pointers.

## 2. Component Structure

The library divides into four main areas:

1.  **Service Registry**: A table-driven dispatcher that routes requests.
2.  **Core SDU Engine**: Parses and validates protocol requirements (Session, Security, Length).
3.  **State Manager**: Tracks sessions, security levels, and timers (S3, P2). It uses optional hooks for NVM persistence.
4.  **Transport Abstraction**: A layer that connects to OS sockets (Zephyr/Linux) or uses the internal ISO-TP fallback.

```mermaid
graph TD
    App[User Application] -->|uds_client_request| Core
    App -->|uds_init/process| Core
    Core -->|Lookup| Registry[Service Registry]
    Registry -->|Handler| Ext[External/User Service]
    Registry -->|Handler| Builtin[Built-in Service]
    Core -->|Condition Check| Gate{Safety Gate}
    Gate -->|Passed| Registry
    Gate -->|Rejected| NRC[Send NRC 0x22]
    Core[UDSLib Core] -->|SDU Send| TP[Transport Layer]
    TP -->|CAN Frame| HW[Hardware/HAL]
    HW -->|CAN Frame| TP
    TP -->|SDU Input| Core
    Core -->|Callback| App
```

## 3. Modular Service Registry

A table-driven dispatcher manages UDS services.

- **Scalability**: Adding a service (like SID 0x29) requires adding an entry to the service table.
- **Extensibility**: Applications register `user_services` in `uds_config_t` to override or extend standard functionality.
- **Validation**: The core engine enforces ISO 14229-1 NRC priorities (Session → Subfunction → Length → Security → Safety) before calling the handler.

## 4. Safety Gates

For industrial and automotive safety, UDSLib implements **Safety Gates**.

Every potentially destructive service (Reset, Write, Download) passes through an application-provided `fn_is_safe` callback.

- If the callback returns `false`, the stack rejects the request with **NRC 0x22 (ConditionsNotCorrect)**.
- This allows engineers to block diagnostics when the machine is in an unsafe state.

## 5. State Persistence (NVM)

The protocol state (Active Session and Security Level) persists across power cycles using **NVM Persistence Hooks**.

- `fn_nvm_save`: Runs when the session or security level changes.
- `fn_nvm_load`: Runs during `uds_init` to restore the last known valid state.

## 6. Spliced Transport Layer

The Transport Layer (ISO 15765-2) operates as a pluggable module.

### SDU vs PDU
- **SDU (Service Data Unit)**: A complete UDS message (e.g., `[0x10, 0x03]`).
- **PDU (Protocol Data Unit)**: A single CAN frame (e.g., `[0x02, 0x10, 0x03, 0x00...]`).

The `udslib` core logic strictly consumes and produces **SDUs**.

If the underlying OS (like Zephyr or Linux) has a native ISO-TP stack, UDSLib communicates directly at the SDU level. This removes redundant reassembly logic.

If the OS is "dumb" (Bare Metal), UDSLib uses the `uds_tp_isotp.c` fallback to handle reassembly, converting SDUs into raw CAN PDUs.

## 7. Memory Management

To ensure MISRA-C compliance and reliability:
- The library **never calls `malloc()` or `free()`**.
- The caller provides buffers for RX and TX operations.
- Message structures use fixed sizes.

## 8. Non-Blocking Design

The `uds_process()` function runs the stack. It is designed for a loop and does not block. It uses the `get_time_ms()` callback to check if internal timers (S3, P2, P2*) have expired.

This allows `udslib` to fit into different scheduling models:
- **Super Loop**: Call once per loop.
- **RTOS Task**: Call periodically with `vTaskDelay` or `k_sleep`.
- **Interrupt Mode**: Call when a hardware timer triggers.
