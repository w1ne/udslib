# White Paper: UDSLib Architecture

## Overview
UDSLib is a portable UDS (ISO 14229-1) protocol stack for safety-critical systems.

## 1. Safety by Design
Dynamic memory allocation is a common source of failure in long-running embedded systems. UDSLib avoids this with a **static memory model**.
**No Malloc**. The stack performs zero runtime allocation.
**Caller-Owned Buffers**. Applications allocate buffers and pass them to the stack.
**Overflow Protection**. Internal checks prevent multi-packet read operations (SID 0x22) from exceeding buffer limits.

## 2. Deterministic Timing
UDSLib uses a "Tickless" timing engine to maintain protocol compliance without monopolizing the CPU.
**Monotonic Timing**. Protocol timers (P2/P2*) rely on absolute system time, eliminating tick-drift.
**Non-Blocking**. The stack processes states without blocking the main loop, allowing critical tasks like motor control to preempt it.
**Asynchronous Dispatch**. The stack handles "Response Pending" (NRC 0x78) logic for long operations automatically.

## 3. The "Safety Gate"
Executing diagnostic requests immediately can be dangerous. A diagnostic tool should not be able to reset an ECU while a vehicle is in motion. UDSLib enforces **Safety Gates**.
**Application Approval**. Destructive operations (Flash, Reset, Write) require explicit approval from the application layer via a callback.
**ECU Authority**. The application knows the vehicle state; the protocol stack does not. The application always has final say.

## 4. Developer Ecosystem
Debugging binary protocols is slow. UDSLib includes tools to help engineers see what is happening:
**Wireshark Dissector**. A LUA script decodes raw UDS frames into readable protocol messages within Wireshark.
**Python Bindings**. A `ctypes` wrapper exposes the C stack to Python, enabling scriptable integration tests on a host machine.
**Zephyr Integration**. The repository includes specific guides for integrating with Zephyr RTOS.

## 5. Verification
The stack ships with the tests used to verify it:
**Host-Based Testing**. Protocol logic is verified on the host.
**Fuzzing**. The parser is tested against malformed packets to ensure stability.
**MISRA-C**. The codebase follows MISRA-C:2012 guidelines.
