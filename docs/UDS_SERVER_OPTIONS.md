# UDS Server Options

This document compares open-source UDS server implementations for validation.

## 1. Python Solutions

### **py-uds** (Recommended)
- **GitHub**: [bri3d/py-uds](https://github.com/bri3d/py-uds)
- **Role**: Server and Client.
- **Strengths**: Simulation support, multiple buses (CAN/LIN), timing control.
- **Use Case**: robust automated testing.

### **udsoncan**
- **GitHub**: [pylessard/python-udsoncan](https://github.com/pylessard/python-udsoncan)
- **Role**: Client only.
- **Strengths**: ISO-14229 compliant, security testing, fuzzing.
- **Use Case**: Negative testing and security validation.

## 2. C/C++ Implementations

### **driftregion/iso14229** (Recommended)
- **GitHub**: [driftregion/iso14229](https://github.com/driftregion/iso14229)
- **Role**: Server and Client.
- **Strengths**: Production-quality C code, Linux kernel ISO-TP support.
- **Use Case**: Independent C-based validation.

### **Honinb0n/uds-server-simulator**
- **GitHub**: [Honinb0n/uds-server-simulator](https://github.com/Honinb0n/uds-server-simulator)
- **Role**: Server simulator.
- **Strengths**: JSON configuration, based on `can-utils`.
- **Use Case**: Rapid prototyping.

## Recommendation Matrix

| Requirement | Recommended Tool | Why |
|:------------|:----------------|:----|
| **Independent C validation** | `driftregion/iso14229` | Separate C codebase, server support. |
| **Flexible automation** | `py-uds` | Scriptable server simulation. |
| **Security & fuzzing** | `udsoncan` | Malformed message detection. |
| **Quick prototyping** | `Honinb0n` | JSON config. |

## Integration Strategy

### Phase 1: Internal Validation
Use `udslib/host_sim` for rapid CI/CD loops.

### Phase 2: Independent C Validation
1. Clone `driftregion/iso14229`.
2. Cross-verify Client/Server roles against UDSLib.

### Phase 3: Python Harness
1. Use `py-uds` for complex scenarios.
2. Use `udsoncan` for fuzzing.
