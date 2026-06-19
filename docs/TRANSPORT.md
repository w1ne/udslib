# Transport Layer Architecture

UDSLib uses a modular Transport Layer (ISO 15765-2), allowing it to fit both OS-managed and bare-metal environments.

## 1. SDU vs PDU

- **SDU (Service Data Unit)**: The complete UDS message (max 4095 bytes). The core stack (`uds_core.c`) operates only on SDUs.
- **PDU (Protocol Data Unit)**: Individual CAN frames (8 bytes). The Transport Layer handles segmentation and reassembly.

## 2. Integration Models

### 2.1. Native OS Stack
If the OS (Zephyr, Linux SocketCAN) provides an ISO-TP stack:
1.  Initialize UDSLib with a `tp_send` function that writes to the OS socket.
2.  Pass received SDUs from the socket directly to `uds_input_sdu()`.
3.  The internal `uds_tp_isotp.c` is **not** used.

### 2.2. Internal Fallback (Bare Metal)
If no OS stack is available, the internal ISO-TP engine is fully **instance-based**: the application allocates a `uds_isotp_ctx_t` (and a TX SDU cache buffer) and passes it to every call, so multiple independent channels can run concurrently with zero dynamic allocation.

1.  Allocate a context and a TX SDU cache buffer, then initialize:
    ```c
    static uds_isotp_ctx_t iso;
    static uint8_t iso_tx_sdu[1024];

    uds_tp_isotp_init(&iso, can_send_fn, tx_id, rx_id, iso_tx_sdu, sizeof(iso_tx_sdu));
    ```
2.  Wire `config.fn_tp_send` to a small adapter that forwards to `uds_isotp_send()`:
    ```c
    static int isotp_send_adapter(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
    {
        (void) ctx;
        return uds_isotp_send(&iso, data, len);
    }
    ```
3.  Feed raw CAN frames into `uds_isotp_rx_callback(&iso, &uds, id, data, len)`.
4.  Drive timing/transmission via `uds_tp_isotp_process(&iso, time_ms)`.

## 3. Internal ISO-TP States

The fallback implementation handles standard ISO-TP flows:
- **SF (Single Frame)**: Immediate dispatch.
- **FF (First Frame)**: Allocates buffer, sends **FC (Flow Control)**, waits for data.
- **CF (Consecutive Frame)**: Reassembles payload.
- **TX Flow Control**: When sending large SDUs, the stack sends FF and waits for the peer's FC before streaming CFs.

## 4. Duplex mode (half / full)

Each ISO-TP channel operates in one of two modes, selected with
`uds_tp_isotp_set_mode(&iso, mode)`:

- `ISOTP_HALF_DUPLEX` (default): one transfer per N_AI at a time. An inbound
  Single/First Frame terminates an in-flight transmission, and starting a
  transmission abandons an in-flight reception — matching ISO 15765-2 Table 23
  for half-duplex nodes. This is the default to preserve compatibility.
- `ISOTP_FULL_DUPLEX`: a segmented reception and a segmented transmission run
  simultaneously and independently on the same N_AI. Inbound frames never abort
  an outgoing response, and vice versa. Use this when the node must accept new
  requests while still streaming a long response (e.g. gateways, or a server
  that must not drop its response when the tester sends TesterPresent).
  **Scope note**: full-duplex permits one concurrent RX transfer and one TX
  transfer on a channel — it does not support two simultaneous transmissions on
  one N_AI (a single TX connection per N_AI, per ISO 15765-2). A Single-Frame
  response (e.g. TesterPresent) emitted while a multi-frame response is in
  flight is fine, because it does not open a second TX connection.

The mode is per channel (mirrors AUTOSAR `CanTpChannelMode`). RX and TX use
independent state, sequence numbers, block-size counters, and N_Cr/N_Bs timers;
the receiver-advertised BS/STmin (sent in our FlowControl) are kept separate
from the sender-honored BS/STmin (received in the peer's FlowControl).

## 5. Physical vs. functional addressing

Each ISO-TP channel recognises a physical (point-to-point) RX ID and an optional
functional (broadcast, one-to-many) RX ID, set with
`uds_tp_isotp_set_functional_id(&iso, id)` (pass 0 to disable; disabled by
default). A frame on the physical `rx_id` is delivered as `UDS_ADDR_PHYSICAL`; a
frame on the functional ID as `UDS_ADDR_FUNCTIONAL`.

Functional addressing is **Single-Frame only** (ISO 15765-2 has no flow control
for one-to-many): a functionally addressed FF/CF/FC is ignored. Responses are
always sent on the physical `tx_id`.

A service declares which addressing it accepts via `address_mode` in its
`uds_service_entry_t` (a `UDS_ADDR_*` bitmask). `0` (the default, and every
built-in core service) means **both**. A functionally addressed request to a
service that does not accept it is silently dropped.

Per ISO 14229-1, a functionally addressed request never elicits the negative
response codes `0x11`, `0x12`, `0x7E`, `0x7F`, or `0x31` (these are suppressed to
avoid flooding a shared bus); all other negative responses and all positive
responses are still sent.

## 7. Hardening & Flow Control

UDSLib implements standard ISO-TP hardening features to ensure robust communication:
- **STmin (Separation Time)**: Enforces minimum time between consecutive frames (CF) to prevent overwhelming the receiver.
- **Block Size (BS)**: Manages data flow by requiring Flow Control (FC) frames after a specified number of CFs.
- **Dynamic Timing**: STmin and Block Size parameters are dynamically extracted from peer Flow Control frames during transmission.
- **Transfer Timeouts**: A stalled multi-frame transfer is aborted on timeout. `N_Cr` bounds the wait for the next Consecutive Frame during reception, and `N_Bs` bounds the wait for a Flow Control frame after sending a First Frame. Both default to 1000 ms (`ISOTP_N_CR_DEFAULT_MS` / `ISOTP_N_BS_DEFAULT_MS`) and are overridable per instance via `iso.n_cr_ms` / `iso.n_bs_ms`.

## 8. CAN-FD Support

The internal ISO-TP layer supports both Classic CAN and CAN-FD, enabling frames up to 64 bytes for higher throughput.
- **Enable**: Call `uds_tp_isotp_set_fd(&iso, true)` after initialization (Classic CAN is the default).
- **Single Frame (SF)**: Automatically uses CAN-FD SF format (`0x00 | DL`) for payloads > 7 bytes.
- **Multi-Frame**: First Frame (FF) and Consecutive Frames (CF) utilize full 64-byte capacity (up to 62/63 bytes payload per frame).
- **Compliance**: Adheres to ISO 15765-2 Table 9 for N_PCI bytes.

## 9. Virtual CAN (Host Simulation)

For PC-based verification, we encapsulate CAN frames in UDP packets. This allows full stack execution without physical hardware.
