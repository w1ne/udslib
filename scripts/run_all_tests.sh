#!/bin/bash
# LibUDS Comprehensive Test Suite Runner
# Executes all test tiers: Unit → Integration → System Validation

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR/.."

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  LibUDS Comprehensive Test Suite              ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════╝${NC}"
echo ""

PASSED=0
FAILED=0

# Function to run a test phase
run_test() {
    local name="$1"
    local command="$2"
    
    echo -e "${YELLOW}[$((PASSED+FAILED+1))] Running: $name${NC}"
    if eval "$command"; then
        echo -e "${GREEN}✅ PASSED${NC}"
        ((PASSED += 1))
    else
        echo -e "${RED}❌ FAILED${NC}"
        ((FAILED += 1))
        if [ "$STOP_ON_FAILURE" = "1" ]; then
            echo "Stopping due to failure (set STOP_ON_FAILURE=0 to continue)"
            exit 1
        fi
    fi
    echo ""
}

# ==================================================================
# Phase 1: Unit Tests
# ==================================================================
echo -e "${GREEN}=== Phase 1: Unit Tests ===${NC}"
echo ""

if [ -d "tests/unit" ] && [ -f "tests/unit/Makefile" ]; then
    run_test "Unit Tests (CMocka)" "cd tests/unit && make clean && make && ./test_uds_core"
else
    echo -e "${YELLOW}⚠️  Unit tests not yet implemented, skipping...${NC}"
    echo ""
fi

# ==================================================================
# Phase 2: Integration Tests (C-to-C)
# ==================================================================
echo -e "${GREEN}=== Phase 2: Integration Tests (C) ===${NC}"
echo ""

run_test "C-to-C Integration (host_sim)" "bash run_integration_test.sh"

# ==================================================================
# Phase 3: Python Integration Tests
# ==================================================================
echo -e "${GREEN}=== Phase 3: Integration Tests (Python) ===${NC}"
echo ""

if command -v python3 &>/dev/null; then
    run_test "Python Test Harness" "python3 tests/integration/test_uds.py"
else
    echo -e "${YELLOW}⚠️  Python3 not found, skipping...${NC}"
    echo ""
fi

# ==================================================================
# Phase 4: External Validation (iso14229)
# ==================================================================
echo -e "${GREEN}=== Phase 4: External Validation (iso14229) ===${NC}"
echo ""

if [ -d "external/iso14229" ]; then
    if [ -f "tests/integration/test_iso14229.sh" ]; then
        run_test "iso14229 Cross-Validation" "bash tests/integration/test_iso14229.sh"
    else
        echo -e "${YELLOW}⚠️  iso14229 test script not yet created, skipping...${NC}"
        echo ""
    fi
else
    echo -e "${YELLOW}⚠️  iso14229 not cloned, skipping...${NC}"
    echo ""
fi

# ==================================================================
# Phase 5: Python Automation (py-uds)
# ==================================================================
echo -e "${GREEN}=== Phase 5: Python Automation (py-uds) ===${NC}"
echo ""

if python3 -c "import uds" 2>/dev/null; then
    if [ -f "tests/integration/test_against_pyuds.py" ]; then
        run_test "py-uds Server Validation" "python3 tests/integration/test_against_pyuds.py"
    else
        echo -e "${YELLOW}⚠️  py-uds test script not yet created, skipping...${NC}"
        echo ""
    fi
else
    echo -e "${YELLOW}⚠️  py-uds not installed, skipping...${NC}"
    echo "    Install with: pip3 install -r requirements.txt"
    echo ""
fi

# ==================================================================
# Summary
# ==================================================================
echo -e "${GREEN}╔════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Test Summary                                  ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════╝${NC}"
echo -e "  ${GREEN}Passed:${NC} $PASSED"
echo -e "  ${RED}Failed:${NC} $FAILED"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}🎉 ALL TESTS PASSED! 🎉${NC}"
    exit 0
else
    echo -e "${RED}⚠️  Some tests failed. See output above.${NC}"
    exit 1
fi
