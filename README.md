# UDSLib: ISO 14229 Stack for Production

Portable, MISRA-aligned UDS (ISO 14229-1) stack with deterministic memory, Zephyr/native ISO-TP support, and built-in tooling.

## Licensing
- **Community**: PolyForm Noncommercial 1.0.0 (noncommercial only). See `LICENSE`.
- **Commercial**: 5,000 EUR, includes integration support (up to 40h) and 1 year of updates/support. See `COMMERCIAL_LICENSE.md` or email `andrii@shylenko.com`.
- **Evaluation**: Development/testing allowed under the community license. No production/for‑profit use without the commercial license.

## Key Capabilities
- **Services**: ISO 14229-1 set implemented in this repo (0x10/11/14/19/22/23/27/28/29/2E/31/34/36/37/3D/3E/85).
- **Safety & Quality**: Deterministic, zero-malloc memory model; NRC priority enforcement; Safety Gate callbacks; MISRA-aligned codebase.
- **Transports**: Zephyr ISO-TP sockets or built-in ISO-TP fallback (static buffers) with CAN-FD support.
- **Tooling**: Host simulator, Wireshark dissector, Python `pyudslib` harness, Dockerized CI scripts.

## Quick Start (Linux)
```bash
sudo apt-get install build-essential cmake libcmocka-dev
mkdir build && cd build
cmake ..
make
ctest --output-on-failure   # optional: run tests
```

Run host simulator:
```bash
./examples/host_sim/uds_host_sim
```

## Integrate
- Provide time source, TX/RX buffers, and transport send function via `uds_config_t`.
- Call `uds_input_sdu()` with complete SDUs; call `uds_process()` periodically to drive timers.
- For Zephyr, use the native ISO-TP wrapper; for bare metal, use the internal ISO-TP fallback.

## Session Analysis & Reporting
Generate a visual HTML dashboard to analyze UDS sessions (CAN-FD/ISO-TP):

```bash
# 1. Run simulation with capture (CAN-FD mode, Default)
./run_capture.sh

# Option: Run in Classic CAN mode (8-byte frames)
./run_capture.sh classic

# 2. View the generated report
# Open 'session_report_fd.html' (or 'session_report_classic.html')
```

The analyzer includes:
- **Visual Interface**: Dark-mode HTML dashboard.
- **Rich Details**: Decodes timestamps, specific services, and raw payloads.
- **Flow Analysis**: Visualizes TX/RX direction and ISO-TP frame types (SF/FF/CF/FC).


## Repository Structure
- `include/uds/` public API headers
- `src/core/` protocol logic
- `src/transport/` ISO-TP fallback
- `examples/` host simulator and integration templates
- `extras/` Wireshark dissector, Python bindings
- `docs/` white paper, guides, and strategy

## Support & Licensing
Commercial inquiries: `andrii@shylenko.com`  
Community questions: open an issue on GitHub.

## 7. Documentation

For deeper dives into the project's design and future, please refer to the following documents:

*   [**Architecture**](docs/ARCHITECTURE.md) - Design philosophy, component diagrams, and transport strategy.
*   [**Roadmap**](docs/ROADMAP.md) - Project phases, upcoming features, and long-term vision.
*   [**Vision**](docs/VISION.md) - Product mission, market position, and design principles.
