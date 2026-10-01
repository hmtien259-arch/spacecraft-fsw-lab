/* labs/03-tmtc/pus.c - PUS TM secondary header pack + HK payload
 * serialize/deserialize, all explicit big endian (shifts/masks, no struct
 * bit fields or raw memcpy of a float, so the wire format does not depend
 * on the host's endianness or float representation quirks). */
#include "pus.h"
#include <string.h>

size_t pus_pack_secondary(uint8_t *buf, const pus_tm_hdr *h) {
    buf[0] = h->pus_version;
    buf[1] = h->service;
    buf[2] = h->subtype;
    buf[3] = (uint8_t) (h->counter >> 8);
    buf[4] = (uint8_t) (h->counter & 0xFFu);
    return PUS_TM_SEC_HDR_LEN;
}

/* Packs a float as its raw IEEE-754 bit pattern, most significant byte
 * first. Extracting the bits via memcpy (not a union+shift on the float
 * itself) keeps this well-defined C regardless of host endianness: once
 * we have a uint32_t, >> and & are endianness independent. */
static void pack_f32_be(uint8_t *buf, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    buf[0] = (uint8_t) (bits >> 24);
    buf[1] = (uint8_t) (bits >> 16);
    buf[2] = (uint8_t) (bits >> 8);
    buf[3] = (uint8_t) (bits & 0xFFu);
}

static float unpack_f32_be(const uint8_t *buf) {
    uint32_t bits = ( (uint32_t) buf[0] << 24 )
                   | ( (uint32_t) buf[1] << 16 )
                   | ( (uint32_t) buf[2] << 8 )
                   |   (uint32_t) buf[3];
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

size_t pus_serialize_hk_payload(uint8_t *buf,
                                 float bus_voltage, uint8_t battery_pct,
                                 float temp_obc, float temp_batt,
                                 uint8_t mode) {
    size_t off = 0;
    pack_f32_be(buf + off, bus_voltage); off += 4;
    buf[off] = battery_pct;              off += 1;
    pack_f32_be(buf + off, temp_obc);    off += 4;
    pack_f32_be(buf + off, temp_batt);   off += 4;
    buf[off] = mode;                     off += 1;
    return off; /* == PUS_HK_PAYLOAD_LEN */
}

void pus_deserialize_hk_payload(const uint8_t *buf,
                                 float *bus_voltage, uint8_t *battery_pct,
                                 float *temp_obc, float *temp_batt,
                                 uint8_t *mode) {
    size_t off = 0;
    *bus_voltage = unpack_f32_be(buf + off); off += 4;
    *battery_pct = buf[off];                 off += 1;
    *temp_obc    = unpack_f32_be(buf + off); off += 4;
    *temp_batt   = unpack_f32_be(buf + off); off += 4;
    *mode        = buf[off];                 off += 1;
    (void) off;
}
