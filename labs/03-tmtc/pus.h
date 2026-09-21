/* labs/03-tmtc/pus.h
 *
 * A simplified PUS (ECSS-E-ST-70-41) TM secondary header, and the HK
 * payload serializer, for Session 7's Service 3 (Housekeeping) report.
 *
 * Subtype note (per Session 7 Section 4's own warning): PUS_HK_SUBTYPE
 * below follows the session's worked example (Service 3, subtype 25 -
 * "HK parameter report") but the exact subtype has NOT been checked
 * against ECSS-E-ST-70-41 itself. Treat it as provisional; a real mission
 * implementation must confirm it against the standard before flight.
 */
#ifndef PUS_H
#define PUS_H

#include <stddef.h>
#include <stdint.h>

#define PUS_SERVICE_HOUSEKEEPING   3u
#define PUS_HK_SUBTYPE             25u /* "HK parameter report" - verify against ECSS-E-ST-70-41 */
#define PUS_TM_VERSION             0x20u /* PUS-C style version/ack nibble, see the standard */

#define PUS_TM_SEC_HDR_LEN 5u /* pus_version(1) + service(1) + subtype(1) + counter(2) */

typedef struct {
    uint8_t  pus_version; /* nibble-packed version/ack field, kept as a single byte for this lab */
    uint8_t  service;     /* 3 = housekeeping */
    uint8_t  subtype;     /* PUS_HK_SUBTYPE for a housekeeping parameter report */
    uint16_t counter;     /* message-type sequence counter, big endian */
} pus_tm_hdr;

/* Packs `h` into `buf` (must have room for PUS_TM_SEC_HDR_LEN bytes).
 * Returns the number of bytes written. */
size_t pus_pack_secondary(uint8_t *buf, const pus_tm_hdr *h);

/* --- HK payload ---------------------------------------------------------
 * Fixed layout, big endian: bus_voltage(f32) | battery_pct(u8) |
 * temp_obc(f32) | temp_batt(f32) | mode(u8). Deliberately takes plain
 * values rather than a spacecraft_state pointer, so the PUS/CCSDS layer
 * has no dependency on the Session 3/4 HK struct's internal layout - only
 * on the values it needs to serialize. */
#define PUS_HK_PAYLOAD_LEN 14u /* 4 + 1 + 4 + 4 + 1 */

size_t pus_serialize_hk_payload(uint8_t *buf,
                                 float bus_voltage, uint8_t battery_pct,
                                 float temp_obc, float temp_batt,
                                 uint8_t mode);

/* Inverse of pus_serialize_hk_payload(), used for the self-check round trip. */
void pus_deserialize_hk_payload(const uint8_t *buf,
                                 float *bus_voltage, uint8_t *battery_pct,
                                 float *temp_obc, float *temp_batt,
                                 uint8_t *mode);

#endif /* PUS_H */
