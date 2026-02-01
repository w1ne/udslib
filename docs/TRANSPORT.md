# Transport Layer Architecture

LibUDS uses a modular Transport Layer (ISO 15765-2), allowing it to fit both OS-managed and bare-metal environments.

## 1. SDU vs PDU

- **SDU (Service Data Unit)**: The complete UDS message (max 4095 bytes). The core stack (`uds_core.c`) operates only on SDUs.
- **PDU (Protocol Data Unit)**: Individual CAN frames (8 bytes). The Transport Layer handles segmentation and reassembly.

## 2. Integration Models

### 2.1. Native OS Stack
If the OS (Zephyr, Linux SocketCAN) provides an ISO-TP stack:
1.  Initialize LibUDS with a `tp_send` function that writes to the OS socket.
2.  Pass received SDUs from the socket directly to `uds_input_sdu()`.
3.  The internal `uds_tp_isotp.c` is **not** used.

### 2.2. Internal Fallback (Bare Metal)
If no OS stack is available:
1.  Initialize with `uds_tp_isotp_init(can_send_fn, tx_id, rx_id)`.
2.  Feed raw CAN frames into `uds_isotp_rx_callback()`.
3.  Proces logic via `uds_tp_isotp_process()`.

## 3. Internal ISO-TP States

The fallback implementation handles standard ISO-TP flows:
- **SF (Single Frame)**: Immediate dispatch.
- **FF (First Frame)**: Allocates buffer, sends **FC (Flow Control)**, waits for data.
- **CF (Consecutive Frame)**: Reassembles payload.
- **TX Flow Control**: When sending large SDUs, the stack sends FF and waits for the peer's FC before streaming CFs.

## 4. Hardening & Flow Control

LibUDS implements standard ISO-TP hardening features to ensure robust communication:
- **STmin (Separation Time)**: Enforces minimum time between consecutive frames (CF) to prevent overwhelming the receiver.
- **Block Size (BS)**: Manages data flow by requiring Flow Control (FC) frames after a specified number of CFs.
- **Dynamic Timing**: STmin and Block Size parameters are dynamically extracted from peer Flow Control frames during transmission.

## 5. Virtual CAN (Host Simulation)

For PC-based verification, we encapsulate CAN frames in UDP packets. This allows full stack execution without physical hardware.
