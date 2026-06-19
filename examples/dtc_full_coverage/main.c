/*
 * Copyright (c) 2026 Andrii Shylenko
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

/**
 * @file main.c
 * @brief Application-served ReadDTCInformation (0x19) sub-functions demo.
 *
 * The library frames the common 0x19 sub-functions itself (0x01/0x02/0x0A,
 * 0x04/0x06, 0x07/0x08/0x09, 0x0B-0x0E, 0x14, 0x15, 0x42/0x55) from records
 * the application supplies via fn_dtc_list. The remaining standard
 * sub-functions need a memory region, a record number, an emissions subset, or
 * a memory selection that does not generalise into a single hook, so the
 * library routes them to the raw fn_dtc_read fallback. This example shows an
 * application implementing those layers itself:
 *
 *   0x03 reportDTCSnapshotIdentification
 *   0x05 reportDTCStoredDataByRecordNumber
 *   0x0F reportMirrorMemoryDTCByStatusMask
 *   0x10 reportMirrorMemoryDTCExtDataRecordByDTCNumber
 *   0x11 reportNumberOfMirrorMemoryDTCByStatusMask
 *   0x12 reportNumberOfEmissionsOBDDTCByStatusMask
 *   0x13 reportEmissionsOBDDTCByStatusMask
 *   0x16 reportDTCExtDataRecordByRecordNumber
 *   0x17 reportUserDefMemoryDTCByStatusMask
 *   0x18 reportUserDefMemoryDTCSnapshotRecordByDTCNumber
 *   0x19 reportUserDefMemoryDTCExtDataRecordByDTCNumber
 *
 * The library writes [0x59, sub-function] and the application writes the
 * payload that follows. fn_dtc_read receives the full request (req[0]=0x19,
 * req[1]=sub, req[2..]=parameters), so the handler can read the status mask,
 * DTC, record number, or memory selection it needs. Everything here uses
 * fixed static arrays (no malloc/free), consistent with the library's
 * zero-allocation, MISRA-friendly design.
 *
 * Returns 0 when every demonstrated request produced a positive response.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uds/uds_core.h"
#include "uds/uds_dtc.h"

/* --- ECU glue (loopback transport) --- */
static uint8_t g_resp[256];
static uint16_t g_resp_len;
static uint32_t g_time;

static uint32_t ecu_time(void)
{
    return g_time;
}

static int ecu_send(uds_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    (void) ctx;
    if (len > sizeof(g_resp)) {
        len = (uint16_t) sizeof(g_resp);
    }
    memcpy(g_resp, data, len);
    g_resp_len = len;
    return 0;
}

/* --- Application DTC data (static; no dynamic allocation) --- */

/* Primary-memory DTCs the library frames via fn_dtc_list. */
static const uds_dtc_record_t g_primary[] = {
    {0x012345u, UDS_DTC_STATUS_TEST_FAILED | UDS_DTC_STATUS_CONFIRMED, 0, 0, 0, 0,
     UDS_DTC_FGID_EMISSIONS},
    {0xC00100u, UDS_DTC_STATUS_CONFIRMED, 0, 0, 0, 0, 0},
};

/* Mirror-memory DTCs (a separate region the application owns). */
typedef struct
{
    uint32_t dtc;
    uint8_t status;
} mem_dtc_t;

static const mem_dtc_t g_mirror[] = {
    {0x101010u, UDS_DTC_STATUS_TEST_FAILED | UDS_DTC_STATUS_CONFIRMED},
    {0x202020u, UDS_DTC_STATUS_CONFIRMED},
};

/* Emissions-related (OBD) DTC subset. */
static const mem_dtc_t g_emissions[] = {
    {0x012345u, UDS_DTC_STATUS_TEST_FAILED | UDS_DTC_STATUS_CONFIRMED},
};

#define APP_STATUS_AVAIL_MASK 0x7Fu
#define APP_DTC_FORMAT_ID 0x01u /* ISO_14229-1 */

/* fn_dtc_list: serves the library-framed sub-functions from the primary set. */
static int app_dtc_list(struct uds_ctx *ctx, uint8_t status_mask, uds_dtc_record_t *out,
                        uint16_t max)
{
    (void) ctx;
    uint16_t n = 0u;
    uint16_t count = (uint16_t) (sizeof(g_primary) / sizeof(g_primary[0]));
    for (uint16_t i = 0u; i < count; i++) {
        bool match = (status_mask == 0u) || ((g_primary[i].status & status_mask) != 0u);
        if (match) {
            if ((out != NULL) && (n < max)) {
                out[n] = g_primary[i];
            }
            n++;
        }
    }
    return (int) n;
}

/* Helper: append a {DTC(3) status(1)} record. Returns the new write position. */
static uint16_t put_dtc_status(uint8_t *out, uint16_t pos, uint32_t dtc, uint8_t status)
{
    out[pos] = (uint8_t) ((dtc >> 16) & 0xFFu);
    out[pos + 1u] = (uint8_t) ((dtc >> 8) & 0xFFu);
    out[pos + 2u] = (uint8_t) (dtc & 0xFFu);
    out[pos + 3u] = status;
    return (uint16_t) (pos + 4u);
}

/*
 * fn_dtc_read: serves the sub-functions the library does not frame. The
 * library has already written [0x59, sub]; this writes the payload after it
 * and returns the payload length (or a negative NRC). req is the full request.
 */
static int app_dtc_read(struct uds_ctx *ctx, uint8_t subfn, const uint8_t *req, uint16_t req_len,
                        uint8_t *out_buf, uint16_t max_len)
{
    (void) ctx;
    (void) max_len;
    uint16_t pos = 0u;

    switch (subfn) {
        case 0x03u: { /* reportDTCSnapshotIdentification: {DTC(3) snapshotRecordNumber(1)}... */
            pos = put_dtc_status(out_buf, pos, 0x012345u, 0x01u); /* DTC + record number 0x01 */
            return (int) pos;
        }

        case 0x05u: { /* reportDTCStoredDataByRecordNumber: recordNumber + stored data */
            if (req_len < 3u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t record_number = req[2];
            if (record_number != 0x01u) {
                return -0x31; /* requestOutOfRange */
            }
            out_buf[pos++] = record_number;
            pos = put_dtc_status(out_buf, pos, 0x012345u, g_primary[0].status);
            return (int) pos;
        }

        case 0x0Fu: { /* reportMirrorMemoryDTCByStatusMask: statusAvail + {DTC(3) status}... */
            if (req_len < 3u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t status_mask = req[2];
            out_buf[pos++] = APP_STATUS_AVAIL_MASK;
            for (uint16_t i = 0u; i < (sizeof(g_mirror) / sizeof(g_mirror[0])); i++) {
                if ((g_mirror[i].status & status_mask) != 0u) {
                    pos = put_dtc_status(out_buf, pos, g_mirror[i].dtc, g_mirror[i].status);
                }
            }
            return (int) pos;
        }

        case 0x10u: { /* reportMirrorMemoryDTCExtDataRecordByDTCNumber */
            if (req_len < 6u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint32_t dtc = ((uint32_t) req[2] << 16) | ((uint32_t) req[3] << 8) | (uint32_t) req[4];
            uint8_t ext_record_number = req[5];
            if (dtc != g_mirror[0].dtc) {
                return -0x31; /* requestOutOfRange */
            }
            out_buf[pos++] = (uint8_t) ((dtc >> 16) & 0xFFu);
            out_buf[pos++] = (uint8_t) ((dtc >> 8) & 0xFFu);
            out_buf[pos++] = (uint8_t) (dtc & 0xFFu);
            out_buf[pos++] = g_mirror[0].status;
            out_buf[pos++] = ext_record_number;
            out_buf[pos++] = 0x2Au; /* one byte of extended data */
            return (int) pos;
        }

        case 0x11u:   /* reportNumberOfMirrorMemoryDTCByStatusMask */
        case 0x12u: { /* reportNumberOfEmissionsOBDDTCByStatusMask */
            if (req_len < 3u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t status_mask = req[2];
            const mem_dtc_t *set = (subfn == 0x11u) ? g_mirror : g_emissions;
            uint16_t set_n = (subfn == 0x11u)
                                 ? (uint16_t) (sizeof(g_mirror) / sizeof(g_mirror[0]))
                                 : (uint16_t) (sizeof(g_emissions) / sizeof(g_emissions[0]));
            uint16_t count = 0u;
            for (uint16_t i = 0u; i < set_n; i++) {
                if ((set[i].status & status_mask) != 0u) {
                    count++;
                }
            }
            out_buf[pos++] = APP_STATUS_AVAIL_MASK;
            out_buf[pos++] = APP_DTC_FORMAT_ID;
            out_buf[pos++] = (uint8_t) ((count >> 8) & 0xFFu);
            out_buf[pos++] = (uint8_t) (count & 0xFFu);
            return (int) pos;
        }

        case 0x13u: { /* reportEmissionsOBDDTCByStatusMask: statusAvail + {DTC(3) status}... */
            if (req_len < 3u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t status_mask = req[2];
            out_buf[pos++] = APP_STATUS_AVAIL_MASK;
            for (uint16_t i = 0u; i < (sizeof(g_emissions) / sizeof(g_emissions[0])); i++) {
                if ((g_emissions[i].status & status_mask) != 0u) {
                    pos = put_dtc_status(out_buf, pos, g_emissions[i].dtc, g_emissions[i].status);
                }
            }
            return (int) pos;
        }

        case 0x16u: { /* reportDTCExtDataRecordByRecordNumber: extRecordNumber + {DTC(3) status
                         data} */
            if (req_len < 3u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t ext_record_number = req[2];
            out_buf[pos++] = ext_record_number;
            pos = put_dtc_status(out_buf, pos, 0x012345u, g_primary[0].status);
            out_buf[pos++] = 0x2Au; /* one byte of extended data */
            return (int) pos;
        }

        case 0x17u: { /* reportUserDefMemoryDTCByStatusMask: memSel + statusAvail + {DTC(3) status}
                       */
            if (req_len < 4u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t memory_selection = req[2];
            uint8_t status_mask = req[3];
            out_buf[pos++] = memory_selection;
            out_buf[pos++] = APP_STATUS_AVAIL_MASK;
            for (uint16_t i = 0u; i < (sizeof(g_mirror) / sizeof(g_mirror[0])); i++) {
                if ((g_mirror[i].status & status_mask) != 0u) {
                    pos = put_dtc_status(out_buf, pos, g_mirror[i].dtc, g_mirror[i].status);
                }
            }
            return (int) pos;
        }

        case 0x18u:   /* reportUserDefMemoryDTCSnapshotRecordByDTCNumber */
        case 0x19u: { /* reportUserDefMemoryDTCExtDataRecordByDTCNumber */
            if (req_len < 7u) {
                return -0x13; /* incorrectMessageLength */
            }
            uint8_t memory_selection = req[2];
            uint32_t dtc = ((uint32_t) req[3] << 16) | ((uint32_t) req[4] << 8) | (uint32_t) req[5];
            uint8_t record_number = req[6];
            out_buf[pos++] = memory_selection;
            out_buf[pos++] = (uint8_t) ((dtc >> 16) & 0xFFu);
            out_buf[pos++] = (uint8_t) ((dtc >> 8) & 0xFFu);
            out_buf[pos++] = (uint8_t) (dtc & 0xFFu);
            out_buf[pos++] = g_mirror[0].status;
            out_buf[pos++] = record_number;
            out_buf[pos++] = 0x2Au; /* one byte of snapshot / extended data */
            return (int) pos;
        }

        default:
            break;
    }

    /* Any sub-function this application does not serve. */
    return -0x12; /* subFunctionNotSupported */
}

/* Fire one request, print the response, and report whether it was positive. */
static int fire(uds_ctx_t *ctx, const char *label, const uint8_t *req, uint16_t req_len)
{
    g_resp_len = 0u;
    uds_input_sdu(ctx, req, req_len);

    printf("  %-44s ->", label);
    for (uint16_t i = 0u; i < g_resp_len; i++) {
        printf(" %02X", g_resp[i]);
    }
    printf("\n");

    if ((g_resp_len >= 1u) && (g_resp[0] == 0x59u)) {
        return 0;
    }
    printf("    ERROR: not a positive response\n");
    return 1;
}

int main(void)
{
    static uint8_t rxb[256];
    static uint8_t txb[256];
    uds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.get_time_ms = ecu_time;
    cfg.fn_tp_send = ecu_send;
    cfg.rx_buffer = rxb;
    cfg.rx_buffer_size = sizeof(rxb);
    cfg.tx_buffer = txb;
    cfg.tx_buffer_size = sizeof(txb);
    cfg.dtc_status_availability_mask = APP_STATUS_AVAIL_MASK;
    cfg.dtc_format_id = APP_DTC_FORMAT_ID;
    cfg.fn_dtc_list = app_dtc_list; /* library-framed sub-functions */
    cfg.fn_dtc_read = app_dtc_read; /* application-served sub-functions */

    uds_ctx_t ctx;
    uds_init(&ctx, &cfg);

    printf("=== Library-framed sub-function (0x02 reportDTCByStatusMask) ===\n");
    int rc = 0;
    uint8_t r02[] = {0x19, 0x02, 0xFF};
    rc |= fire(&ctx, "0x02 reportDTCByStatusMask", r02, sizeof(r02));

    printf("\n=== Application-served sub-functions (via fn_dtc_read) ===\n");
    uint8_t r03[] = {0x19, 0x03};
    rc |= fire(&ctx, "0x03 reportDTCSnapshotIdentification", r03, sizeof(r03));
    uint8_t r05[] = {0x19, 0x05, 0x01};
    rc |= fire(&ctx, "0x05 reportDTCStoredDataByRecordNumber", r05, sizeof(r05));
    uint8_t r0f[] = {0x19, 0x0F, 0xFF};
    rc |= fire(&ctx, "0x0F reportMirrorMemoryDTCByStatusMask", r0f, sizeof(r0f));
    uint8_t r10[] = {0x19, 0x10, 0x10, 0x10, 0x10, 0x01};
    rc |= fire(&ctx, "0x10 reportMirrorMemoryDTCExtDataRecord", r10, sizeof(r10));
    uint8_t r11[] = {0x19, 0x11, 0xFF};
    rc |= fire(&ctx, "0x11 reportNumberOfMirrorMemoryDTC", r11, sizeof(r11));
    uint8_t r12[] = {0x19, 0x12, 0xFF};
    rc |= fire(&ctx, "0x12 reportNumberOfEmissionsOBDDTC", r12, sizeof(r12));
    uint8_t r13[] = {0x19, 0x13, 0xFF};
    rc |= fire(&ctx, "0x13 reportEmissionsOBDDTCByStatusMask", r13, sizeof(r13));
    uint8_t r16[] = {0x19, 0x16, 0x01};
    rc |= fire(&ctx, "0x16 reportDTCExtDataRecordByRecordNumber", r16, sizeof(r16));
    uint8_t r17[] = {0x19, 0x17, 0x01, 0xFF};
    rc |= fire(&ctx, "0x17 reportUserDefMemoryDTCByStatusMask", r17, sizeof(r17));
    uint8_t r18[] = {0x19, 0x18, 0x01, 0x10, 0x10, 0x10, 0x01};
    rc |= fire(&ctx, "0x18 reportUserDefMemoryDTCSnapshotRecord", r18, sizeof(r18));
    uint8_t r19[] = {0x19, 0x19, 0x01, 0x10, 0x10, 0x10, 0x01};
    rc |= fire(&ctx, "0x19 reportUserDefMemoryDTCExtDataRecord", r19, sizeof(r19));

    printf("\n%s\n", (rc == 0) ? "All sub-functions answered positively." : "FAILED");
    return rc;
}
