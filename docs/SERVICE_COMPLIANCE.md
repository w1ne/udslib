# ISO 14229-1 (UDS) Service Compliance

This document tracks the compliance and support level for UDS services.

## Compliance Matrix

| SID | Service Name | Status | Notes |
| :--- | :--- | :--- | :--- |
| **0x10** | **Diagnostic Session Control** | ✅ Supported | Default, Extended, Programming. |
| **0x11** | **ECU Reset** | ✅ Supported | Hard, Soft, KeyOffOn. |
| **0x14** | **Clear Diagnostic Information** | ✅ Supported | Supports optional memory selection byte. |
| **0x19** | **Read DTC Information** | ✅ Supported | Masks: 0x01, 0x02, 0x04, 0x06, 0x0A. |
| **0x22** | **Read Data By Identifier** | ✅ Supported | Multi-DID with tx_buffer overflow protection. |
| **0x23** | **Read Memory By Address** | ✅ Supported | Address/Length parsing + bounds check. |
| **0x27** | **Security Access** | ✅ Supported | App-defined seed/key callbacks. |
| **0x28** | **Communication Control** | ✅ Supported | Subfunctions 0x00-0x05 + validation. |
| **0x29** | **Authentication** | ✅ Supported | Certificate Exchange (ISO 14229-1:2020). |
| **0x2A** | **Read Data By Identifier Periodic** | ✅ Supported | Integrated scheduler (Fast, Medium, Slow). |
| **0x2E** | **Write Data By Identifier** | ✅ Supported | Table-driven registry. |
| **0x2F** | **Input Output Control By ID** | ✅ Supported | Actuator control with SID 0x22 integration. |
| **0x31** | **Routine Control** | ✅ Supported | Start, Stop, Request Results. |
| **0x34** | **Request Download** | ✅ Supported | Flexible format identification support. |
| **0x35** | **Request Upload** | ✅ Supported | Symmetrical data provider flow. |
| **0x36** | **Transfer Data** | ✅ Supported | Block streaming. |
| **0x37** | **Request Transfer Exit** | ✅ Supported | Completion logic. |
| **0x3D** | **Write Memory By Address** | ✅ Supported | Echoes address/size in response. |
| **0x3E** | **Tester Present** | ✅ Supported | Busy-relaxed NRC 0x21 logic. |
| **0x85** | **Control DTC Setting** | ✅ Supported | DTC ON/OFF control. |

## Safeguards

- **Service Registry**: Decoupled, table-driven dispatcher.
- **Verification Priority**: Enforces ISO 14229-1 NRC priorities (Session -> Subfunction -> Length -> Security -> Safety).
- **Safety Gates**: Application callbacks block destructive services (Reset, Write, Download) when unsafe.
- **Asynchronous Processing**: Support for `UDS_PENDING` (NRC 0x78) enables non-blocking integration with slow hardware/flash operations.
- **MISRA-C:2012**: Core logic audited for baseline MISRA-C:2012 compliance (Rules 10.x, 17.x, 21.x).

## Simulation & Tests

Host-side simulation is used to validate end-to-end request/response behavior for the implemented services.

- Full service sequence (covers **all** services listed in the matrix above): `tests/integration/test_uds.py`
- Short PCAP + HTML report demo (SessionControl + ReadDataByIdentifier/VIN): `run_capture.sh`
- Outputs are written under `../artifacts/` (local-only).

## Future Services

- **0x24**: Read Scaling Data By Identifier.
- **0x2C**: Dynamically Define Data Identifier.
- **0x38**: Request File Transfer.
- **0x83**: Access Timing Parameter.
- **0x84**: Secured Data Transmission.
- **0x86**: Response On Event.
- **0x87**: Link Control.
