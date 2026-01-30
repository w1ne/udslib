# ISO 14229-1 (UDS) Service Compliance

This document tracks the implementation status of UDS services in LibUDS.

## Compliance Matrix

| SID | Service Name | Status | Support Level |
| :--- | :--- | :--- | :--- |
| **0x10** | **Diagnostic Session Control** | ✅ Supported | Full (Default, Extended, Programming) |
| **0x11** | ECU Reset | ✅ Supported | Hard, Soft, KeyOffOn subfunctions |
| **0x27** | **Security Access** | ✅ Supported | Seed/Key Exchange |
| **0x28** | Communication Control | ✅ Supported | RX/TX Enable/Disable states |
| **0x3E** | **Tester Present** | ✅ Supported | Zero-Subfunction & SuppressPosResponse |
| **0x83** | Access Timing Parameter | ❌ N/A | Not planned for initial release |
| **0x84** | Secured Data Transmission | ❌ N/A | Not planned for initial release |
| **0x85** | Control DTC Setting | ❌ Planned | Support pending |
| **0x86** | Response On Event | ❌ N/A | Not planned for initial release |
| **0x87** | Link Control | ❌ N/A | Not planned for initial release |
| **0x22** | **Read Data By Identifier** | ✅ Supported | Dynamic RDBI (Mocked for VIN F190) |
| **0x23** | Read Memory By Address | ❌ Optional | Feature flag dependent |
| **0x24** | Read Scaling Data By Identifier | ❌ Planned | Support pending |
| **0x2A** | Read Data By Periodic Identifier| ❌ Planned | Support pending |
| **0x2C** | Dynamically Define Data Identifier| ❌ Planned | Support pending |
| **0x2E** | Write Data By Identifier | ✅ Experimental| Basic implementation in branch |
| **0x3D** | Write Memory By Address | ❌ Optional | Feature flag dependent |
| **0x14** | Clear Diagnostic Information | ❌ Planned | Support pending |
| **0x19** | Read DTC Information | ❌ Planned | Support pending |
| **0x2F** | Input Output Control By ID | ❌ Planned | Support pending |
| **0x31** | Routine Control | ✅ Supported | Base logic in core |
| **0x34** | Request Download | ✅ Supported | Bootloader integration ready |
| **0x35** | Request Upload | ❌ Planned | Support pending |
| **0x36** | Transfer Data | ✅ Supported | Streaming support |
| **0x37** | Request Transfer Exit | ✅ Supported | Completion logic |
| **0x38** | Request File Transfer | ❌ N/A | Not planned for initial release |

## Implementation Progress

The current core focuses on **Phase 1: Diagnostic & Data Exchange**. This covers ~80% of common automotive and industrial use cases.

### Service Handler Roadmap

- **Task 1: Maintenance Services**: Reset (0x11), Communication Control (0x28), DTC Management (0x14, 0x19, 0x85). [DONE for 0x11, 0x28]
- **Task 2: Advanced Data**: Periodic RDBI (0x2A), Scaling (0x24), Dynamic IDs (0x2C).
- **Task 3: Memory & Security**: Full Upload (0x35), Memory Address services (0x23, 0x3D).
