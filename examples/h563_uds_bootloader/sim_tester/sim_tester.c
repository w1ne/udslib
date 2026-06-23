/*
 * sim_tester.c — baked-in OTA smoke tester for SIM_OTA_TESTER builds.
 *
 * Drives the full OTA sequence over FDCAN internal loopback:
 *   SESSION → SECURITY_ACCESS → ERASE → REQUEST_DOWNLOAD →
 *   TRANSFER_DATA(n) → TRANSFER_EXIT → CHECK → ACTIVATE
 *
 * Communicates over FDCAN in CAN-FD mode (matching the bootloader's
 * uds_tp_isotp_set_fd(true) configuration).  Tester TX ID = 0x7E0,
 * server TX ID = 0x7E8.
 *
 * Milestones printed on UART:
 *   OTA-WRITE-OK   — after positive 0x37 TransferExit response
 *   OTA-VERIFY-OK  — after positive 0x31/0xFF01 CheckProgramming response
 *   OTA-ACTIVATE   — just before 0x31/0xFF02 ActivateSoftware is sent
 */

#include "sim_tester.h"
#include "app_b_image_blob.h"
#include "../bootloader/fdcan.h"
#include "../bootloader/flash_h5.h"
#include "../bootloader/sec_cmac.h"

#include <string.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define TESTER_TX_ID 0x7E0u /* tester → server */
#define SERVER_TX_ID 0x7E8u /* server → tester */

#define BANK_SIZE 0x100000UL
#define BL_REGION_SIZE 0x18000UL
#define SECTOR_SIZE 0x2000UL
#define BL_COPY_SECTORS 12u /* sectors 0-11 = bootloader region */

/* Target address for App-B: inactive bank app base.
 * With flash_active_bank()=0 (OPTSR_CUR never updated), inactive=1,
 * inactive_app_base = 0x08000000 + 1*0x100000 + 0x18000 = 0x08118000. */
#define INACTIVE_APP_BASE 0x08118000UL
#define INACTIVE_BL_BASE 0x08100000UL /* Bank2 start for bootloader copy */

/* ---------------------------------------------------------------------------
 * ISO-TP / CAN-FD helpers
 * ------------------------------------------------------------------------- */

/* Send a PDU via CAN-FD ISO-TP SF (payload ≤ 62 bytes) or multi-frame.
 * For this tester every outgoing PDU fits in one CAN-FD SF (≤ 62 bytes). */
static void tester_send_sf(const uint8_t *pdu, uint8_t pdu_len)
{
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));

    if (pdu_len <= 7u) {
        /* Classic ISO-TP SF: [N|len][data...] */
        frame[0] = pdu_len;
        memcpy(&frame[1], pdu, pdu_len);
        fdcan_send_frame(TESTER_TX_ID, frame, (uint8_t) (1u + pdu_len), pdu_len > 8u);
    }
    else {
        /* CAN-FD ISO-TP SF: [0x00][len8][data...] */
        frame[0] = 0x00u;
        frame[1] = pdu_len;
        memcpy(&frame[2], pdu, pdu_len);
        fdcan_send_frame(TESTER_TX_ID, frame, (uint8_t) (2u + pdu_len), true);
    }
}

/* ---------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */
typedef enum
{
    ST_COPY_BL = 0, /* copy bootloader sectors 0-11 to Bank2 */
    ST_SESSION,     /* send 10 02 */
    ST_SESSION_WAIT,
    ST_DTC_OFF, /* send 85 02 — disable DTC setting before flashing */
    ST_DTC_OFF_WAIT,
    ST_COMM_OFF, /* send 28 03 01 — disable normal application messages */
    ST_COMM_OFF_WAIT,
    ST_SEED_REQ, /* send 27 01 */
    ST_SEED_WAIT,
    ST_KEY_SEND, /* compute CMAC and send 27 02 <key> */
    ST_KEY_WAIT,
    ST_ERASE, /* send 31 01 FF 00 */
    ST_ERASE_WAIT,
    ST_RD, /* send 34 ... */
    ST_RD_WAIT,
    ST_TD, /* send 36 block chunks */
    ST_TD_WAIT,
    ST_TE, /* send 37 */
    ST_TE_WAIT,
    ST_CHECK, /* send 31 01 FF 01 — also print OTA-WRITE-OK */
    ST_CHECK_WAIT,
    ST_ACTIVATE, /* print OTA-ACTIVATE and send 31 01 FF 02 */
    ST_DONE,     /* OTA complete; server resets */
} tester_state_t;

#define SEC_SECRET_LEN 16u
#define SEC_SEED_LEN 16u
#define SEC_KEY_LEN 16u

/* Tester context */
static struct
{
    tester_state_t state;
    uint8_t secret[SEC_SECRET_LEN]; /* shared CMAC secret (from sim_tester_init) */
    uint8_t seed_rx[16];            /* seed received from the server's 0x67 01 response */
    uint8_t seed_len;               /* number of seed bytes actually received */
    uint32_t td_offset;             /* bytes of APP_B_IMAGE already sent */
    uint8_t td_seq;                 /* next TD sequence number (1-based) */
    /* ISO-TP RX reassembler for server responses */
    uint8_t rx_sdu[128];
    uint8_t rx_len;
    bool rx_ready;         /* true when a complete SDU is available */
    uint32_t wait_counter; /* simple delay between sends */
} g_t;

/* ---------------------------------------------------------------------------
 * ISO-TP receive reassembler (tester side)
 * Server uses CAN-FD, so responses arrive as CAN-FD SF or multi-frame.
 * All server SDUs in this protocol fit in a single CAN-FD SF (≤ 62 bytes).
 * ------------------------------------------------------------------------- */
void sim_tester_rx(const uint8_t *data, uint8_t len)
{
    if (len == 0u) {
        return;
    }
    uint8_t pci_nibble = (data[0] >> 4u) & 0x0Fu;

    if (pci_nibble == 0x0u) {
        /* Single Frame */
        uint8_t sdu_len;
        uint8_t offset;
        if (data[0] == 0x00u && len >= 2u) {
            /* CAN-FD extended SF: [0x00][len][data...] */
            sdu_len = data[1];
            offset = 2u;
        }
        else {
            sdu_len = data[0] & 0x0Fu;
            offset = 1u;
        }
        if (sdu_len == 0u || sdu_len > (uint8_t) (len - offset) || sdu_len > sizeof(g_t.rx_sdu)) {
            return;
        }
        memcpy(g_t.rx_sdu, &data[offset], sdu_len);
        g_t.rx_len = sdu_len;
        g_t.rx_ready = true;
    }
    /* Multi-frame handling omitted: server SDUs always fit in one CAN-FD SF. */
}

/* ---------------------------------------------------------------------------
 * sim_tester_init — called once after uds_init()
 * ------------------------------------------------------------------------- */
void sim_tester_init(const uint8_t *secret, const uint8_t *seed)
{
    /* Store the shared CMAC secret; the SecurityAccess key is computed live in
     * ST_KEY_SEND as AES-128-CMAC(secret, seed-from-server).  The seed argument
     * is intentionally unused: the tester reads the seed from the server's
     * 0x67 01 response rather than assuming a fixed value, which is what a real
     * tester must do once the server returns a TRNG-derived per-attempt nonce. */
    (void) seed;
    memcpy(g_t.secret, secret, SEC_SECRET_LEN);
    g_t.state = ST_COPY_BL;
}

/* ---------------------------------------------------------------------------
 * sim_tester_poll — called every iteration of the main polling loop
 * ------------------------------------------------------------------------- */
void sim_tester_poll(void)
{
    /* Throttle: skip every other poll tick to give the server time to process.
     * The server runs uds_process() once per outer loop iteration, so we must
     * not send a new request on the same tick we expect a response. */
    ++g_t.wait_counter;
    if ((g_t.wait_counter & 1u) == 0u) {
        return;
    }

    uint8_t buf[64];

    switch (g_t.state) {
        case ST_COPY_BL: {
            /*
             * Copy bootloader sectors 0-11 (96 KB) from Bank1 to Bank2 so that
             * after the physical swap the CPU can boot from 0x08000000 (which will
             * contain Bank2's copy of the bootloader).
             *
             * Bank2 was erased during chip initialisation, so the sectors are
             * already in the blank (0xFF) state and we can program directly.
             */
            uart_puts("SIM: copy BL to Bank2\n");
            flash_unlock();
            const uint8_t *src = (const uint8_t *) (uintptr_t) 0x08000000UL;
            uint32_t copy_size = (uint32_t) BL_COPY_SECTORS * (uint32_t) SECTOR_SIZE; /* 96 KB */
            int rc = flash_program(INACTIVE_BL_BASE, src, copy_size);
            if (rc != 0) {
                /* Flash copy failed — halt; smoke will time out */
                uart_puts("SIM: copy FAIL\n");
                for (;;) {
                }
            }
            uart_puts("SIM: copy OK\n");
            g_t.state = ST_SESSION;
            break;
        }

        case ST_SESSION: {
            uart_puts("SIM: send session\n");
            buf[0] = 0x10u;
            buf[1] = 0x02u; /* DiagnosticSessionControl, programmingSession */
            tester_send_sf(buf, 2u);
            g_t.rx_ready = false;
            g_t.state = ST_SESSION_WAIT;
            break;
        }

        case ST_SESSION_WAIT:
            if (g_t.rx_ready) {
                if (g_t.rx_len >= 2u && g_t.rx_sdu[0] == 0x50u && g_t.rx_sdu[1] == 0x02u) {
                    g_t.rx_ready = false;
                    g_t.state = ST_DTC_OFF;
                }
                else {
                    /* NRC or unexpected — retry */
                    g_t.rx_ready = false;
                    g_t.state = ST_SESSION;
                }
            }
            break;

        case ST_DTC_OFF: {
            /* ControlDTCSetting OFF (0x85 0x02): freeze DTC storage before the
             * erase/flash provokes faults. Allowed in the programming session. */
            uart_puts("SIM: disable DTC\n");
            buf[0] = 0x85u;
            buf[1] = 0x02u;
            tester_send_sf(buf, 2u);
            g_t.rx_ready = false;
            g_t.state = ST_DTC_OFF_WAIT;
            break;
        }

        case ST_DTC_OFF_WAIT:
            if (g_t.rx_ready) {
                /* Positive response: C5 02 */
                if (g_t.rx_len >= 2u && g_t.rx_sdu[0] == 0xC5u && g_t.rx_sdu[1] == 0x02u) {
                    g_t.rx_ready = false;
                    g_t.state = ST_COMM_OFF;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    g_t.rx_ready = false;
                    g_t.state = ST_DTC_OFF;
                }
            }
            break;

        case ST_COMM_OFF: {
            /* CommunicationControl (0x28 0x03 0x01): disable Rx and Tx of normal
             * application messages so the ECU is quiet while it is flashed. */
            uart_puts("SIM: disable app comms\n");
            buf[0] = 0x28u;
            buf[1] = 0x03u; /* disableRxAndTx */
            buf[2] = 0x01u; /* normalCommunicationMessages (application) */
            tester_send_sf(buf, 3u);
            g_t.rx_ready = false;
            g_t.state = ST_COMM_OFF_WAIT;
            break;
        }

        case ST_COMM_OFF_WAIT:
            if (g_t.rx_ready) {
                /* Positive response: 68 03 */
                if (g_t.rx_len >= 2u && g_t.rx_sdu[0] == 0x68u && g_t.rx_sdu[1] == 0x03u) {
                    g_t.rx_ready = false;
                    g_t.state = ST_SEED_REQ;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    g_t.rx_ready = false;
                    g_t.state = ST_COMM_OFF;
                }
            }
            break;

        case ST_SEED_REQ: {
            buf[0] = 0x27u;
            buf[1] = 0x01u; /* SecurityAccess, requestSeed */
            tester_send_sf(buf, 2u);
            g_t.rx_ready = false;
            g_t.state = ST_SEED_WAIT;
            break;
        }

        case ST_SEED_WAIT:
            if (g_t.rx_ready) {
                if (g_t.rx_len >= 3u && g_t.rx_sdu[0] == 0x67u && g_t.rx_sdu[1] == 0x01u) {
                    /* Positive response: copy the seed bytes the SERVER chose. */
                    uint8_t seed_len = (uint8_t) (g_t.rx_len - 2u);
                    if (seed_len > sizeof(g_t.seed_rx)) {
                        seed_len = (uint8_t) sizeof(g_t.seed_rx);
                    }
                    memcpy(g_t.seed_rx, &g_t.rx_sdu[2], seed_len);
                    g_t.seed_len = seed_len;
                    g_t.rx_ready = false;
                    g_t.state = ST_KEY_SEND;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    g_t.rx_ready = false;
                    g_t.state = ST_SEED_REQ;
                }
            }
            break;

        case ST_KEY_SEND: {
            /* Compute the SecurityAccess key LIVE: AES-128-CMAC(secret, seed) over
             * the seed the server returned, using the same one-shot the bootloader
             * verifies with.  No baked-in key. */
            uint8_t key[SEC_KEY_LEN];
            if (aes_cmac(g_t.secret, g_t.seed_rx, g_t.seed_len, key) != 0) {
                uart_puts("SIM: CMAC FAIL\n");
                for (;;) {
                }
            }
            buf[0] = 0x27u;
            buf[1] = 0x02u; /* SecurityAccess, sendKey */
            memcpy(&buf[2], key, SEC_KEY_LEN);
            tester_send_sf(buf, (uint8_t) (2u + SEC_KEY_LEN));
            g_t.rx_ready = false;
            g_t.state = ST_KEY_WAIT;
            break;
        }

        case ST_KEY_WAIT:
            if (g_t.rx_ready) {
                if (g_t.rx_len >= 2u && g_t.rx_sdu[0] == 0x67u && g_t.rx_sdu[1] == 0x02u) {
                    g_t.rx_ready = false;
                    g_t.state = ST_ERASE;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    g_t.rx_ready = false;
                    g_t.state = ST_SEED_REQ; /* re-authenticate */
                }
            }
            break;

        case ST_ERASE: {
            /* RoutineControl StartRoutine 0xFF00 (EraseMemory) */
            buf[0] = 0x31u;
            buf[1] = 0x01u;
            buf[2] = 0xFFu;
            buf[3] = 0x00u;
            tester_send_sf(buf, 4u);
            g_t.rx_ready = false;
            g_t.state = ST_ERASE_WAIT;
            break;
        }

        case ST_ERASE_WAIT:
            if (g_t.rx_ready) {
                /* 71 01 FF 00 [optional_status_byte] */
                if (g_t.rx_len >= 4u && g_t.rx_sdu[0] == 0x71u && g_t.rx_sdu[1] == 0x01u &&
                    g_t.rx_sdu[2] == 0xFFu && g_t.rx_sdu[3] == 0x00u) {
                    g_t.rx_ready = false;
                    g_t.state = ST_RD;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    g_t.rx_ready = false;
                    g_t.state = ST_ERASE;
                }
            }
            break;

        case ST_RD: {
            /* RequestDownload: addr=INACTIVE_APP_BASE, size=APP_B_IMAGE_LEN */
            /* ALFID = 0x44: 4-byte address, 4-byte length */
            buf[0] = 0x34u; /* SID */
            buf[1] = 0x00u; /* dataFormatIdentifier */
            buf[2] = 0x44u; /* ALFID: 4-byte addr, 4-byte length */
            buf[3] = (uint8_t) (INACTIVE_APP_BASE >> 24u);
            buf[4] = (uint8_t) (INACTIVE_APP_BASE >> 16u);
            buf[5] = (uint8_t) (INACTIVE_APP_BASE >> 8u);
            buf[6] = (uint8_t) (INACTIVE_APP_BASE >> 0u);
            buf[7] = (uint8_t) (APP_B_IMAGE_LEN >> 24u);
            buf[8] = (uint8_t) (APP_B_IMAGE_LEN >> 16u);
            buf[9] = (uint8_t) (APP_B_IMAGE_LEN >> 8u);
            buf[10] = (uint8_t) (APP_B_IMAGE_LEN >> 0u);
            tester_send_sf(buf, 11u);
            g_t.rx_ready = false;
            g_t.td_offset = 0u;
            g_t.td_seq = 1u;
            g_t.state = ST_RD_WAIT;
            break;
        }

        case ST_RD_WAIT:
            if (g_t.rx_ready) {
                if (g_t.rx_len >= 2u && g_t.rx_sdu[0] == 0x74u) {
                    /* Positive RD response — max block length in [1..] ignored */
                    g_t.rx_ready = false;
                    g_t.state = ST_TD;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    g_t.rx_ready = false;
                    g_t.state = ST_RD;
                }
            }
            break;

        case ST_TD: {
            /* TransferData: send 32 bytes of APP_B_IMAGE per block */
            if (g_t.td_offset >= APP_B_IMAGE_LEN) {
                /* All bytes sent — move to TransferExit */
                g_t.state = ST_TE;
                break;
            }
            uint32_t remaining = APP_B_IMAGE_LEN - g_t.td_offset;
            uint8_t chunk = (uint8_t) ((remaining > 32u) ? 32u : (uint8_t) remaining);
            buf[0] = 0x36u;      /* TransferData SID */
            buf[1] = g_t.td_seq; /* Block sequence counter */
            memcpy(&buf[2], &APP_B_IMAGE[g_t.td_offset], chunk);
            tester_send_sf(buf, (uint8_t) (2u + chunk));
            g_t.td_offset += chunk;
            g_t.td_seq = (uint8_t) (g_t.td_seq == 0xFFu ? 0x00u : g_t.td_seq + 1u);
            g_t.rx_ready = false;
            g_t.state = ST_TD_WAIT;
            break;
        }

        case ST_TD_WAIT:
            if (g_t.rx_ready) {
                if (g_t.rx_len >= 2u && g_t.rx_sdu[0] == 0x76u) {
                    /* Positive TD response */
                    g_t.rx_ready = false;
                    g_t.state = ST_TD;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    /* NRC — abort (halt for now; smoke will time out) */
                    for (;;) {
                    }
                }
            }
            break;

        case ST_TE: {
            buf[0] = 0x37u; /* RequestTransferExit */
            tester_send_sf(buf, 1u);
            g_t.rx_ready = false;
            g_t.state = ST_TE_WAIT;
            break;
        }

        case ST_TE_WAIT:
            if (g_t.rx_ready) {
                if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x77u) {
                    uart_puts("OTA-WRITE-OK\n");
                    g_t.rx_ready = false;
                    g_t.state = ST_CHECK;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    for (;;) {
                    }
                }
            }
            break;

        case ST_CHECK: {
            /* RoutineControl StartRoutine 0xFF01 (CheckProgramming) */
            buf[0] = 0x31u;
            buf[1] = 0x01u;
            buf[2] = 0xFFu;
            buf[3] = 0x01u;
            tester_send_sf(buf, 4u);
            g_t.rx_ready = false;
            g_t.state = ST_CHECK_WAIT;
            break;
        }

        case ST_CHECK_WAIT:
            if (g_t.rx_ready) {
                /* 71 01 FF 01 [0x01=PASS] */
                if (g_t.rx_len >= 5u && g_t.rx_sdu[0] == 0x71u && g_t.rx_sdu[1] == 0x01u &&
                    g_t.rx_sdu[2] == 0xFFu && g_t.rx_sdu[3] == 0x01u && g_t.rx_sdu[4] == 0x01u) {
                    uart_puts("OTA-VERIFY-OK\n");
                    g_t.rx_ready = false;
                    g_t.state = ST_ACTIVATE;
                }
                else if (g_t.rx_len >= 1u && g_t.rx_sdu[0] == 0x7Fu) {
                    for (;;) {
                    } /* verification failed — halt */
                }
            }
            break;

        case ST_ACTIVATE: {
            uart_puts("OTA-ACTIVATE\n");
            /* RoutineControl StartRoutine 0xFF02 (ActivateSoftware)
             * This will trigger swap+reset; the server does not return a response. */
            buf[0] = 0x31u;
            buf[1] = 0x01u;
            buf[2] = 0xFFu;
            buf[3] = 0x02u;
            tester_send_sf(buf, 4u);
            g_t.state = ST_DONE;
            break;
        }

        case ST_DONE:
            /* Server has reset. Tester is idle — just spin. */
            break;

        default:
            break;
    }
}
