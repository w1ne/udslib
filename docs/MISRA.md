# MISRA-C:2012 Compliance

LibUDS targets MISRA-C:2012 for its core (`src/`, `include/`). Compliance is
**checked in CI** on every push via `scripts/check_misra.sh`, which runs
cppcheck's MISRA addon against the documented deviation baseline below.

- **No mandatory-rule violations.** The baseline contains only Required and
  Advisory rules, so any mandatory-rule violation introduced later fails CI.
- **Regression gate.** Any rule not in the baseline fails the build — it must
  be fixed, or added here with rationale and to the `ACCEPTED` list in
  `scripts/check_misra.sh`.

## Reproduce locally

```bash
./scripts/docker_run.sh ./scripts/check_misra.sh
# or directly:
cppcheck --addon=misra --enable=all --inline-suppr -I include -I src/core src/
```

## Documented deviations

| Rule | Cat. | Rationale |
| :--- | :--- | :--- |
| 15.5 | Advisory | Single point of exit. Handlers return early on each negative-response path; this is clearer than nesting and is the dominant deviation. |
| 12.1 | Advisory | Explicit operator-precedence parentheses; the existing expressions are reviewed and unambiguous. |
| 12.3 | Advisory | Comma operator (initializer lists / loop forms). |
| 2.3 / 2.4 / 2.5 | Advisory | Unused type/tag/macro: the public API intentionally exposes enums, NRC/SID macros, and types for integrators that are not all referenced inside the library's own translation units. |
| 8.7 / 8.9 | Advisory | External-linkage symbols and object scope kept for the public API and the per-instance design. |
| 10.1 / 10.4 / 10.8 | Required | Essential-type arithmetic and casts on protocol fields (byte/bit packing of CAN/ISO-TP/UDS data) use explicit casts; each is reviewed for correctness. |
| 11.1 | Required | The service dispatch table stores handlers behind a single generic signature. |
| 11.9 | Required | `NULL` usage for optional callbacks/pointers. |
| 14.4 | Required | Idiomatic non-boolean controlling expressions (`if (ptr)`, `if (len)`). |
| 15.6 / 15.7 | Required | A few terse compound-statement / `else if` forms, reviewed. |
| 17.7 | Required | The transport return value from `uds_send_nrc()` is intentionally not used on some negative-response paths. |

These deviations are stylistic or reflect the protocol-parsing and
public-API nature of the code; none affect determinism, memory safety, or the
zero-malloc guarantee.
