# Transport Layer Architecture

LibUDS utilizes a "Spliced Layer" design for the Transport Layer (ISO 15765-2), allowing it to remain lightweight while supporting complex multi-frame communication.

## 1. SDU vs PDU Separation

- **SDU (Service Data Unit)**: The complete UDS message (max 4095 bytes in standard ISO-TP).
- **PDU (Protocol Data Unit)**: Individual CAN frames (8 bytes for CAN 2.0).

The core stack (`uds_core.c`) only deals with SDUs. Segmenting these into PDUs is the responsibility of the Transport Layer.

## 2. Integration Models

### 2.1. Native OS (High-Level)
If your OS (e.g., Zephyr, Linux SocketCAN) provides a native ISO-TP stack:
1.  Initialize LibUDS with a `tp_send` function that writes to the OS socket.
2.  When the OS socket receives a full message, call `uds_input_sdu()`.
3.  In this model, the internal `uds_tp_isotp.c` is **not** used.

### 2.2. Internal Fallback (Bare Metal)
If no OS stack exists, use `uds_tp_isotp.c`:
1.  Initialize with `uds_tp_isotp_init(can_send_fn, tx_id, rx_id)`.
2.  Hook your raw CAN driver RX to `uds_isotp_rx_callback()`.
3.  Call `uds_tp_isotp_process()` in your main loop.

## 3. Multi-Frame State Machine

The internal fallback implements the standard ISO-TP state machine:
- **SF (Single Frame)**: Immediate dispatch.
- **FF (First Frame)**: Caches data, sends **FC (Flow Control)**, enters `ISOTP_RX_WAIT_CF`.
- **CF (Consecutive Frame)**: Reassembles data until `msg_len` is reached.
- **FC (Flow Control)**: Triggered during TX. If the core sends a large SDU, the TP layer sends FF, then waits for FC before resuming with CFs in the `process()` loop.

## 4. Virtual CAN over UDP

For host-based verification (Linux/Windows), we use a virtual CAN layer:
- CAN frames are encapsulated in UDP packets.
- This allows running the full stack on a PC without physical CAN hardware.
- The `uds_host_sim` example demonstrates this integration.
