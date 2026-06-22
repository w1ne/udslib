/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file flash_tool.c
 * @brief Linux SocketCAN CAN-FD host flash tool for STM32H563 UDS OTA bootloader.
 *
 * Usage:
 *   flash_tool <can-iface> <image.bin>   — full OTA download + activate
 *   flash_tool <can-iface> --rollback    — tester-commanded rollback (0xFF03)
 *
 * Flash sequence (normal):
 *   1. 0x10 02  DiagnosticSessionControl(programming)
 *   2. 0x22 F1A0 ReadDataByIdentifier(active-bank DID)
 *   3. 0x27 01  SecurityAccess(requestSeed)
 *   4. 0x27 02  SecurityAccess(sendKey) — AES-128-CMAC of seed
 *   5. 0x34     RequestDownload to inactive bank app region
 *   6. 0x36 x N TransferData (chunked)
 *   7. 0x37     RequestTransferExit
 *   8. 0x31 01 FF01 CheckProgrammingDependencies
 *   9. 0x31 01 FF02 ActivateSoftware (bank swap + reset)
 *
 * Rollback sequence (--rollback):
 *   1. 0x10 02  DiagnosticSessionControl(programming)
 *   2. 0x27 01  SecurityAccess(requestSeed)
 *   3. 0x27 02  SecurityAccess(sendKey) — AES-128-CMAC of seed
 *   4. 0x31 01 FF03 PerformRollback (revert to other bank + reset)
 *
 * ISO-TP FD is hand-rolled (SF/FF/CF/FC) for minimal footprint.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <mbedtls/cipher.h>
#include <mbedtls/cmac.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define TESTER_ID   0x7E0u
#define ECU_ID      0x7E8u

/* Inactive app base formula: bank_base + BL_REGION_SIZE
 * bank_base = 0x08000000 + bank * 0x100000
 * BL_REGION_SIZE = 0x18000 (96 KB bootloader) */
#define BANK_BASE_OFFSET  0x08000000UL
#define BANK_SIZE         0x100000UL
#define BL_REGION_SIZE    0x18000UL

/* DEMO_SECRET — must match bootloader/main.c DEMO_SECRET exactly */
static const uint8_t DEMO_SECRET[16] = {
    0xA3, 0xF1, 0x7C, 0x28, 0xB6, 0x4E, 0xD9, 0x05,
    0x71, 0xCC, 0x3A, 0x8F, 0x52, 0x0B, 0xE4, 0x96
};

/* ---------------------------------------------------------------------------
 * CAN FD helpers
 * ------------------------------------------------------------------------- */

/** Round up payload length to the nearest valid CAN FD DLC size. */
static uint8_t canfd_round_len(uint16_t n)
{
    static const uint8_t dlc_sizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,
                                         12, 16, 20, 24, 32, 48, 64};
    /* Guard: zero-length payload must still produce a valid 1-byte frame */
    if (n == 0u) {
        n = 1u;
    }
    for (int i = 0; i < 16; i++) {
        if ((uint16_t)dlc_sizes[i] >= n) {
            return dlc_sizes[i];
        }
    }
    return 64u;
}

/**
 * Read one CAN FD frame with a timeout.
 *
 * @param sock       SocketCAN FD socket (non-blocking).
 * @param fr         Output frame.
 * @param timeout_ms Receive timeout in milliseconds.
 * @return 0 on success, -1 on error, 1 on timeout.
 */
static int can_recv_frame(int sock, struct canfd_frame *fr, int timeout_ms)
{
    fd_set rd;
    struct timeval tv;
    FD_ZERO(&rd);
    FD_SET(sock, &rd);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(sock + 1, &rd, NULL, NULL, &tv);
    if (r < 0) {
        perror("select");
        return -1;
    }
    if (r == 0) {
        return 1; /* timeout */
    }
    ssize_t n = recv(sock, fr, sizeof(*fr), 0);
    if (n <= 0) {
        perror("recv");
        return -1;
    }
    return 0;
}

/**
 * Send one CAN FD frame containing `payload` bytes, padded to a valid DLC.
 *
 * @param sock     SocketCAN FD socket.
 * @param can_id   11-bit CAN ID.
 * @param payload  Payload bytes (max 64).
 * @param plen     Number of payload bytes.
 * @return 0 on success, -1 on error.
 */
static int can_send_frame(int sock, uint32_t can_id, const uint8_t *payload, uint8_t plen)
{
    struct canfd_frame fr;
    memset(&fr, 0, sizeof(fr));
    fr.can_id = can_id & CAN_SFF_MASK;
    uint8_t padded = canfd_round_len((uint16_t)plen);
    fr.len   = padded;
    fr.flags = CANFD_BRS;
    memcpy(fr.data, payload, plen);
    /* Pad remaining bytes with 0xCC (CAN FD padding convention) */
    if (padded > plen) {
        memset(&fr.data[plen], 0xCCu, (size_t)(padded - plen));
    }
    ssize_t n = write(sock, &fr, sizeof(fr));
    if (n != sizeof(fr)) {
        perror("write canfd_frame");
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * ISO-TP FD (hand-rolled, single-message request/response)
 * ------------------------------------------------------------------------- */

#define ISOTP_SF_MAX  62u   /* CAN FD SF max data bytes (byte 0 = PCI) */
#define ISOTP_FF_DATA 62u   /* CAN FD FF first-frame data bytes after 2-byte PCI */

/**
 * Send an ISO-TP SDU on `tx_id`.
 *
 * Handles:
 *   - SF  (Single Frame)  for len <= 62
 *   - FF+CF (First/Consecutive Frames) for len > 62; waits for FC from `fc_rx_id`.
 *
 * @param sock      SocketCAN FD socket.
 * @param tx_id     CAN ID to transmit on.
 * @param fc_rx_id  CAN ID on which to receive the Flow Control frame.
 * @param data      SDU bytes.
 * @param len       SDU length.
 * @return 0 on success, -1 on error.
 */
static int isotp_send(int sock, uint32_t tx_id, uint32_t fc_rx_id,
                      const uint8_t *data, uint16_t len)
{
    uint8_t frame_buf[64];

    if (len <= ISOTP_SF_MAX) {
        /* Single Frame */
        frame_buf[0] = (uint8_t)len;
        memcpy(&frame_buf[1], data, len);
        return can_send_frame(sock, tx_id, frame_buf, (uint8_t)(1u + len));
    }

    /* First Frame */
    frame_buf[0] = (uint8_t)(0x10u | ((len >> 8u) & 0x0Fu));
    frame_buf[1] = (uint8_t)(len & 0xFFu);
    uint16_t ff_data = (len > ISOTP_FF_DATA) ? ISOTP_FF_DATA : len;
    memcpy(&frame_buf[2], data, ff_data);
    if (can_send_frame(sock, tx_id, frame_buf, (uint8_t)(2u + ff_data)) != 0) {
        return -1;
    }

    /* Wait for Flow Control from the receiver */
    struct canfd_frame fc_frame;
    int rc = can_recv_frame(sock, &fc_frame, 1000);
    if (rc != 0) {
        fprintf(stderr, "isotp_send: no FC received (rc=%d)\n", rc);
        return -1;
    }
    if ((fc_frame.can_id & CAN_SFF_MASK) != (fc_rx_id & CAN_SFF_MASK)) {
        fprintf(stderr, "isotp_send: unexpected CAN ID 0x%03X waiting for FC\n",
                fc_frame.can_id & CAN_SFF_MASK);
        return -1;
    }
    if ((fc_frame.data[0] & 0xF0u) != 0x30u) {
        fprintf(stderr, "isotp_send: FC PCI wrong: 0x%02X\n", fc_frame.data[0]);
        return -1;
    }

    /*
     * Parse Flow Control fields (ISO 15765-2 §9.8.4):
     *   data[0] & 0x0F = FS  (0=ContinueToSend, 1=Wait, 2=Overflow)
     *   data[1]        = BlockSize (0 = send all CFs without waiting for another FC)
     *   data[2]        = STmin
     *
     * STmin decode (ISO 15765-2 Table 6):
     *   0x00-0x7F : 0-127 ms
     *   0xF1-0xF9 : 100-900 µs (in 100 µs steps)
     *   other     : treat as 0 (undefined, be conservative)
     */
    uint8_t fc_fs         = fc_frame.data[0] & 0x0Fu;
    uint8_t fc_block_size = fc_frame.data[1];
    uint8_t fc_stmin_raw  = fc_frame.data[2];
    unsigned int stmin_us;   /* inter-CF gap in microseconds */

    if (fc_fs == 2u) {
        fprintf(stderr, "isotp_send: FC Overflow — ECU cannot receive\n");
        return -1;
    }
    /* fc_fs == 1 (Wait) is handled per-FC receive below */

    if (fc_stmin_raw <= 0x7Fu) {
        stmin_us = (unsigned int)fc_stmin_raw * 1000u; /* ms → µs */
    } else if (fc_stmin_raw >= 0xF1u && fc_stmin_raw <= 0xF9u) {
        stmin_us = (unsigned int)(fc_stmin_raw - 0xF0u) * 100u; /* 100-900 µs */
    } else {
        stmin_us = 0u; /* reserved range — treat as 0 */
    }

    /* Send Consecutive Frames, respecting block_size and STmin */
    uint16_t sent          = ff_data;
    uint8_t  sn            = 1u;
    uint8_t  cfs_in_block  = 0u; /* CFs sent since last FC */

    while (sent < len) {
        /*
         * If block_size != 0 and we have filled the current block, wait for
         * the next FC before continuing.
         */
        if (fc_block_size != 0u && cfs_in_block >= fc_block_size) {
            cfs_in_block = 0u;
            /* Wait for next FC */
            for (;;) {
                int rc2 = can_recv_frame(sock, &fc_frame, 1000);
                if (rc2 != 0) {
                    fprintf(stderr, "isotp_send: no FC after block (rc=%d)\n", rc2);
                    return -1;
                }
                if ((fc_frame.can_id & CAN_SFF_MASK) != (fc_rx_id & CAN_SFF_MASK)) {
                    continue; /* ignore other IDs */
                }
                if ((fc_frame.data[0] & 0xF0u) != 0x30u) {
                    fprintf(stderr, "isotp_send: expected FC, got 0x%02X\n",
                            fc_frame.data[0]);
                    return -1;
                }
                fc_fs         = fc_frame.data[0] & 0x0Fu;
                fc_block_size = fc_frame.data[1];
                fc_stmin_raw  = fc_frame.data[2];
                if (fc_stmin_raw <= 0x7Fu) {
                    stmin_us = (unsigned int)fc_stmin_raw * 1000u;
                } else if (fc_stmin_raw >= 0xF1u && fc_stmin_raw <= 0xF9u) {
                    stmin_us = (unsigned int)(fc_stmin_raw - 0xF0u) * 100u;
                } else {
                    stmin_us = 0u;
                }
                if (fc_fs == 2u) {
                    fprintf(stderr, "isotp_send: FC Overflow on block boundary\n");
                    return -1;
                }
                if (fc_fs == 0u) {
                    break; /* ContinueToSend */
                }
                /* fc_fs == 1 (Wait) — keep waiting for another FC */
            }
        }

        uint16_t remain = (uint16_t)(len - sent);
        uint8_t  chunk  = (remain > 63u) ? 63u : (uint8_t)remain;
        frame_buf[0] = (uint8_t)(0x20u | (sn & 0x0Fu));
        memcpy(&frame_buf[1], data + sent, chunk);
        if (can_send_frame(sock, tx_id, frame_buf, (uint8_t)(1u + chunk)) != 0) {
            return -1;
        }
        sent = (uint16_t)(sent + chunk);
        sn   = (uint8_t)((sn + 1u) & 0x0Fu);
        cfs_in_block++;

        /* Honor STmin inter-frame gap */
        if (stmin_us > 0u && sent < len) {
            usleep((useconds_t)stmin_us);
        }
    }
    return 0;
}

/**
 * Receive one ISO-TP SDU from `rx_id`.
 *
 * Handles SF and FF+CF reassembly. Sends FC as `fc_tx_id` when a multi-frame
 * reception is in progress.
 *
 * @param sock        SocketCAN FD socket.
 * @param rx_id       CAN ID to filter on.
 * @param fc_tx_id    CAN ID to use for Flow Control.
 * @param buf         Output buffer for the reassembled SDU.
 * @param out_len     Output: number of bytes written to buf.
 * @param buf_cap     Capacity of buf.
 * @param timeout_ms  Receive timeout in milliseconds.
 * @return 0 on success, -1 on error, 1 on timeout.
 */
static int isotp_recv(int sock, uint32_t rx_id, uint32_t fc_tx_id,
                      uint8_t *buf, uint16_t *out_len, uint16_t buf_cap,
                      int timeout_ms)
{
    struct canfd_frame fr;
    *out_len = 0;

    /* Wait for the first frame from rx_id */
    for (;;) {
        int rc = can_recv_frame(sock, &fr, timeout_ms);
        if (rc != 0) {
            return rc; /* timeout or error */
        }
        if ((fr.can_id & CAN_SFF_MASK) == (rx_id & CAN_SFF_MASK)) {
            break;
        }
        /* Ignore frames from other IDs */
    }

    uint8_t pci_type = (fr.data[0] >> 4u) & 0x0Fu;

    if (pci_type == 0u) {
        /*
         * Single Frame — two variants (ISO 15765-2 §9.6.1):
         *   Classic SF  : data[0] = 0x0L (L = length 1-7), data starts at byte 1
         *   CAN-FD escape SF: data[0] = 0x00, data[1] = length (>7), data starts at byte 2
         *
         * The escape form is used when the payload does not fit in the classic nibble
         * (i.e. length > 7).  The udslib bootloader uses it for the 0x27 seed response
         * (18 bytes: 0x67 0x01 + 16-byte seed).
         */
        uint8_t sf_len, off;
        if (fr.data[0] == 0x00u) {
            /* CAN-FD escape SF: [0x00][len][data...] */
            if (fr.len < 2u) {
                fprintf(stderr, "isotp_recv: escape-SF frame too short (len=%u)\n", fr.len);
                return -1;
            }
            sf_len = fr.data[1];
            off    = 2u;
        } else {
            /* Classic SF: [0x0L][data...] */
            sf_len = fr.data[0] & 0x0Fu;
            off    = 1u;
        }
        if (sf_len == 0u || sf_len > (uint8_t)buf_cap ||
                (uint32_t)off + (uint32_t)sf_len > (uint32_t)fr.len) {
            fprintf(stderr, "isotp_recv: SF len=%u out of range (off=%u, frame_len=%u, cap=%u)\n",
                    sf_len, off, fr.len, buf_cap);
            return -1;
        }
        memcpy(buf, &fr.data[off], sf_len);
        *out_len = sf_len;
        return 0;
    }

    if (pci_type == 1u) {
        /* First Frame */
        uint16_t total_len = (uint16_t)(((uint16_t)(fr.data[0] & 0x0Fu) << 8u) |
                                        (uint16_t)fr.data[1]);
        if (total_len > buf_cap) {
            fprintf(stderr, "isotp_recv: FF total_len=%u exceeds buf_cap=%u\n",
                    total_len, buf_cap);
            return -1;
        }

        uint16_t ff_data = (uint16_t)(fr.len - 2u); /* bytes after 2-byte PCI */
        if (ff_data > total_len) {
            ff_data = total_len;
        }
        memcpy(buf, &fr.data[2], ff_data);
        uint16_t received = ff_data;

        /* Send Flow Control: CTS, block_size=0, ST_min=0 */
        uint8_t fc_buf[3] = {0x30u, 0x00u, 0x00u};
        if (can_send_frame(sock, fc_tx_id, fc_buf, 3u) != 0) {
            return -1;
        }

        /* Receive Consecutive Frames */
        uint8_t expected_sn = 1u;
        while (received < total_len) {
            int rc = can_recv_frame(sock, &fr, timeout_ms);
            if (rc != 0) {
                fprintf(stderr, "isotp_recv: timeout waiting for CF\n");
                return (rc > 0) ? 1 : -1;
            }
            if ((fr.can_id & CAN_SFF_MASK) != (rx_id & CAN_SFF_MASK)) {
                continue; /* ignore other IDs */
            }
            if ((fr.data[0] & 0xF0u) != 0x20u) {
                fprintf(stderr, "isotp_recv: expected CF, got PCI=0x%02X\n", fr.data[0]);
                return -1;
            }
            uint8_t sn = fr.data[0] & 0x0Fu;
            if (sn != (expected_sn & 0x0Fu)) {
                fprintf(stderr, "isotp_recv: CF SN mismatch: got %u, expected %u\n",
                        sn, expected_sn & 0x0Fu);
                return -1;
            }
            expected_sn = (uint8_t)((expected_sn + 1u) & 0x0Fu);

            uint16_t remain = (uint16_t)(total_len - received);
            uint8_t  chunk  = (uint8_t)((fr.len - 1u) < remain ? (fr.len - 1u) : remain);
            memcpy(&buf[received], &fr.data[1], chunk);
            received = (uint16_t)(received + chunk);
        }

        *out_len = total_len;
        return 0;
    }

    fprintf(stderr, "isotp_recv: unexpected PCI type=0x%X\n", pci_type);
    return -1;
}

/* ---------------------------------------------------------------------------
 * UDS request / response helper
 * ------------------------------------------------------------------------- */

/**
 * Send a UDS request and receive one UDS response.
 *
 * @param sock     SocketCAN FD socket.
 * @param req      Request bytes (including SID).
 * @param req_len  Request length.
 * @param resp     Output buffer for the response (including SID).
 * @param resp_len Output: number of bytes in response.
 * @return 0 on success, -1 on error.
 */
static int do_request(int sock,
                      const uint8_t *req, uint16_t req_len,
                      uint8_t *resp, uint16_t *resp_len)
{
    if (isotp_send(sock, TESTER_ID, ECU_ID, req, req_len) != 0) {
        fprintf(stderr, "do_request: send failed\n");
        return -1;
    }
    int rc = isotp_recv(sock, ECU_ID, TESTER_ID,
                        resp, resp_len, 512u, 2000);
    if (rc != 0) {
        fprintf(stderr, "do_request: recv failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * AES-128-CMAC key computation
 * ------------------------------------------------------------------------- */

/**
 * Compute AES-128-CMAC(DEMO_SECRET, seed[seed_len]) into key_out[16].
 * Returns 0 on success, -1 on error.
 */
static int compute_key(const uint8_t *seed, uint16_t seed_len, uint8_t *key_out)
{
    const mbedtls_cipher_info_t *ci =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (ci == NULL) {
        fprintf(stderr, "compute_key: mbedtls_cipher_info_from_type failed\n");
        return -1;
    }
    int rc = mbedtls_cipher_cmac(ci, DEMO_SECRET, 128u, seed, seed_len, key_out);
    if (rc != 0) {
        fprintf(stderr, "compute_key: mbedtls_cipher_cmac error %d\n", rc);
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * SocketCAN FD socket setup
 * ------------------------------------------------------------------------- */

/**
 * Open and configure a CAN FD RAW socket on `iface`.
 * Returns the socket fd on success, -1 on error.
 */
static int open_can_socket(const char *iface)
{
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /* Enable CAN FD frames */
    int enable = 1;
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) != 0) {
        perror("setsockopt CAN_RAW_FD_FRAMES");
        close(sock);
        return -1;
    }

    /* Bind to the interface */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) != 0) {
        fprintf(stderr, "SIOCGIFINDEX for %s: %s\n", iface, strerror(errno));
        close(sock);
        return -1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

/* ---------------------------------------------------------------------------
 * Utility: print hex buffer
 * ------------------------------------------------------------------------- */
static void print_hex(const char *label, const uint8_t *data, uint16_t len)
{
    printf("%s:", label);
    for (uint16_t i = 0u; i < len; i++) {
        printf(" %02X", data[i]);
    }
    printf("\n");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage:\n"
                "  %s <can-iface> <image.bin>  — full OTA download + activate\n"
                "  %s <can-iface> --rollback   — tester-commanded rollback (0xFF03)\n",
                argv[0], argv[0]);
        return 1;
    }
    const char *iface    = argv[1];
    const char *img_path = argv[2];

    /* --rollback mode: open session, authenticate, send 0xFF03, done. */
    if (strcmp(img_path, "--rollback") == 0) {
        int sock = open_can_socket(iface);
        if (sock < 0) {
            return 1;
        }
        printf("[flash_tool] Rollback mode on %s\n", iface);

        uint8_t  resp[512];
        uint16_t resp_len;

        /* Step 1: DiagnosticSessionControl(programming) */
        printf("\n[1/4] DiagnosticSessionControl(programming)...\n");
        {
            uint8_t req[] = {0x10u, 0x02u};
            if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
                fprintf(stderr, "FAIL: no response to 0x10 02\n");
                close(sock);
                return 1;
            }
            print_hex("   resp", resp, resp_len);
            if (resp_len < 2u || resp[0] != 0x50u || resp[1] != 0x02u) {
                fprintf(stderr, "FAIL: expected 0x50 0x02\n");
                close(sock);
                return 1;
            }
            printf("   OK\n");
        }

        /* Step 2: SecurityAccess(requestSeed) */
        printf("\n[2/4] SecurityAccess(requestSeed)...\n");
        uint8_t seed[16];
        {
            uint8_t req[] = {0x27u, 0x01u};
            if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
                fprintf(stderr, "FAIL: no response to 0x27 01\n");
                close(sock);
                return 1;
            }
            print_hex("   resp", resp, resp_len);
            if (resp_len < 18u || resp[0] != 0x67u || resp[1] != 0x01u) {
                fprintf(stderr, "FAIL: expected 0x67 0x01 + 16 seed bytes\n");
                close(sock);
                return 1;
            }
            memcpy(seed, &resp[2], 16u);
            print_hex("   seed", seed, 16u);
            printf("   OK\n");
        }

        /* Step 3: SecurityAccess(sendKey) */
        printf("\n[3/4] SecurityAccess(sendKey)...\n");
        {
            uint8_t key[16];
            if (compute_key(seed, 16u, key) != 0) {
                fprintf(stderr, "FAIL: AES-CMAC key computation\n");
                close(sock);
                return 1;
            }
            print_hex("   key ", key, 16u);

            uint8_t req[18];
            req[0] = 0x27u;
            req[1] = 0x02u;
            memcpy(&req[2], key, 16u);
            if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
                fprintf(stderr, "FAIL: no response to 0x27 02\n");
                close(sock);
                return 1;
            }
            print_hex("   resp", resp, resp_len);
            if (resp_len < 2u || resp[0] != 0x67u || resp[1] != 0x02u) {
                fprintf(stderr, "FAIL: SecurityAccess rejected\n");
                close(sock);
                return 1;
            }
            printf("   OK — security unlocked\n");
        }

        /* Step 4: PerformRollback — 0x31 01 FF03 (triggers reset, no response) */
        printf("\n[4/4] PerformRollback (0x31 01 FF03) — ECU will reset...\n");
        {
            uint8_t req[] = {0x31u, 0x01u, 0xFFu, 0x03u};
            /* Best-effort send; ECU resets and may not respond */
            if (isotp_send(sock, TESTER_ID, ECU_ID, req, sizeof(req)) != 0) {
                fprintf(stderr, "WARN: send of 0x31 01 FF03 failed\n");
            }
            /* Wait briefly for optional response */
            uint16_t dummy_len;
            isotp_recv(sock, ECU_ID, TESTER_ID, resp, &dummy_len, sizeof(resp), 500);
            printf("   ECU rollback triggered\n");
        }

        printf("\n[flash_tool] Rollback complete. ECU will run the other bank after reset.\n");
        close(sock);
        return 0;
    }

    /* Load image file */
    FILE *f = fopen(img_path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Cannot open image file: %s\n", img_path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(f);
        return 1;
    }
    long img_size_l = ftell(f);
    if (img_size_l <= 0) {
        fprintf(stderr, "Image file is empty or ftell failed\n");
        fclose(f);
        return 1;
    }
    rewind(f);
    uint32_t img_size = (uint32_t)img_size_l;
    uint8_t *img_buf  = (uint8_t *)malloc(img_size);
    if (img_buf == NULL) {
        fprintf(stderr, "malloc(%u) failed\n", img_size);
        fclose(f);
        return 1;
    }
    if (fread(img_buf, 1, img_size, f) != img_size) {
        fprintf(stderr, "fread failed\n");
        fclose(f);
        free(img_buf);
        return 1;
    }
    fclose(f);
    printf("[flash_tool] Image: %s (%u bytes)\n", img_path, img_size);

    /* Open SocketCAN */
    int sock = open_can_socket(iface);
    if (sock < 0) {
        free(img_buf);
        return 1;
    }
    printf("[flash_tool] CAN FD socket on %s ready\n", iface);

    uint8_t  resp[512];
    uint16_t resp_len;

    /* ------------------------------------------------------------------
     * Step 1: DiagnosticSessionControl(programming) — 0x10 02
     * ------------------------------------------------------------------ */
    printf("\n[1/9] DiagnosticSessionControl(programming)...\n");
    {
        uint8_t req[] = {0x10u, 0x02u};
        if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x10 02\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        if (resp_len < 2u || resp[0] != 0x50u || resp[1] != 0x02u) {
            fprintf(stderr, "FAIL: expected 0x50 0x02, got 0x%02X 0x%02X\n",
                    resp[0], resp_len > 1u ? resp[1] : 0u);
            goto err;
        }
        printf("   OK\n");
    }

    /* ------------------------------------------------------------------
     * Step 2: ReadDataByIdentifier(0xF1A0) — active bank
     * ------------------------------------------------------------------ */
    printf("\n[2/9] ReadDataByIdentifier(0xF1A0) — active bank...\n");
    uint8_t  active_bank;
    uint32_t inactive_app_base;
    {
        uint8_t req[] = {0x22u, 0xF1u, 0xA0u};
        if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x22 F1A0\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        /* Expected: 0x62 0xF1 0xA0 <bank> */
        if (resp_len < 4u || resp[0] != 0x62u || resp[1] != 0xF1u || resp[2] != 0xA0u) {
            fprintf(stderr, "FAIL: unexpected RDBI response\n");
            goto err;
        }
        active_bank = resp[3];
        uint8_t inactive_bank = (uint8_t)(active_bank ^ 0x01u);
        inactive_app_base = BANK_BASE_OFFSET +
                            (uint32_t)inactive_bank * BANK_SIZE +
                            BL_REGION_SIZE;
        printf("   active bank=%u  inactive bank=%u  target base=0x%08X\n",
               active_bank, inactive_bank, inactive_app_base);
        printf("   OK\n");
    }

    /* ------------------------------------------------------------------
     * Step 3: SecurityAccess(requestSeed) — 0x27 01
     * ------------------------------------------------------------------ */
    printf("\n[3/9] SecurityAccess(requestSeed)...\n");
    uint8_t seed[16];
    {
        uint8_t req[] = {0x27u, 0x01u};
        if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x27 01\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        /* Expected: 0x67 0x01 <16 seed bytes> */
        if (resp_len < 18u || resp[0] != 0x67u || resp[1] != 0x01u) {
            fprintf(stderr, "FAIL: expected 0x67 0x01 + 16 seed bytes (got %u bytes)\n",
                    resp_len);
            goto err;
        }
        memcpy(seed, &resp[2], 16u);
        print_hex("   seed", seed, 16u);
        printf("   OK\n");
    }

    /* ------------------------------------------------------------------
     * Step 4: SecurityAccess(sendKey) — 0x27 02 + AES-128-CMAC
     * ------------------------------------------------------------------ */
    printf("\n[4/9] SecurityAccess(sendKey)...\n");
    {
        uint8_t key[16];
        if (compute_key(seed, 16u, key) != 0) {
            fprintf(stderr, "FAIL: AES-CMAC key computation\n");
            goto err;
        }
        print_hex("   key ", key, 16u);

        uint8_t req[18];
        req[0] = 0x27u;
        req[1] = 0x02u;
        memcpy(&req[2], key, 16u);

        if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x27 02\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        if (resp_len < 2u || resp[0] != 0x67u || resp[1] != 0x02u) {
            fprintf(stderr, "FAIL: SecurityAccess rejected (0x%02X 0x%02X)\n",
                    resp[0], resp_len > 1u ? resp[1] : 0u);
            goto err;
        }
        printf("   OK — security unlocked\n");
    }

    /* ------------------------------------------------------------------
     * Step 5: RequestDownload — 0x34
     * ALFID=0x44: 4-byte addr, 4-byte size
     * ------------------------------------------------------------------ */
    printf("\n[5/9] RequestDownload (addr=0x%08X size=%u)...\n",
           inactive_app_base, img_size);
    uint32_t max_block_len = 4095u; /* default if parse fails */
    {
        /* ALFID=0x44: 4-byte address + 4-byte memory size (ISO 14229-1 §14.4.2) */
        uint8_t req11[11];
        req11[0] = 0x34u;
        req11[1] = 0x00u;       /* dataFormatIdentifier */
        req11[2] = 0x44u;       /* addressAndLengthFormatIdentifier: 4+4 */
        /* Address (big-endian) */
        req11[3] = (uint8_t)(inactive_app_base >> 24u);
        req11[4] = (uint8_t)(inactive_app_base >> 16u);
        req11[5] = (uint8_t)(inactive_app_base >>  8u);
        req11[6] = (uint8_t)(inactive_app_base        );
        /* Size (big-endian) */
        req11[7] = (uint8_t)(img_size >> 24u);
        req11[8] = (uint8_t)(img_size >> 16u);
        req11[9] = (uint8_t)(img_size >>  8u);
        req11[10]= (uint8_t)(img_size        );

        if (do_request(sock, req11, sizeof(req11), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x34\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        if (resp_len < 2u || resp[0] != 0x74u) {
            fprintf(stderr, "FAIL: RequestDownload rejected\n");
            goto err;
        }

        /*
         * Parse maxNumberOfBlockLength from the 0x74 response.
         * Per ISO 14229-1 §14.4.3:
         *   resp[1] = lengthFormatIdentifier (high nibble = number of block-length bytes)
         *   resp[2..] = maxNumberOfBlockLength (MSB-first)
         */
        uint8_t  len_bytes = (uint8_t)((resp[1] >> 4u) & 0x0Fu);
        if (len_bytes > 0u && resp_len >= (uint16_t)(2u + len_bytes)) {
            uint32_t mbl = 0u;
            for (uint8_t i = 0u; i < len_bytes; i++) {
                mbl = (mbl << 8u) | resp[2u + i];
            }
            /*
             * Only accept mbl if it leaves room for at least one data byte
             * after SID (1 byte) + blockSequenceCounter (1 byte) = 2-byte overhead.
             * mbl <= 1 would cause chunk_data_size = mbl - 2 to underflow.
             */
            if (mbl > 1u) {
                max_block_len = mbl;
            }
        }
        printf("   maxNumberOfBlockLength=%u\n", max_block_len);
        printf("   OK\n");
    }

    /* ------------------------------------------------------------------
     * Step 6: TransferData — 0x36 × N
     * chunk size = max_block_len - 2 (SID + seq)
     * ------------------------------------------------------------------ */
    printf("\n[6/9] TransferData (%u bytes in chunks of %u)...\n",
           img_size, max_block_len - 2u);
    {
        /* max_block_len is guaranteed > 1 (default 4095 or validated from response) */
        uint32_t chunk_data_size = max_block_len - 2u; /* subtract SID + blockSequenceCounter */
        uint8_t *req_buf = (uint8_t *)malloc(max_block_len);
        if (req_buf == NULL) {
            fprintf(stderr, "FAIL: malloc chunk buffer\n");
            goto err;
        }

        uint32_t offset = 0u;
        uint8_t  seq    = 0x01u;

        while (offset < img_size) {
            uint32_t remain = img_size - offset;
            uint32_t chunk  = (remain < chunk_data_size) ? remain : chunk_data_size;

            req_buf[0] = 0x36u;
            req_buf[1] = seq;
            memcpy(&req_buf[2], img_buf + offset, chunk);

            uint16_t req_total = (uint16_t)(2u + chunk);
            if (do_request(sock, req_buf, req_total, resp, &resp_len) != 0) {
                fprintf(stderr, "FAIL: TransferData no response (offset=%u)\n", offset);
                free(req_buf);
                goto err;
            }
            if (resp_len < 2u || resp[0] != 0x76u || resp[1] != seq) {
                fprintf(stderr, "FAIL: TransferData NACK (resp[0]=0x%02X resp[1]=0x%02X seq=0x%02X)\n",
                        resp[0], resp_len > 1u ? resp[1] : 0u, seq);
                free(req_buf);
                goto err;
            }

            offset += chunk;
            printf("   TX block seq=0x%02X offset=%u/%u\r", seq, offset, img_size);
            fflush(stdout);

            /* Wrap sequence counter: 0x01..0xFF, then back to 0x01 */
            seq = (uint8_t)(seq == 0xFFu ? 0x01u : seq + 1u);
        }
        free(req_buf);
        printf("\n   OK (%u blocks)\n", offset);
    }

    /* ------------------------------------------------------------------
     * Step 7: RequestTransferExit — 0x37
     * ------------------------------------------------------------------ */
    printf("\n[7/9] RequestTransferExit...\n");
    {
        uint8_t req[] = {0x37u};
        if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x37\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        if (resp_len < 1u || resp[0] != 0x77u) {
            fprintf(stderr, "FAIL: RequestTransferExit NACK\n");
            goto err;
        }
        printf("   OK\n");
    }

    /* ------------------------------------------------------------------
     * Step 8: CheckProgrammingDependencies — 0x31 01 FF01
     * ------------------------------------------------------------------ */
    printf("\n[8/9] CheckProgrammingDependencies (0x31 01 FF01)...\n");
    {
        uint8_t req[] = {0x31u, 0x01u, 0xFFu, 0x01u};
        if (do_request(sock, req, sizeof(req), resp, &resp_len) != 0) {
            fprintf(stderr, "FAIL: no response to 0x31 01 FF01\n");
            goto err;
        }
        print_hex("   resp", resp, resp_len);
        /* Expected: 0x71 0x01 0xFF 0x01 <result> */
        if (resp_len < 5u || resp[0] != 0x71u || resp[1] != 0x01u ||
                resp[2] != 0xFFu || resp[3] != 0x01u) {
            fprintf(stderr, "FAIL: unexpected CheckProgramming response\n");
            goto err;
        }
        if (resp[4] != 0x01u) {
            fprintf(stderr, "FAIL: image validation FAILED (result=0x%02X)\n", resp[4]);
            goto err;
        }
        printf("   PASS\n");
    }

    /* ------------------------------------------------------------------
     * Step 9: ActivateSoftware — 0x31 01 FF02 (triggers reset, no response)
     * ------------------------------------------------------------------ */
    printf("\n[9/9] ActivateSoftware (0x31 01 FF02) — ECU will reset...\n");
    {
        uint8_t req[] = {0x31u, 0x01u, 0xFFu, 0x02u};
        /* Best-effort send; ECU resets and may not respond */
        if (isotp_send(sock, TESTER_ID, ECU_ID, req, sizeof(req)) != 0) {
            fprintf(stderr, "WARN: send of 0x31 01 FF02 failed\n");
        }
        /* Wait briefly for optional response */
        uint16_t dummy_len;
        isotp_recv(sock, ECU_ID, TESTER_ID, resp, &dummy_len, sizeof(resp), 500);
        printf("   ECU reset triggered\n");
    }

    printf("\n[flash_tool] Flash complete. New image will run after ECU reset.\n");
    close(sock);
    free(img_buf);
    return 0;

err:
    close(sock);
    free(img_buf);
    return 1;
}

