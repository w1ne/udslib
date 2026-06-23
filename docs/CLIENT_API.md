# UDS Client API Guide

UDSLib provides a UDS client (tester) role alongside the server stack. The
client is a small, separate context (`uds_client_ctx_t`, declared in
`include/uds/uds_client.h`) that reuses a `uds_config_t` for its transport
binding only (`tx_buffer`, `fn_tp_send`, and the mutex); the server hook fields
are unused. It is independent of the server `uds_ctx_t`.

## 1. Request and Response Pattern

The client API is non-blocking: it sends one outstanding request and fires a
callback when the matching response arrives.

### `uds_client_request`

```c
int uds_client_request(uds_client_ctx_t* c,
                       uint8_t sid,
                       const uint8_t* data,
                       uint16_t len,
                       uds_response_cb cb);
```

- **`c`**: A pointer to a `uds_client_ctx_t` whose `config` is set to a
  transport-bound `uds_config_t`.
- **`sid`**: The Service Identifier (e.g., `0x22` for ReadDataByIdentifier).
- **`data`**: The payload after the SID (sub-functions, identifiers, ...).
- **`len`**: The payload length.
- **`cb`**: The callback armed for the response.

It builds `{sid, data...}` in `config->tx_buffer` and sends it via
`config->fn_tp_send` (called with `ctx == NULL`), then arms `cb`. Returns the
transport result or a negative `UDS_ERR_*`.

### Thread Safety

`uds_client_request` builds its frame under the configured mutex, so a client
and a server may safely share one `tx_buffer` when both are given the same
mutex callbacks. See [docs/OSAL.md](OSAL.md) for the full concurrency model.

> [!WARNING]
> Do not call `uds_client_request` from inside a callback unless your mutex
> implementation supports recursion.

## 2. Response Callback

```c
typedef void (*uds_response_cb)(struct uds_client_ctx* c,
                                uint8_t sid,
                                const uint8_t* data,
                                uint16_t len);
```

- **`sid`**: The response SID.
    - Positive response: `Original SID + 0x40` (e.g., `0x62`).
    - Negative response: `0x7F`. `data` then contains `[Original SID, NRC]`.
- **`data`**: The response payload **after** the SID byte.
- **`len`**: The payload length.

Feed incoming frames to `uds_client_handle_response()`. When a frame is the
response to the outstanding request (a positive `sid == pending | 0x40`, or a
`0x7F` negative response echoing the pending SID), it fires `cb`, clears the
pending state, and returns `true`; otherwise it returns `false` and the caller
routes the frame elsewhere.

## 3. Example

The ISO-TP transport is instance-based. The application wires
`config.fn_tp_send` to an adapter that forwards to its ISO-TP instance, and
routes reassembled SDUs to the client via an SDU handler (see
`uds_isotp_set_sdu_handler`) so the transport stays role-agnostic.

```c
// ISO-TP transport instance and its multi-frame TX cache.
static uds_isotp_ctx_t iso;
static uint8_t iso_tx_sdu[1024];
static uint8_t tx_buf[64];

// Transport-only config for the client.
static uds_config_t client_cfg = {
    .tx_buffer = tx_buf,
    .tx_buffer_size = sizeof(tx_buf),
    .get_time_ms = get_time_ms,
    .fn_tp_send = isotp_send_adapter,   // forwards to uds_isotp_send(&iso, ...)
};

static uds_client_ctx_t client = { .config = &client_cfg };

// Adapter binding the core fn_tp_send contract to this ISO-TP instance.
static int isotp_send_adapter(struct uds_ctx* ctx, const uint8_t* data, uint16_t len) {
    (void) ctx;
    return uds_isotp_send(&iso, data, len);
}

// SDU sink: route reassembled responses to the client.
static void on_sdu(void* cookie, const uint8_t* sdu, uint16_t len, uint8_t addr) {
    (void) addr;
    uds_client_ctx_t* c = (uds_client_ctx_t*) cookie;
    if (len > 0) {
        uds_client_handle_response(c, sdu[0], &sdu[1], (uint16_t)(len - 1));
    }
}

static void on_vin_received(struct uds_client_ctx* c, uint8_t sid,
                            const uint8_t* data, uint16_t len) {
    if (sid == 0x62) {            // Positive ReadDataByIdentifier
        // process VIN in data[0..len-1] ...
    } else if (sid == 0x7F && data[0] == 0x22) {
        // handle NRC for service 0x22
    }
}

// Wire the transport, route SDUs to the client, then send.
uds_tp_isotp_init(&iso, can_send, 0x7E0, 0x7E8, iso_tx_sdu, sizeof(iso_tx_sdu));
uds_isotp_set_sdu_handler(&iso, on_sdu, &client);

uds_client_request(&client, 0x22, (uint8_t[]){0xF1, 0x90}, 2, on_vin_received);

// Main loop drives instance-based transport processing.
while (1) {
    uds_tp_isotp_process(&iso, get_time_ms());
    // Feed received CAN frames into the ISO-TP engine:
    // uds_isotp_rx_callback(&iso, /* core or NULL-backed ctx */, id, frame_data, frame_len);
}
```

## 4. Multi-Frame Support

The transport layer handles segmented responses (multi-frame) automatically.
The callback runs only after the stack reassembles the full SDU.
