# UDS Client API Guide

LibUDS functions symmetrically: the same core stack acts as both a Server (ECU) and a Client (Tester).

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

```c
static void on_vin_received(uds_ctx_t* ctx, uint8_t sid, const uint8_t* data, uint16_t len) {
    if (sid == 0x62) { // Positive ReadDataByIdentifier
        // process VIN ...
    } else if (sid == 0x7F && data[0] == 0x22) {
        // handle NRC for service 0x22
    }
}

// Sending the request
uds_client_request(&ctx, 0x22, (uint8_t[]){0xF1, 0x90}, 2, on_vin_received);

// Ensure the main loop calls uds_process and transport-layer processing
while(1) {
    uds_process(&ctx);
    uds_tp_isotp_process(); // If using fallback
    // ...
}
```

## 4. Multi-Frame Support

The transport layer handles segmented responses (multi-frame) automatically. The callback runs only after the stack reconstructs the full message.
