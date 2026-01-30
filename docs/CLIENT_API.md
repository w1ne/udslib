# UDS Client API Guide

LibUDS supports a symmetric design where the same core stack can act as both a Server (ECU) and a Client (Tester).

## 1. Request/Response Pattern

The client API is non-blocking and relies on a callback mechanism.

### `uds_client_request`

```c
int uds_client_request(uds_ctx_t* ctx, 
                       uint8_t sid, 
                       const uint8_t* data, 
                       uint16_t len, 
                       uds_response_cb callback);
```

- **`ctx`**: Pointer to the initialized UDS context.
- **`sid`**: Service Identifier (e.g., `0x10` for Session Control).
- **`data`**: Pointer to the service-specific payload (e.g., sub-function, data identifiers).
- **`len`**: Length of the payload.
- **`callback`**: Function pointer to the handler which will be called when the response arrives.

### Thread Safety

`uds_client_request` is thread-safe and will lock the context mutex.
**Warning**: Do not call `uds_client_request` from within a `callback` unless your mutex implementation is recursive, as the callback is invoked while the mutex is held.

## 2. Response Callback

The callback is invoked when a complete SDU (response or NRC) is received from the server.

```c
typedef void (*uds_response_cb)(uds_ctx_t* ctx, 
                                uint8_t sid, 
                                const uint8_t* data, 
                                uint16_t len);
```

- **`sid`**: The SID of the response.
    - Positive response: `Original SID + 0x40` (e.g., `0x50`).
    - Negative response: `0x7F`. Data will contain `[Original SID, NRC]`.
- **`data`**: The response payload.
- **`len`**: Length of the response payload.

## 3. Example Usage

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

// Ensure your main loop calls uds_process and transport-layer processing
while(1) {
    uds_process(&ctx);
    uds_tp_isotp_process(); // If using fallback
    // ...
}
```

## 4. Multi-Frame Support

The client API automatically handles segmented responses (multi-frame) via the underlying transport layer. The callback is only triggered once the full message has been reconstructed.
