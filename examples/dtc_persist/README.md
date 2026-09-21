# DTC persistence example

Shows the ECU side of keeping DTCs across a reset. udslib does not write flash.

What this program does, in order:

1. The **monitor** calls `uds_dtc_store_report_test()` until the fault-detection
   counter hits +127. That confirms the DTC and freezes the environment from
   `uds_dtc_store_set_environment()`. The UDS stack never calls the monitor.
2. The **ignition-cycle task** calls `uds_dtc_store_operation_cycle()` twice:
   once to close the failed cycle, once for a later cycle with no failure.
   Ageing moves only on that clean cycle. The stack never calls this either.
3. The application copies the store into a buffer with
   `uds_dtc_store_serialize()` and keeps that buffer. Here the buffer is a
   `uint8_t` array. A real ECU calls its own flash or EEPROM driver instead.
   Do not erase flash inside the `0x19` handler — erase is slow, and the
   tester will time out.
4. RAM is wiped. The DTC catalog is registered again (severity and functional
   unit are configuration, not part of the blob). `0x19` is empty.
5. `uds_dtc_store_deserialize()` puts the runtime bytes back. `0x02`, `0x04`,
   and `0x06` show the same status, freeze frame, and counters.

```c
int n = uds_dtc_store_serialize(&store, nvm, sizeof(nvm));
/* nvm_write() is your driver. The library does not call it. */

uds_dtc_store_register(&store, 0x012345u, /* ...catalog again at boot... */);
uds_dtc_store_deserialize(&store, nvm, nvm_len);
```

## Build & run

```sh
make run
```

Expected output:

```
The UDS stack does not detect faults. The ECU monitor calls report_test.
127 failing reports confirmed DTC 012345 and stored the freeze frame.
After two operation cycles, the second with no failure:
  occurrence=1  pending=0  ageing=1  snapshot voltage=0x8C
Saved 21 bytes into the application NVM buffer. The library did not write flash.

After reset, before NVM load:
  19 02 FF -> 59 02 7F
  19 04 01 23 45 01 -> 59 04 01 23 45 00
  19 06 01 23 45 01 -> 59 06 01 23 45 00 01 00 00 00 00
  RAM was cleared. 0x19 reads RAM.

After NVM load:
  19 02 FF -> 59 02 7F 01 23 45 2D
  19 04 01 23 45 01 -> 59 04 01 23 45 2D 01 02 10 01 19 06 13 0D 05 2A 10 02 8C 02
  19 06 01 23 45 01 -> 59 06 01 23 45 2D 01 01 00 00 01
  Status, freeze frame, and counters are back.
```

`59 02 7F` is a positive `0x19 0x02` with an empty list. After the load,
status `0x2D` is the confirmed DTC with the "this operation cycle" bit cleared.
`0x04` is DID `0x1001` (time, year first) and DID `0x1002` (voltage, power mode).
`0x06` is occurrence `1`, pending `0`, aged `0`, ageing `1`.

## See also

`../dtc_store` — list, snapshot, and extended data without the reset.
`../dtc_full_coverage` — write your own `0x04` / `0x06` bytes when this
reference layout is not the one your OEM specification uses.
