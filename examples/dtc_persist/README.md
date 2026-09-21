# DTC persistence example

Shows how DTC status survives a reset **without udslib writing flash**.

The reference store (`uds/uds_dtc_store.h`) keeps status, the fault-detection
counter, and the aging counter in an application RAM array. On a power cycle
that RAM is gone, so `0x19` reads an empty table.

The library copies those runtime bytes to a buffer you own:

```c
/* After a test result, an operation cycle, or a clear. */
int n = uds_dtc_store_serialize(&store, nvm, sizeof(nvm));
/* nvm_write() is your flash / EEPROM driver. The library does not call it. */

/* At boot, after the same DTC numbers are registered again. */
uds_dtc_store_deserialize(&store, nvm, nvm_len);
```

Severity, functional unit, and functional group are configuration. Register
them again at boot. They are not in the blob.

This demo uses a `uint8_t` array as the stand-in for flash. A real ECU replaces
`nvm_save()` / `nvm_load()` in `main.c` with its own driver (for example an
STM32 wear-leveled page store). Do not erase flash inside the `0x19` handler —
erase is slow, and the tester will time out. Save from your own task, after
the response has been sent.

## Build & run

```sh
make run
```

Expected output:

```
Saved 21 bytes of DTC state into the application NVM buffer.
After reset, before NVM load:
  19 02 FF -> 59 02 7F
  DTC is gone. 0x19 reads RAM, and RAM was cleared.
After NVM load:
  19 02 FF -> 59 02 7F 01 23 45 23
  DTC 012345 status 0x23 is back. Flash stayed in the application.
```

`59 02 7F` is a positive `0x19 0x02` response with an empty DTC list.
`01 23 45 23` is DTC `0x012345` with status
`testFailed | testFailedThisOperationCycle | testFailedSinceLastClear`.
