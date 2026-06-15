# UDS Client API Guide

UDSLib functions symmetrically: the same core stack acts as both a Server (ECU) and a Client (Tester).

## 1. Request and Response Pattern

The client API is non-blocking and uses callbacks to handle responses.

### `uds_client_request`

```c
int uds_client_request(uds_ctx_t* ctx, 
                       uint8_t sid, 
                       const uint8_t* data, 
                       uint16_t len, 
                       uds_response_cb callback);
```

- **`ctx`**: A pointer to the initialized UDS context.
- **`sid`**: The Service Identifier (e.g., `0x10` for Session Control).
- **`data`**: A pointer to the payload specifics (like sub-functions or identifiers).
- **`len`**: The payload length.
- **`callback`**: The function that runs when the response arrives.

### Thread Safety

`uds_client_request` locks the context mutex.

> [!WARNING]
> Do not call `uds_client_request` from inside a `callback` unless your mutex implementation supports recursion. The callback runs while the stack holds the lock.

## 2. Response Callback

The handler runs when the server sends a complete SDU (response or NRC).

```c
typedef void (*uds_response_cb)(uds_ctx_t* ctx, 
                                uint8_t sid, 
                                const uint8_t* data, 
                                uint16_t len);
```

- **`sid`**: The response SID.
    - Positive response: `Original SID + 0x40` (e.g., `0x50`).
    - Negative response: `0x7F`. Data contains `[Original SID, NRC]`.
- **`data`**: The response payload.
- **`len`**: The payload length.

## 3. Example

The ISO-TP transport is instance-based. The application wires `config.fn_tp_send`
to a small adapter that forwards to its ISO-TP instance, and feeds received CAN
frames to `uds_isotp_rx_callback`.

```c
// ISO-TP transport instance and its multi-frame TX cache.
static uds_isotp_ctx_t iso;
static uint8_t iso_tx_sdu[1024];

// Adapter binding the core's fn_tp_send contract to this ISO-TP instance.
static int isotp_send_adapter(uds_ctx_t* ctx, const uint8_t* data, uint16_t len) {
    (void) ctx;
    return uds_isotp_send(&iso, data, len);
}

static void on_vin_received(uds_ctx_t* ctx, uint8_t sid, const uint8_t* data, uint16_t len) {
    if (sid == 0x62) { // Positive ReadDataByIdentifier
        // process VIN ...
    } else if (sid == 0x7F && data[0] == 0x22) {
        // handle NRC for service 0x22
    }
}

// Initialize the transport (TX 0x7E0, RX 0x7E8) and wire fn_tp_send = isotp_send_adapter.
uds_tp_isotp_init(&iso, can_send, 0x7E0, 0x7E8, iso_tx_sdu, sizeof(iso_tx_sdu));

// Sending the request
uds_client_request(&ctx, 0x22, (uint8_t[]){0xF1, 0x90}, 2, on_vin_received);

// Ensure the main loop calls uds_process and instance-based transport processing
while(1) {
    uds_process(&ctx);
    uds_tp_isotp_process(&iso, get_time_ms());

    // Feed received CAN frames into the ISO-TP engine.
    // uds_isotp_rx_callback(&iso, &ctx, id, frame_data, frame_len);
    // ...
}
```

## 4. Multi-Frame Support

The transport layer handles segmented responses (multi-frame) automatically. The callback runs only after the stack reconstructs the full message.
