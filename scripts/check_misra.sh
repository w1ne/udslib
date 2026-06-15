#!/bin/bash
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

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

# 3. MISRA-C:2012 addon against a documented deviation baseline.
#    Any rule NOT in ACCEPTED is a regression: fix it, or add it to
#    docs/MISRA.md and this list with rationale. No mandatory rule is listed,
#    so introducing a mandatory-rule violation fails the build.
#    (General cppcheck analysis is covered by the static-analysis CI job.)
echo -n "[3/3] Running MISRA-C:2012 addon (deviation baseline)... "
if command -v cppcheck &> /dev/null; then
    ACCEPTED="2.3 2.4 2.5 8.7 8.9 10.1 10.4 10.8 11.1 11.9 12.1 12.3 14.4 15.5 15.6 15.7 17.7"
    MISRA_OUT=$(cppcheck --addon=misra --enable=all --suppress=missingIncludeSystem \
        --suppress=unusedFunction --suppress=checkersReport --inline-suppr \
        -I include -I src/core src/ 2>&1)
    NEW=""
    for r in $(echo "$MISRA_OUT" | grep -oE 'misra-c2012-[0-9.]+' | sed 's/misra-c2012-//' | sort -u); do
        case " $ACCEPTED " in
            *" $r "*) ;;
            *) NEW="$NEW $r" ;;
        esac
    done
    if [ -n "$NEW" ]; then
        echo -e "${RED}FAILED${NC}"
        echo "New MISRA-C:2012 rule violation(s) outside the documented deviation list:$NEW"
        echo "Fix them, or document them in docs/MISRA.md and add to ACCEPTED."
        exit 1
    fi
    echo -e "${GREEN}PASSED${NC} (no mandatory violations; deviations: see docs/MISRA.md)"
else
    echo -e "${RED}SKIPPED${NC} (cppcheck not installed)"
fi

echo "--------------------------------------------------"
echo -e "${GREEN}COMPLIANCE CHECK COMPLETE${NC}"
echo "--------------------------------------------------"
