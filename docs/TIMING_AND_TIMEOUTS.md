# UDS Timing and Timeout Management

UDSLib implements the timing requirements of **ISO 14229-1** (UDS) and **ISO 15765-2** (ISO-TP).

## 1. Server Response Deadlines (P2 / P2*)

### P2 Server Timeout
- **Definition**: Time limit for the first response after a request.
- **Config**: `uds_config_t.p2_ms` (Default: 50ms).
- **Behavior**: If the service logic takes longer than `p2_ms`, the stack automatically sends **NRC 0x78 (Response Pending)**.

### P2* Server Timeout
- **Definition**: The extended timeout used after sending NRC 0x78.
- **Config**: `uds_config_t.p2_star_ms` (Default: 5000ms).
- **Behavior**: The stack repeats NRC 0x78 every `p2_star_ms` until the service provides a final response.

### Runtime Negotiation (AccessTimingParameter, SID 0x83)
The live P2 / P2* values can be read and changed at runtime via **AccessTimingParameter (0x83)** — read current, set, or restore defaults. This operates directly on the active timing parameters (no application callback); the configured `p2_ms` / `p2_star_ms` remain the power-on defaults.

## 2. Asynchronous Services

For long-running operations (like flash erasing), you can defer the response.

### Pattern: `uds_pending()`
A service handler returns its outcome through the `uds_result_t *out`
descriptor; the framework is the single emission authority. To defer, mark the
result pending and return — do **not** emit from the handler:

```c
void my_service_handler(uds_ctx_t* ctx, const uint8_t* data, uint16_t len,
                        uds_result_t* out) {
    (void) data; (void) len;
    // Start the hardware operation, then:
    uds_pending(out);   // framework sends NRC 0x78 and tracks P2*
}
```

UDSLib handles the NRC 0x78 generation in the background via `uds_process()`,
repeating it every `p2_star_ms` until the operation completes.

### Finishing the Operation
The deferred result is emitted from the application's own context (not the
handler) when the task completes, via `uds_send_response()` or
`uds_send_nrc()`:

```c
if (job_done) {
    ctx->config->tx_buffer[0] = 0x50 | my_sid;
    // ... add data ...
    uds_send_response(ctx, len);
}
```

## 3. S3 Inactivity Timeout

- **Purpose**: Reverts the ECU to the **Default Session** if the tester stays silent.
- **Config**: `uds_config_t.s3_ms` (default 5000ms; resolved once in `uds_init()`
  into `ctx->session.s3_ms`). Set `s3_ms` to override the ISO default.
- **Behavior**: From `uds_process()`, reverts `ctx->session.active` to the default
  session and relocks `ctx->security.level` / clears any outstanding seed.

## 4. ISO-TP Transport Timeouts (N_Cr / N_Bs)

The ISO 15765-2 transport is **instance-based**: each `uds_isotp_ctx_t` is owned by
the application and carries its own timers, advanced from `uds_tp_isotp_process()`.
Two timeouts abort a stalled multi-frame transfer instead of wedging the engine:

- **N_Cr** (`n_cr_ms`, default 1000ms): max wait for the next **Consecutive Frame**
  during reception. If a CF never arrives, the in-progress reception is aborted.
- **N_Bs** (`n_bs_ms`, default 1000ms): max wait for the **Flow Control** frame after
  sending a First Frame during transmission. If FC never arrives, the transmission
  is aborted.

Both are per-instance fields on `uds_isotp_ctx_t` and may be overridden after
`uds_tp_isotp_init()`.

## 5. Summary

| Parameter | Default | Description |
| :--- | :--- | :--- |
| `p2_ms` | 50ms | Time to first response / NRC 0x78 (runtime-adjustable via SID 0x83) |
| `p2_star_ms` | 5000ms | Interval between subsequent NRC 0x78 (runtime-adjustable via SID 0x83) |
| `s3_ms` | 5000ms | Session timeout due to inactivity (configurable; default `UDS_S3_TIMEOUT_MS`) |
| `STmin` | 0ms | Separation time between ISO-TP CFs |
| `Block Size`| 8 | ISO-TP flow control block size |
| `n_cr_ms` | 1000ms | ISO-TP: max wait for a Consecutive Frame (RX) |
| `n_bs_ms` | 1000ms | ISO-TP: max wait for Flow Control after a First Frame (TX) |
