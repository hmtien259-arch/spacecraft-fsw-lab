/* labs/03-tmtc/ccsds.h
 *
 * CCSDS Space Packet primary header: 6 octets, big endian, per Session 7
 * Section 3. Packed and parsed explicitly with shifts/masks rather than C
 * bit fields (whose in-memory layout is compiler dependent), per the
 * session's own byte-order warning.
 */
#ifndef CCSDS_H
#define CCSDS_H

#include <stddef.h>
#include <stdint.h>

#define CCSDS_PRI_HDR_LEN 6u

typedef struct {
    uint8_t  version;      /* 3 bits, always 0 for version-1 space packets */
    uint8_t  type;         /* 1 bit, 0 = TM, 1 = TC */
    uint8_t  sec_hdr_flag;  /* 1 bit, 1 if a secondary header is present */
    uint16_t apid;         /* 11 bits, Application Process Identifier */
    uint8_t  seq_flags;    /* 2 bits, 3 (0b11) = standalone unsegmented packet */
    uint16_t seq_count;    /* 14 bits, increments per APID, wraps at 16384 */
    uint16_t data_length;  /* 16 bits, (octets in the data field) - 1 */
} ccsds_pri_hdr;

/* Packs `h` into `buf` (must have room for CCSDS_PRI_HDR_LEN bytes).
 * Returns the number of bytes written (always CCSDS_PRI_HDR_LEN). */
size_t ccsds_pack_primary(uint8_t *buf, const ccsds_pri_hdr *h);

/* Inverse of ccsds_pack_primary(): reconstructs `h` from the six octets at
 * `buf`. Used to round-trip prove the packer is correct. */
void ccsds_parse_primary(const uint8_t *buf, ccsds_pri_hdr *h);

/* Housekeeping-service APID used throughout this lab. A real mission would
 * assign this from a project-wide APID allocation table; a single fixed
 * value is enough to demonstrate routing-by-APID here. */
#define CCSDS_APID_HK 0x064 /* 100 decimal, arbitrary but fixed */

#endif /* CCSDS_H */
