/* labs/03-tmtc/tc.h
 *
 * Telecommand (TC) packet parsing for Session 8: the CCSDS primary header
 * (same 6 octets as Session 7, packet type bit = 1) followed by a PUS TC
 * secondary header, the application data, and a 2-octet packet error
 * control (PEC) field.
 *
 *   [ primary : 6 B ][ PUS TC sec hdr : 5 B ][ app data : N B ][ PEC : 2 B ]
 *                     ver|ack, svc, sub, src   (N may be 0)      CRC-16
 *
 * Every TC is treated as untrusted input: tc_parse() either returns TC_OK
 * with every field validated, or an error code and nothing usable.
 *
 * Subtype note (same warning as pus.h): service/subtype numbers used with
 * this parser follow the session text and ECSS-E-ST-70-41; confirm them
 * against the standard before any real use.
 */
#ifndef TC_H
#define TC_H

#include "ccsds.h"

#include <stddef.h>
#include <stdint.h>

#define PUS_TC_VERSION        2u  /* PUS-C, upper nibble of the first sec-hdr octet */
#define PUS_TC_SEC_HDR_LEN    5u  /* ver|ack(1) + service(1) + subtype(1) + source_id(2) */
#define TC_PEC_LEN            2u  /* CRC-16 over every preceding octet of the packet */
#define TC_MIN_PACKET_LEN     (CCSDS_PRI_HDR_LEN + PUS_TC_SEC_HDR_LEN + TC_PEC_LEN)
#define TC_MAX_PACKET_LEN     256u /* "plausible length" bound for this lab */

/* Acknowledgement flags (low nibble of the first secondary-header octet):
 * which Service 1 success reports the ground wants for this command. */
#define PUS_ACK_ACCEPT    0x8u
#define PUS_ACK_START     0x4u
#define PUS_ACK_PROGRESS  0x2u
#define PUS_ACK_COMPLETE  0x1u

typedef struct {
    uint8_t  ack_flags; /* 4 bits: accept | start | progress | complete */
    uint8_t  service;
    uint8_t  subtype;
    uint16_t source_id;
} pus_tc_hdr;

/* tc_parse() result codes. Anything other than TC_OK means "do not act". */
enum {
    TC_OK = 0,
    TC_ERR_ARG,          /* NULL pointer passed in                                  */
    TC_ERR_TOO_SHORT,    /* shorter than the smallest legal TC                      */
    TC_ERR_TOO_LONG,     /* longer than TC_MAX_PACKET_LEN                           */
    TC_ERR_LENGTH,       /* data_length field disagrees with the bytes received     */
    TC_ERR_CRC,          /* packet error control check failed                       */
    TC_ERR_TYPE,         /* not a version-1 standalone TC with a secondary header   */
    TC_ERR_APID,         /* addressed to an APID this application does not serve    */
    TC_ERR_PUS_VERSION   /* secondary header is not PUS-C                           */
};

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR),
 * the CRC the PUS packet error control field uses. */
uint16_t tc_crc16(const uint8_t *buf, size_t len);

/* Validates and parses a received TC. Order of checks: size sanity, length
 * consistency, CRC, primary-header fields, PUS header. The CRC is checked
 * before any field of the packet is believed (only the length is read
 * first, because it tells us where the CRC is).
 *
 * On TC_OK: *pri and *tc are filled, *app_data points INTO buf (no copy) and
 * *app_len is the application-data length. On any error the outputs must not
 * be used. */
int tc_parse(const uint8_t *buf, size_t len,
             ccsds_pri_hdr *pri, pus_tc_hdr *tc,
             const uint8_t **app_data, size_t *app_len);

/* Ground-side helper (used by the self checks and any host tooling): builds
 * a complete, valid TC packet with the given sequence count into `out`
 * (room for TC_MAX_PACKET_LEN bytes) and returns its length, or 0 if the
 * arguments cannot form a legal packet. */
size_t tc_build(uint8_t *out, uint16_t seq_count, const pus_tc_hdr *tc,
                const uint8_t *app_data, size_t app_len);

#endif /* TC_H */
