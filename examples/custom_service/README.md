# Custom service example

Shows how to add a **manufacturer-specific UDS service** (or override a built-in
one) **without editing the library** — purely through `config.user_services`.

The dispatcher checks user services *before* core services
(`find_service()` in `src/core/uds_core.c`), so a row in your `user_services`
table either adds a new SID or shadows a core one.

## The whole integration

```c
/* 1. Write a handler with the standard service signature. */
static int handle_vendor_diag(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (len < 2u) {
        return uds_send_nrc(ctx, 0xBAu, 0x13u);
    }
    uint8_t *tx = ctx->config->tx_buffer;
    tx[0] = (uint8_t)(0xBAu + 0x40u);   /* positive response SID */
    tx[1] = data[1];                    /* echo sub-function     */
    tx[2] = 'V'; tx[3] = 'N'; tx[4] = '1';
    return uds_send_response(ctx, 5u);
}

/* 2. Register it. Columns mirror uds_service_entry_t:
 *    { SID, min_len, session_mask, security_mask, handler, sub_mask } */
static const uds_service_entry_t user_services[] = {
    { 0xBAu, 2u, UDS_SESSION_ALL, 0u, handle_vendor_diag, NULL },
};

/* 3. Point the config at the table. */
cfg.user_services      = user_services;
cfg.user_service_count = 1u;
```

To **override** a core service instead, use its SID (e.g. `0x11`) in the table;
your handler wins because user services are matched first.

## Build & run

```sh
make run
```

Expected output:

```
=== Custom vendor service 0xBA (registered via cfg.user_services) ===
-> request: BA 01
  <- response: FA 01 56 4E 31
...
=== Unknown SID 0xC0 rejected with NRC 0x11 ===
-> request: C0 00
  <- response: 7F C0 11
```
