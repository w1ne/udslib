/**
 * @file uds_tp.h
 * @brief UDS Transport Layer Definitions
 *
 * Defines shared defaults for Transport Layer integration.
 */

#ifndef UDS_TP_H
#define UDS_TP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ISO-14229-2 Transport Protocol Types */
#define UDS_TP_PCI_SINGLE_FRAME 0x00
#define UDS_TP_PCI_FIRST_FRAME 0x10
#define UDS_TP_PCI_CONSEC_FRAME 0x20
#define UDS_TP_PCI_FLOW_CONTROL 0x30

/* Common ISO-TP Flow Control Flags */
#define UDS_TP_FC_CONTINUE 0
#define UDS_TP_FC_WAIT 1
#define UDS_TP_FC_OVERFLOW 2

#ifdef __cplusplus
}
#endif

#endif /* UDS_TP_H */
