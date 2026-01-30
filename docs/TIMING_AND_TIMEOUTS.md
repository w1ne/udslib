# UDS Timing and Timeout Management

LibUDS implements the strictly-defined timing requirements of **ISO 14229-1 (UDS)** and **ISO 15765-2 (ISO-TP)**.

## 1. P2 and P2* Timers (Server)

These timers manage the response deadlines for diagnostic services.

### P2 Server Timeout
- **Definition**: The time between the receipt of a request and the start of the response.
- **LibUDS Implementation**: Configured via `uds_config_t.p2_ms`.
- **Behavior**: If a service doesn't respond within `p2_ms`, the stack automatically sends **NRC 0x78 (Response Pending)**.

### P2* Server Timeout
- **Definition**: The extended timeout used after an NRC 0x78 has been sent.
- **LibUDS Implementation**: Configured via `uds_config_t.p2_star_ms`.
- **Behavior**: The stack will continue to send NRC 0x78 every `p2_star_ms` until a final response is sent.

---

## 2. Implementing Asynchronous Services

If a service requires a long-running operation (e.g., flash erase, long routine), you can defer the response.

### Pattern: `UDS_PENDING`
In your service dispatcher (or handler), set the pending flag:

```c
void my_service_handler(uds_ctx_t* ctx, ...) {
    // Start hardware operation...
    ctx->p2_msg_pending = true;
    // Return without calling uds_send_response()
}
```

### Finishing the Operation
Once the operation is complete, call `uds_send_response()` or `uds_send_nrc()` from your main loop or a callback:

```c
if (job_done) {
    ctx->config->tx_buffer[0] = 0x50 | my_sid;
    // ... add data ...
    uds_send_response(ctx, len);
}
```

LibUDS will handle the NRC 0x78 generation automatically in the background via `uds_process()`.

---

## 3. S3 Client/Server Timeout

### S3 Server Timeout
- **Purpose**: Reverts the ECU to the **Default Session** if no tester activity is detected.
- **LibUDS Implementation**: Fixed at 5000ms (standard).
- **Behavior**: Resets `active_session` and `security_level`.

---

## 4. Summary Table

| Parameter | Default Value | Description |
| :--- | :--- | :--- |
| `p2_ms` | 50ms | Time to first response / NRC 0x78 |
| `p2_star_ms` | 5000ms | Interval between subsequent NRC 0x78 |
| `s3_server` | 5000ms | Non-activity session timeout |
