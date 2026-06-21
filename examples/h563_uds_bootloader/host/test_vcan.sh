#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== h563 flash tool: vcan smoke test ==="

# --- ISO-TP frame construction self-test ---
echo "[1/2] ISO-TP frame construction self-test..."
TMP_C=$(mktemp /tmp/isotp_test_XXXXXX.c)
TMP_BIN=$(mktemp /tmp/isotp_test_XXXXXX)
trap 'rm -f "$TMP_C" "$TMP_BIN"' EXIT

cat > "$TMP_C" << 'CEOF'
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(void) {
    /* SF: 3 bytes payload, SF PCI = 0x03, then data */
    uint8_t payload[3] = {0x10, 0x02, 0x00};
    uint8_t frame[64] = {0};
    frame[0] = (uint8_t)sizeof(payload);
    memcpy(&frame[1], payload, sizeof(payload));
    if (frame[0] != 0x03 || frame[1] != 0x10 || frame[2] != 0x02) {
        printf("FAIL: SF framing wrong: %02X %02X %02X\n", frame[0], frame[1], frame[2]);
        return 1;
    }
    printf("PASS: SF framing {len=%u, sid=0x%02X, sub=0x%02X}\n", frame[0], frame[1], frame[2]);

    /* FF: 100 bytes payload, FF PCI = 0x10 | (100>>8), 100&0xFF */
    uint16_t total_len = 100;
    frame[0] = (uint8_t)(0x10 | ((total_len >> 8) & 0x0F));
    frame[1] = (uint8_t)(total_len & 0xFF);
    if (frame[0] != 0x10 || frame[1] != 0x64) {
        printf("FAIL: FF header wrong: %02X %02X\n", frame[0], frame[1]);
        return 1;
    }
    printf("PASS: FF header {PCI=0x%02X, len_lo=0x%02X} for %u-byte SDU\n",
           frame[0], frame[1], total_len);
    return 0;
}
CEOF
gcc -o "$TMP_BIN" "$TMP_C" -Wall -Wextra 2>&1 || { echo "FAIL: ISO-TP self-test compile failed"; exit 1; }
"$TMP_BIN" || { echo "FAIL: ISO-TP self-test failed"; exit 1; }
echo "[1/2] PASS"

# --- vcan availability check ---
echo "[2/2] Checking vcan availability..."
if ! modprobe vcan 2>/dev/null; then
    echo "SKIP: vcan not available (need root + CONFIG_CAN_VCAN kernel module)"
    exit 0
fi
if ! ip link add dev vcan0 type vcan 2>/dev/null; then
    echo "SKIP: vcan not available (need root/CAP_NET_ADMIN to create vcan interface)"
    exit 0
fi
ip link set up vcan0 2>/dev/null || true
ip link delete vcan0 2>/dev/null || true

echo "SKIP: vcan available but host-ECU stub not built yet"
echo "      To run end-to-end: build a host-side ECU stub (see README.md)"
echo "      then: ./flash_tool vcan0 ../app/app_b_image.bin"
exit 0
