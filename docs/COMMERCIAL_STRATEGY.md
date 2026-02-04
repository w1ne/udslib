# UDSLib Strategic Positioning

UDSLib bridges the gap between high-overhead Tier-1 diagnostic stacks and unverified open-source projects. It provides a production-ready, safety-first implementation of ISO 14229-1 for embedded environments where reliability is non-negotiable.

## 1. Competitive Landscape

| Attribute | Proprietary Stacks ($50k+) | Typical Open Source | UDSLib v1.8.0 |
| :--- | :--- | :--- | :--- |
| **Accessibility**| High entry barriers | Easy to start | **Instant Simulation** |
| **Portability** | Often HW-locked | Poor abstraction | **Target-Agnostic** |
| **Assurance** | Certified but opaque | Unverified | **Audit-Ready (MISRA)** |
| **Integration** | Heavy toolchain | Minimal tooling | **Rich Ecosystem** |

**Core Position**: "A production-ready diagnostic stack that prioritizes visibility and safety. It integrates into modern CI/CD pipelines and runs anywhere from bare-metal to Zephyr."

## 2. Technical Value Pillars

- **Safety-First Core**: Built-in safety gates (`fn_is_safe`), integer overflow protection, and deterministic timing logic.
- **Enterprise-Grade Verification**: 100% core test coverage and systematic MISRA-C:2012 baseline auditing.
- **Unified Release Model**: We do not gate safety or core features. Version 1.8.0 is the definitive, hardened codebase for all production users.
- **Ecosystem Ready**: Includes professional-grade Wireshark dissectors, Python validation harnesses, and comprehensive documentation.

## 3. Deployment Strategy

UDSLib satisfies the requirements of engineering teams who need to move fast without compromising on safety:

- **Architects**: Focus on the low memory footprint, zero-copy architecture, and clean OSAL integration for multi-threaded RTOS environments.
- **Safety Engineers**: Benefit from the MISRA-C audit, systematic error handling (NRC priorities), and non-blocking asynchronous state machine.
- **Managers**: Prioritize the predictable one-time licensing model and shortened integration time.

## 4. Ecosystem Partners

UDSLib is designed to be the "UDS module of choice" for modern automotive ecosystems:

1. **RTOS Communities**: Primary module for Zephyr Project and FreeRTOS diagnostic integrations.
2. **Semi Vendors**: Optimized for automotive-grade silicon like NXP S32K and ST Stellar families.
3. **Engineering Consultancies**: The go-to stack for firms delivering custom ECU firmware who need a reliable, source-available foundation.
4. **Tool Providers**: Pre-integrated with Wireshark and Python-based validation suites.

---

## 5. Licensing Model

We believe safety-critical code should be transparent while protecting commercial value:

- **Community License**: PolyForm Noncommercial 1.0.0 for personal, research, and other noncommercial use. (See `../LICENSE`.)
- **Commercial License**: 5,000 EUR, includes integration package (up to 40h) and 1 year support; perpetual, royalty-free for production use.
- **Source Access & Assurance**: Full repository access plus MISRA compliance and test coverage reports under commercial terms.

*For licensing inquiries and technical support, contact andrii@shylenko.com.*
