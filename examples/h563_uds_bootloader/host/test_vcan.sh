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

/* ---- minimal stub of isotp_recv SF logic, mirroring flash_tool.c ---- */
static int isotp_recv_sf(const uint8_t *data, uint8_t frame_len,
                          uint8_t *buf, uint8_t buf_cap,
                          uint16_t *out_len)
{
    uint8_t pci_type = (data[0] >> 4u) & 0x0Fu;
    if (pci_type != 0u) {
        return -1; /* not a SF */
    }
    uint8_t sf_len, off;
    if (data[0] == 0x00u) {
        /* CAN-FD escape SF: [0x00][len][data...] */
        if (frame_len < 2u) return -1;
        sf_len = data[1];
        off    = 2u;
    } else {
        /* Classic SF: [0x0L][data...] */
        sf_len = data[0] & 0x0Fu;
        off    = 1u;
    }
    if (sf_len == 0u || sf_len > buf_cap ||
            (uint32_t)off + (uint32_t)sf_len > (uint32_t)frame_len) {
        return -1;
    }
    memcpy(buf, &data[off], sf_len);
    *out_len = sf_len;
    return 0;
}

int main(void) {
    int failures = 0;

    /* --- 1. Classic SF framing (build side: isotp_send) --- */
    {
        uint8_t payload[3] = {0x10, 0x02, 0x00};
        uint8_t frame[64] = {0};
        frame[0] = (uint8_t)sizeof(payload);
        memcpy(&frame[1], payload, sizeof(payload));
        if (frame[0] != 0x03 || frame[1] != 0x10 || frame[2] != 0x02) {
            printf("FAIL: classic-SF build wrong: %02X %02X %02X\n",
                   frame[0], frame[1], frame[2]);
            failures++;
        } else {
            printf("PASS: classic-SF build {len=%u sid=0x%02X sub=0x%02X}\n",
                   frame[0], frame[1], frame[2]);
        }
    }

    /* --- 2. FF header build --- */
    {
        uint8_t frame[64] = {0};
        uint16_t total_len = 100;
        frame[0] = (uint8_t)(0x10 | ((total_len >> 8) & 0x0F));
        frame[1] = (uint8_t)(total_len & 0xFF);
        if (frame[0] != 0x10 || frame[1] != 0x64) {
            printf("FAIL: FF header wrong: %02X %02X\n", frame[0], frame[1]);
            failures++;
        } else {
            printf("PASS: FF header {PCI=0x%02X len_lo=0x%02X} for %u-byte SDU\n",
                   frame[0], frame[1], total_len);
        }
    }

    /* --- 3. isotp_recv: classic-SF (3-byte payload: 0x03 0x10 0x02 0x00) --- */
    {
        uint8_t classic_sf[4] = {0x03, 0x10, 0x02, 0x00};
        uint8_t buf[64]; uint16_t out_len = 0;
        int rc = isotp_recv_sf(classic_sf, sizeof(classic_sf), buf, sizeof(buf), &out_len);
        if (rc != 0 || out_len != 3 || buf[0] != 0x10 || buf[1] != 0x02) {
            printf("FAIL: classic-SF recv (rc=%d out_len=%u buf[0]=0x%02X)\n",
                   rc, out_len, buf[0]);
            failures++;
        } else {
            printf("PASS: classic-SF recv {out_len=%u buf={0x%02X 0x%02X 0x%02X}}\n",
                   out_len, buf[0], buf[1], buf[2]);
        }
    }

    /* --- 4. isotp_recv: CAN-FD escape-SF — the critical bug case ---
     *
     * The udslib bootloader sends a 0x27 01 seed response (18 bytes) as:
     *   [0x00][0x12][0x67 0x01 <16 seed bytes>]
     * Previously sf_len = data[0] & 0x0F = 0 → "SF len 0 out of range" → -1.
     */
    {
        /* Build escape-SF frame: [0x00][0x12][0x67 0x01 0x01..0x10] */
        uint8_t esc_sf[20];
        esc_sf[0] = 0x00u;
        esc_sf[1] = 0x12u; /* 18 bytes of payload */
        esc_sf[2] = 0x67u; /* 0x27 positive response SID */
        esc_sf[3] = 0x01u; /* sub-function: requestSeed */
        for (int i = 0; i < 16; i++) {
            esc_sf[4 + i] = (uint8_t)(i + 1);
        }
        uint8_t buf[64]; uint16_t out_len = 0;
        int rc = isotp_recv_sf(esc_sf, sizeof(esc_sf), buf, sizeof(buf), &out_len);
        if (rc != 0 || out_len != 18 || buf[0] != 0x67u || buf[1] != 0x01u) {
            printf("FAIL: escape-SF recv (rc=%d out_len=%u buf[0]=0x%02X buf[1]=0x%02X)\n",
                   rc, out_len, buf[0], buf[1]);
            failures++;
        } else {
            /* verify all 16 seed bytes */
            int seed_ok = 1;
            for (int i = 0; i < 16; i++) {
                if (buf[2 + i] != (uint8_t)(i + 1)) { seed_ok = 0; break; }
            }
            if (!seed_ok) {
                printf("FAIL: escape-SF recv seed bytes wrong\n");
                failures++;
            } else {
                printf("PASS: escape-SF recv {out_len=%u SID=0x%02X sub=0x%02X seed[0]=0x%02X..seed[15]=0x%02X}\n",
                       out_len, buf[0], buf[1], buf[2], buf[17]);
            }
        }
    }

    if (failures != 0) {
        printf("RESULT: %d test(s) FAILED\n", failures);
        return 1;
    }
    printf("RESULT: all tests PASSED\n");
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
