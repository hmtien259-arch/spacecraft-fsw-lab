/* labs/03-tmtc/verify.c - Service 1 verification reports and the Service 17
 * connection test report. */
#include "verify.h"

#include "ccsds.h"
#include "pus.h"

/* Packs primary header + PUS TM secondary header for a report whose data
 * field (after the secondary header) is `data_len` octets, and advances the
 * APID's sequence counter. Returns the header bytes written. */
static size_t pack_tm_headers(uint8_t *out, uint16_t apid, uint16_t *tm_seq,
                              uint8_t service, uint8_t subtype, size_t data_len) {
    ccsds_pri_hdr pri = {
        .version      = 0,
        .type         = 0, /* TM */
        .sec_hdr_flag = 1,
        .apid         = apid,
        .seq_flags    = 3,
        .seq_count    = *tm_seq,
        .data_length  = (uint16_t) ((PUS_TM_SEC_HDR_LEN + data_len) - 1u),
    };
    pus_tm_hdr sec = {
        .pus_version = PUS_TM_VERSION,
        .service     = service,
        .subtype     = subtype,
        .counter     = *tm_seq, /* same convention as the Session 7 HK packet */
    };
    size_t n = ccsds_pack_primary(out, &pri);
    n += pus_pack_secondary(out + n, &sec);
    *tm_seq = (uint16_t) ((*tm_seq + 1u) % 16384u);
    return n;
}

size_t build_verification(uint8_t *out, uint16_t tm_apid, uint16_t *tm_seq,
                          uint8_t v_subtype,
                          uint16_t cmd_pkt_id, uint16_t cmd_seq_ctrl,
                          uint8_t failure_code) {
    if (out == NULL || tm_seq == NULL) { return 0; }
    if (v_subtype < PUS_VER_ACCEPT_OK || v_subtype > PUS_VER_COMPLETE_FAIL) { return 0; }

    int is_failure = (v_subtype % 2u) == 0u; /* even subtypes are the failure reports */
    size_t data_len = 4u + (is_failure ? 1u : 0u);

    size_t n = pack_tm_headers(out, tm_apid, tm_seq, PUS_SERVICE_VERIFICATION,
                               v_subtype, data_len);
    out[n++] = (uint8_t) (cmd_pkt_id >> 8);
    out[n++] = (uint8_t) (cmd_pkt_id & 0xFFu);
    out[n++] = (uint8_t) (cmd_seq_ctrl >> 8);
    out[n++] = (uint8_t) (cmd_seq_ctrl & 0xFFu);
    if (is_failure) { out[n++] = failure_code; }
    return n;
}

size_t build_conn_test_report(uint8_t *out, uint16_t tm_apid, uint16_t *tm_seq) {
    if (out == NULL || tm_seq == NULL) { return 0; }
    return pack_tm_headers(out, tm_apid, tm_seq, PUS_SERVICE_TEST,
                           PUS_TEST_CONNECTION_TEST_REPORT, 0u);
}
