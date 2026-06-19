# Examples

Worked examples for integrating LibUDS. The host examples below build with a
plain `gcc` and run on your machine — `cd` into one and run `make run`.

## Runnable host demos (`make run`)

`cd` into one and run `make run`. Each prints its response bytes to stdout and
exits non-zero on an unexpected (non-positive) response, so it doubles as a
smoke test.

| Example | Shows |
|---------|-------|
| [`custom_service`](custom_service/) | Add a manufacturer-specific service (or override a built-in) via `config.user_services`, without editing the library. |
| [`auth_challenge`](auth_challenge/) | Wire the Authentication service (0x29) challenge/response flow (no crypto dependency). |
| [`auth_challenge_mbedtls`](auth_challenge_mbedtls/) | The same 0x29 flow with **real AES-128-CMAC** via mbedTLS behind `fn_auth` (needs `libmbedtls-dev`). |
| [`dtc_store`](dtc_store/) | Manage DTC instances with the opt-in reference store and answer ReadDTCInformation (0x19) end-to-end. |
| [`dtc_full_coverage`](dtc_full_coverage/) | Every 0x19 sub-function — library-framed and application-served — plus 0x04/0x06 freeze-frame payloads. |

## Other host examples (`make`, then run the binary)

| Example | Shows |
|---------|-------|
| [`host_sim`](host_sim/) | A full UDS server simulator exercising many services; used by the Python integration tests. |
| [`client_demo`](client_demo/) | Drive a server from the UDS client API. |

## Integration templates

| Example | Target |
|---------|--------|
| [`bare_metal`](bare_metal/) | Bare-metal super-loop integration skeleton. |
| [`freertos_demo`](freertos_demo/) | Task-based FreeRTOS integration skeleton. |
| [`zephyr_uds_server`](zephyr_uds_server/) | Zephyr application (build with `west`); see [`../docs/QUICKSTART_ZEPHYR.md`](../docs/QUICKSTART_ZEPHYR.md). |
| [`pro_flash_tool`](pro_flash_tool/) | End-to-end ECU reprogramming client (built via the top-level CMake). |

## Generated artifacts

| Path | Contents |
|------|----------|
| [`generated/`](generated/) | Example DID table auto-generated from ODX. |
| [`generated_tests/`](generated_tests/) | Generated Python service tests. |

See [`../docs/INTEGRATION_GUIDE.md`](../docs/INTEGRATION_GUIDE.md) for a full
integration walkthrough.
