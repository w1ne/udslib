#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# Generate release notes for UDSLib releases
# Usage: ./generate_release_notes.sh <version>

set -e

VERSION="${1:-}"

if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>" >&2
    echo "Example: $0 1.3.0" >&2
    exit 1
fi

# Header
cat << EOF
# UDSLib v${VERSION}

**Universal Diagnostics Stack for Embedded Systems**

---

## What's New

EOF

# Extract changelog for this version from CHANGELOG.md
if [ -f "CHANGELOG.md" ]; then
    # Find the section for this version and extract until next version or end
    awk -v ver="$VERSION" '
        /^## \[.*\]/ { 
            if (found) exit
            if ($0 ~ "\\[" ver "\\]") found=1
            next
        }
        found { print }
    ' CHANGELOG.md
else
    echo "_No changelog available_"
fi

# Test Results Section
cat << 'EOF'

---

## Test Results

EOF

if [ -f "build/test-results.xml" ]; then
    TOTAL=$(grep -oP 'tests="\K[0-9]+' build/test-results.xml | head -1 || echo "0")
    FAILURES=$(grep -oP 'failures="\K[0-9]+' build/test-results.xml | head -1 || echo "0")
    PASSED=$((TOTAL - FAILURES))
    
    if [ "$FAILURES" -eq 0 ]; then
        STATUS="**All Tests Passed**"
    else
        STATUS="**Some Tests Failed**"
    fi
    
    cat << EOF
$STATUS

- **Total Tests**: ${TOTAL}
- **Passed**: ${PASSED}
- **Failed**: ${FAILURES}

EOF
elif [ -f "test-output.txt" ]; then
    # Parse ctest output
    if grep -q "100% tests passed" test-output.txt; then
        TOTAL=$(grep -oP '\d+(?= tests passed)' test-output.txt | tail -1 || echo "0")
        cat << EOF
**All Tests Passed**

- **Total Tests**: ${TOTAL}
- **Passed**: ${TOTAL}
- **Failed**: 0

EOF
    else
        echo "_Test results available in artifacts_"
    fi
else
    echo "_Unable to parse test results - see workflow artifacts for details_"
fi

# Build Information
cat << EOF

---

## Build Information

- **Version**: ${VERSION}
- **Build Date**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
- **Compliance**: ISO 14229-1 (UDS)
- **Platform Support**: Bare Metal, FreeRTOS, Zephyr, Linux, Windows

## Supported Services

UDSLib v${VERSION} implements **16 ISO 14229-1 services**:

| Service | Description | SID |
|---------|-------------|-----|
| Diagnostic Session Control | Session management | 0x10 |
| ECU Reset | Reset operations | 0x11 |
| Clear Diagnostic Information | DTC clearing | 0x14 |
| Read DTC Information | DTC reporting | 0x19 |
| Read Data By Identifier | Data reading | 0x22 |
| Read Memory By Address | Memory access | 0x23 |
| Security Access | Seed/Key exchange | 0x27 |
| Communication Control | TX/RX control | 0x28 |
| Authentication | Certificate exchange | 0x29 |
| Write Data By Identifier | Data writing | 0x2E |
| Routine Control | Routine execution | 0x31 |
| Request Download | OTA init | 0x34 |
| Transfer Data | Block streaming | 0x36 |
| Request Transfer Exit | OTA completion | 0x37 |
| Write Memory By Address | Memory writing | 0x3D |
| Tester Present | Keep-alive | 0x3E |
| Control DTC Setting | DTC ON/OFF | 0x85 |

---

## Installation

### Download Pre-built Binaries

Download the attached artifacts:
- \`uds_host_sim\` - Host-based ECU simulator
- \`unit_tests\` - Complete test suite

### Build from Source

\`\`\`bash
git clone https://github.com/w1ne/udslib.git
cd udslib
git checkout v${VERSION}
mkdir build && cd build
cmake ..
make
ctest
\`\`\`

---

## Documentation

- [Architecture Guide](docs/ARCHITECTURE.md)
- [API Documentation](docs/CLIENT_API.md)
- [Testing Strategy](docs/TESTING_STRATEGY.md)
- [Porting Guide](README.md#4-porting-guide)
- [Commercial Licensing](docs/COMMERCIAL_STRATEGY.md)

## Licensing

- **Community**: PolyForm Noncommercial 1.0.0 (noncommercial use). See LICENSE.
- **Commercial**: 5,000 EUR for production/commercial use, includes integration + 1 year support.
- **Contact**: andrii@shylenko.com for commercial terms and support.

---

## Report Issues

Found a bug? [Open an issue](https://github.com/w1ne/udslib/issues)

## Full Changelog

See [CHANGELOG.md](CHANGELOG.md) for complete version history.
EOF
