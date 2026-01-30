# ISO 14229-1 (UDS) Service Compliance

This document tracks the compliance and support level for UDS services.

## Compliance Matrix

| SID | Service Name | Status | Notes |
| :--- | :--- | :--- | :--- |
| **0x10** | **Diagnostic Session Control** | ✅ Supported | Default, Extended, Programming. |
| **0x11** | **ECU Reset** | ✅ Supported | Hard, Soft, KeyOffOn. |
| **0x14** | **Clear Diagnostic Information** | ✅ Supported | Group clearing. |
| **0x19** | **Read DTC Information** | ✅ Supported | Subfunction-based reporting. |
| **0x22** | **Read Data By Identifier** | ✅ Supported | Table-driven registry. |
| **0x23** | **Read Memory By Address** | ✅ Supported | Address/Length parsing + bounds check. |
| **0x27** | **Security Access** | ✅ Supported | Seed/Key Exchange. |
| **0x28** | **Communication Control** | ✅ Supported | RX/TX Enable/Disable. |
| **0x29** | **Authentication** | ✅ Supported | Certificate Exchange (ISO 14229-1:2020). |
| **0x2E** | **Write Data By Identifier** | ✅ Supported | Table-driven registry. |
| **0x31** | **Routine Control** | ✅ Supported | Start, Stop, Request Results. |
| **0x34** | **Request Download** | ✅ Supported | OTA Sequence Init. |
| **0x36** | **Transfer Data** | ✅ Supported | Block streaming. |
| **0x37** | **Request Transfer Exit** | ✅ Supported | Completion logic. |
| **0x3D** | **Write Memory By Address** | ✅ Supported | Address/Length parsing + bounds check. |
| **0x3E** | **Tester Present** | ✅ Supported | Zero-Subfunction & SuppressPosResponse. |
| **0x85** | **Control DTC Setting** | ✅ Supported | DTC ON/OFF control. |

## Safeguards

- **Service Registry**: Decoupled, table-driven dispatcher.
- **Verification Priority**: Enforces ISO 14229-1 NRC priorities (Session -> Security -> Safety).
- **Safety Gates**: Application callbacks block destructive services (Reset, Write, Download) when unsafe.
- **Asynchronous Processing**: Support for `UDS_PENDING` (NRC 0x78) enables non-blocking integration with slow hardware/flash operations.

## Future Services

- **0x24**: Read Scaling Data By Identifier.
- **0x2A**: Read Data By Identifier Periodic.
- **0x2C**: Dynamically Define Data Identifier.
