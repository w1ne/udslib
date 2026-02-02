#!/bin/bash
# scripts/check_misra.sh
# Automated MISRA-C:2012 baseline compliance checker for UDSLib.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "--------------------------------------------------"
echo "UDSLib: Systematic MISRA-C Compliance Scan"
echo "--------------------------------------------------"

# 1. Check for Standard Library usage in src/ (Rule 21.x)
echo -n "[1/3] Checking for forbidden stdlib usage... "
FORBIDDEN="malloc\|free\|printf\|fprintf\|sprintf\|exit\|abort\|realloc\|calloc"
# Use -w to match whole words only to avoid flagging "transfer_exit"
if grep -rnw "$FORBIDDEN" src/ | grep -v "uds_internal_log"; then
    echo -e "${RED}FAILED${NC}"
    echo "Found forbidden standard library calls in core code."
    exit 1
fi
echo -e "${GREEN}PASSED${NC}"

# 2. Check for Magic Numbers (Rule 10.x baseline)
echo -n "[2/3] Checking for magic numbers (core dispatch)... "
# Strip comments and look for hex values in assignments or returns
# Ignore common UDS prefixes and safe values (0, 1, 0x40, 0xFF, 0x78)
# Skip lines that look like comments (starting with * or /)
MAGIC_SCAN=$(grep -v "^[[:space:]]*[*\/]" src/core/uds_core.c | sed 's|//.*||; s|/\*.*\*/||' | grep "0x[0-9a-fA-F]\{2\}" | grep -v "UDS_SID\|UDS_NRC\|UDS_MASK\|UDS_SESSION\|0x01u\|0x00u\|0x40u\|0xFFu\|0x78u\|0x78" || true)
if [ -n "$MAGIC_SCAN" ]; then
    echo -e "${RED}FAILED${NC}"
    echo "Potential magic numbers found in uds_core.c (stripped):"
    echo "$MAGIC_SCAN"
    exit 1
fi
echo -e "${GREEN}PASSED${NC}"

# 3. Static Analysis (if available)
echo -n "[3/3] Running Static Analysis (cppcheck)... "
if command -v cppcheck &> /dev/null; then
    cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction src/ include/ --error-exitcode=1 > /dev/null 2>&1
    echo -e "${GREEN}PASSED${NC}"
else
    echo -e "${RED}SKIPPED${NC} (cppcheck not installed)"
fi

echo "--------------------------------------------------"
echo -e "${GREEN}COMPLIANCE CHECK COMPLETE${NC}"
echo "--------------------------------------------------"
