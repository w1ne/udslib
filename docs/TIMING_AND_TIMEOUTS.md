# UDS Timing and Timeout Management

LibUDS implements the timing requirements of **ISO 14229-1** (UDS) and **ISO 15765-2** (ISO-TP).

## 1. Server Response Deadlines (P2 / P2*)

### P2 Server Timeout
- **Definition**: Time limit for the first response after a request.
- **Config**: `uds_config_t.p2_ms` (Default: 50ms).
- **Behavior**: If the service logic takes longer than `p2_ms`, the stack automatically sends **NRC 0x78 (Response Pending)**.

### P2* Server Timeout
- **Definition**: The extended timeout used after sending NRC 0x78.
- **Config**: `uds_config_t.p2_star_ms` (Default: 5000ms).
- **Behavior**: The stack repeats NRC 0x78 every `p2_star_ms` until the service provides a final response.

## 2. Asynchronous Services

For long-running operations (like flash erasing), you can defer the response.

### Pattern: `UDS_PENDING`
In your handler, set the pending flag and return early:

```c
void my_service_handler(uds_ctx_t* ctx, ...) {
    // Start hardware operation
    ctx->p2_msg_pending = true;
    // Return without calling uds_send_response()
}
```

LibUDS handles the NRC 0x78 generation in the background via `uds_process()`.

### Finishing the Operation
When the task completes, call `uds_send_response()` or `uds_send_nrc()`:

```c
if (job_done) {
    ctx->config->tx_buffer[0] = 0x50 | my_sid;
    // ... add data ...
    uds_send_response(ctx, len);
}
```

## 3. S3 Inactivity Timeout

- **Purpose**: Reverts the ECU to the **Default Session** if the tester stays silent.
- **Duration**: Fixed at 5000ms.
- **Behavior**: Resets `active_session` and `security_level`.

## 4. Summary

| Parameter | Default | Description |
| :--- | :--- | :--- |
| `p2_ms` | 50ms | Time to first response / NRC 0x78 |
| `p2_star_ms` | 5000ms | Interval between subsequent NRC 0x78 |
| `s3_server` | 5000ms | Session timeout due to inactivity |
| `STmin` | 0ms | Separation time between ISO-TP CFs |
| `Block Size`| 8 | ISO-TP flow control block size |
