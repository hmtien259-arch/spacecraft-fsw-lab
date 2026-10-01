/* labs/03-tmtc/tc.c - TC validation/parsing and the ground-side TC builder.
 * Explicit big-endian shifts/masks throughout, same discipline as ccsds.c. */
#include "tc.h"

uint16_t tc_crc16(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = (uint16_t) (crc ^ ((uint16_t) buf[i] << 8));
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t) ((crc << 1) ^ 0x1021u)
                                  : (uint16_t) (crc << 1);
        }
    }
    return crc;
}

int tc_parse(const uint8_t *buf, size_t len,
             ccsds_pri_hdr *pri, pus_tc_hdr *tc,
             const uint8_t **app_data, size_t *app_len) {
    if (buf == NULL || pri == NULL || tc == NULL || app_data == NULL || app_len == NULL) {
        return TC_ERR_ARG;
    }

    /* 1. Size sanity, before a single field is read. */
    if (len < TC_MIN_PACKET_LEN) { return TC_ERR_TOO_SHORT; }
    if (len > TC_MAX_PACKET_LEN) { return TC_ERR_TOO_LONG;  }

    /* 2. Length consistency: data_length is (octets in data field) - 1, so
     * the whole packet must be exactly primary header + data_length + 1. */
    ccsds_pri_hdr h;
    ccsds_parse_primary(buf, &h);
    if ((size_t) h.data_length + 1u + CCSDS_PRI_HDR_LEN != len) {
        return TC_ERR_LENGTH;
    }

    /* 3. Integrity, before anything else in the packet is trusted. */
    uint16_t rx_crc = (uint16_t) (((uint16_t) buf[len - 2] << 8) | buf[len - 1]);
    if (tc_crc16(buf, len - TC_PEC_LEN) != rx_crc) {
        return TC_ERR_CRC;
    }

    /* 4. Primary-header fields. */
    if (h.version != 0 || h.type != 1 || h.sec_hdr_flag != 1 || h.seq_flags != 3) {
        return TC_ERR_TYPE;
    }
    if (h.apid != CCSDS_APID_CMD) {
        return TC_ERR_APID;
    }

    /* 5. PUS TC secondary header. */
    const uint8_t *sh = buf + CCSDS_PRI_HDR_LEN;
    if ((uint8_t) (sh[0] >> 4) != PUS_TC_VERSION) {
        return TC_ERR_PUS_VERSION;
    }

    *pri = h;
    tc->ack_flags = (uint8_t) (sh[0] & 0x0Fu);
    tc->service   = sh[1];
    tc->subtype   = sh[2];
    tc->source_id = (uint16_t) (((uint16_t) sh[3] << 8) | sh[4]);
    *app_data = buf + CCSDS_PRI_HDR_LEN + PUS_TC_SEC_HDR_LEN;
    *app_len  = len - TC_MIN_PACKET_LEN;
    return TC_OK;
}

size_t tc_build(uint8_t *out, uint16_t seq_count, const pus_tc_hdr *tc,
                const uint8_t *app_data, size_t app_len) {
    if (out == NULL || tc == NULL || (app_len > 0 && app_data == NULL)) { return 0; }
    size_t total = TC_MIN_PACKET_LEN + app_len;
    if (total > TC_MAX_PACKET_LEN) { return 0; }

    ccsds_pri_hdr pri = {
        .version      = 0,
        .type         = 1, /* TC */
        .sec_hdr_flag = 1,
        .apid         = CCSDS_APID_CMD,
        .seq_flags    = 3,
        .seq_count    = (uint16_t) (seq_count & 0x3FFFu),
        .data_length  = (uint16_t) (total - CCSDS_PRI_HDR_LEN - 1u),
    };
    uint8_t *p = out;
    p += ccsds_pack_primary(p, &pri);
    p[0] = (uint8_t) ((PUS_TC_VERSION << 4) | (tc->ack_flags & 0x0Fu));
    p[1] = tc->service;
    p[2] = tc->subtype;
    p[3] = (uint8_t) (tc->source_id >> 8);
    p[4] = (uint8_t) (tc->source_id & 0xFFu);
    p += PUS_TC_SEC_HDR_LEN;
    for (size_t i = 0; i < app_len; ++i) { *p++ = app_data[i]; }

    uint16_t crc = tc_crc16(out, (size_t) (p - out));
    *p++ = (uint8_t) (crc >> 8);
    *p++ = (uint8_t) (crc & 0xFFu);
    return (size_t) (p - out);
}
