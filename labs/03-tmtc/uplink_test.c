/* labs/03-tmtc/uplink_test.c
 *
 * Session 8 Step 4 self checks (assert based, same style as main.c and
 * labs/01-foundation/test_hk.c):
 *   A. valid TC[17,1]              -> TM[17,2] and success verification
 *   B. unknown (service, subtype)  -> TM[1,2] acceptance failure, no execution
 *   C. length inconsistent packet  -> rejected before any dispatch
 * plus the stretch goals (ack flags honored, CRC verified) and a
 * "never execute unvalidated input" sweep over every truncation and every
 * single-bit corruption of a valid command.
 */
#include "uplink.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TC_APID_CMD_PKT_ID 0x1865u /* version 0 | TC | sec-hdr | APID 0x065 */

static void hex(const char *label, const uint8_t *buf, size_t len) {
    printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) { printf("%02X%s", buf[i], (i + 1 < len) ? " " : ""); }
    printf("\n");
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t) (((uint16_t) p[0] << 8) | p[1]); }

/* Envelope check common to every report: TM packet from the command APID,
 * PUS-C header, data_length consistent with the bytes produced. */
static void expect_tm(const tm_out *o, size_t idx, uint8_t service, uint8_t subtype,
                      size_t expect_len) {
    assert(idx < o->count);
    const uint8_t *p = o->pkt[idx];
    ccsds_pri_hdr h;
    ccsds_parse_primary(p, &h);
    assert(h.type == 0 && h.sec_hdr_flag == 1 && h.apid == CCSDS_APID_CMD);
    assert(h.seq_flags == 3);
    assert(o->len[idx] == expect_len);
    assert((size_t) h.data_length + 1u + CCSDS_PRI_HDR_LEN == o->len[idx]);
    assert(p[CCSDS_PRI_HDR_LEN + 1] == service && p[CCSDS_PRI_HDR_LEN + 2] == subtype); /* PUS hdr follows primary */
}

/* Service 1 report: names the right command and (for failures) the right code. */
static void expect_verif(const tm_out *o, size_t idx, uint8_t subtype,
                         uint16_t pkt_id, uint16_t seq_ctrl, uint8_t fail) {
    int is_fail = (subtype % 2u) == 0u;
    expect_tm(o, idx, PUS_SERVICE_VERIFICATION, subtype, is_fail ? 16u : 15u);
    const uint8_t *d = o->pkt[idx] + CCSDS_PRI_HDR_LEN + 5u;
    assert(rd16(d) == pkt_id);
    assert(rd16(d + 2) == seq_ctrl);
    if (is_fail) { assert(d[4] == fail); }
}

static size_t build_cmd(uint8_t *buf, uint16_t seq, uint8_t ack, uint8_t svc, uint8_t sub,
                        const uint8_t *app, size_t app_len) {
    pus_tc_hdr tc = { .ack_flags = ack, .service = svc, .subtype = sub, .source_id = 0x0001 };
    size_t n = tc_build(buf, seq, &tc, app, app_len);
    assert(n != 0);
    return n;
}

/* Recomputes the trailing CRC so a test can corrupt ONE field and prove that
 * the intended check (not just the CRC) is what rejects it. */
static void refresh_crc(uint8_t *buf, size_t len) {
    uint16_t c = tc_crc16(buf, len - TC_PEC_LEN);
    buf[len - 2] = (uint8_t) (c >> 8);
    buf[len - 1] = (uint8_t) (c & 0xFFu);
}

static void check_crc_and_roundtrip(void) {
    /* Published check value for CRC-16/CCITT-FALSE. */
    assert(tc_crc16((const uint8_t *) "123456789", 9) == 0x29B1);

    uint8_t buf[TC_MAX_PACKET_LEN];
    const uint8_t app[3] = { 0xDE, 0xAD, 0x01 };
    size_t n = build_cmd(buf, 7, PUS_ACK_ACCEPT | PUS_ACK_COMPLETE, 200, 9, app, sizeof(app));

    ccsds_pri_hdr pri; pus_tc_hdr tc; const uint8_t *a; size_t al;
    assert(tc_parse(buf, n, &pri, &tc, &a, &al) == TC_OK);
    assert(pri.type == 1 && pri.apid == CCSDS_APID_CMD && pri.seq_count == 7);
    assert(tc.service == 200 && tc.subtype == 9 && tc.source_id == 1);
    assert(tc.ack_flags == (PUS_ACK_ACCEPT | PUS_ACK_COMPLETE));
    assert(al == 3 && memcmp(a, app, 3) == 0);
    printf("PASS TC build/parse round trip, CRC-16 check value 0x29B1\n");
}

/* A. Valid TC[17,1]. */
static void check_valid_command(void) {
    uplink_ctx ctx; tm_out out; uint8_t cmd[TC_MAX_PACKET_LEN];
    uplink_init(&ctx);

    /* Default lab request: acceptance + completion reports. */
    size_t n = build_cmd(cmd, 5, PUS_ACK_ACCEPT | PUS_ACK_COMPLETE, 17, 1, NULL, 0);
    assert(uplink_handle_tc(&ctx, cmd, n, &out) == TC_OK);

    printf("Case A - valid TC[17,1], ack = accept|complete\n");
    hex("TC   ", cmd, n);
    for (size_t i = 0; i < out.count; ++i) { hex("TM   ", out.pkt[i], out.len[i]); }

    assert(out.count == 3);
    expect_verif(&out, 0, PUS_VER_ACCEPT_OK,   TC_APID_CMD_PKT_ID, 0xC005, 0);
    expect_tm   (&out, 1, PUS_SERVICE_TEST, PUS_TEST_CONNECTION_TEST_REPORT, TEST_REPORT_LEN);
    expect_verif(&out, 2, PUS_VER_COMPLETE_OK, TC_APID_CMD_PKT_ID, 0xC005, 0);
    assert(ctx.accepted == 1 && ctx.rejected == 0 && ctx.executed == 1 && ctx.conn_tests == 1);

    /* Stretch: ack flags honored. All four flags -> accept, start, report,
     * complete (progress has no stage for this command). */
    n = build_cmd(cmd, 6, 0xF, 17, 1, NULL, 0);
    assert(uplink_handle_tc(&ctx, cmd, n, &out) == TC_OK);
    assert(out.count == 4);
    expect_verif(&out, 0, PUS_VER_ACCEPT_OK,   TC_APID_CMD_PKT_ID, 0xC006, 0);
    expect_verif(&out, 1, PUS_VER_START_OK,    TC_APID_CMD_PKT_ID, 0xC006, 0);
    expect_tm   (&out, 2, PUS_SERVICE_TEST, PUS_TEST_CONNECTION_TEST_REPORT, TEST_REPORT_LEN);
    expect_verif(&out, 3, PUS_VER_COMPLETE_OK, TC_APID_CMD_PKT_ID, 0xC006, 0);

    /* No ack flags: the command still runs and answers (the TM[17,2] is its
     * own product), but no Service 1 success report is sent. */
    n = build_cmd(cmd, 7, 0, 17, 1, NULL, 0);
    assert(uplink_handle_tc(&ctx, cmd, n, &out) == TC_OK);
    assert(out.count == 1);
    expect_tm(&out, 0, PUS_SERVICE_TEST, PUS_TEST_CONNECTION_TEST_REPORT, TEST_REPORT_LEN);

    /* Report sequence counts are consecutive across everything emitted. */
    assert(ctx.tm_seq == 3 + 4 + 1);
    assert(ctx.executed == 3 && ctx.conn_tests == 3);
    printf("PASS valid TC[17,1] -> TM[17,2] + success verification; ack flags honored\n\n");
}

/* B. Unknown (service, subtype), or known command with wrong arguments. */
static void check_unknown_command(void) {
    uplink_ctx ctx; tm_out out; uint8_t cmd[TC_MAX_PACKET_LEN];
    uplink_init(&ctx);

    size_t n = build_cmd(cmd, 9, PUS_ACK_ACCEPT | PUS_ACK_COMPLETE, 17, 99, NULL, 0);
    assert(uplink_handle_tc(&ctx, cmd, n, &out) == UPLINK_ERR_UNKNOWN_CMD);

    printf("Case B - unknown TC[17,99]\n");
    hex("TC   ", cmd, n);
    hex("TM   ", out.pkt[0], out.len[0]);

    assert(out.count == 1);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC009, VFAIL_UNKNOWN_CMD);
    assert(ctx.executed == 0 && ctx.conn_tests == 0 && ctx.accepted == 0 && ctx.rejected == 1);

    /* A completely different service is equally unknown - the KEY is the pair. */
    n = build_cmd(cmd, 10, 0, 200, 1, NULL, 0);
    assert(uplink_handle_tc(&ctx, cmd, n, &out) == UPLINK_ERR_UNKNOWN_CMD);
    assert(out.count == 1); /* failure reported even with no ack flags requested */
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC00A, VFAIL_UNKNOWN_CMD);

    /* Known command, but TC[17,1] takes no application data. */
    const uint8_t junk = 0x55;
    n = build_cmd(cmd, 11, PUS_ACK_ACCEPT, 17, 1, &junk, 1);
    assert(uplink_handle_tc(&ctx, cmd, n, &out) == UPLINK_ERR_BAD_APP_DATA);
    assert(out.count == 1);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC00B, VFAIL_BAD_APP_DATA);

    assert(ctx.executed == 0 && ctx.conn_tests == 0 && ctx.rejected == 3);
    printf("PASS unknown / badly-argued command -> TM[1,2], nothing executed\n\n");
}

/* C. Malformed packets: rejected before any dispatch. */
static void check_malformed(void) {
    uplink_ctx ctx; tm_out out; uint8_t good[TC_MAX_PACKET_LEN], bad[TC_MAX_PACKET_LEN];
    uplink_init(&ctx);
    size_t n = build_cmd(good, 20, PUS_ACK_ACCEPT | PUS_ACK_COMPLETE, 17, 1, NULL, 0);

    /* C1. data_length field lies (one too big) but the CRC is re-computed to
     * match, so ONLY the length consistency check can catch it. */
    memcpy(bad, good, n);
    bad[5] = (uint8_t) (bad[5] + 1u);
    refresh_crc(bad, n);
    assert(uplink_handle_tc(&ctx, bad, n, &out) == TC_ERR_LENGTH);
    printf("Case C - length inconsistent packet (data_length says one byte more than received)\n");
    hex("TC   ", bad, n);
    hex("TM   ", out.pkt[0], out.len[0]);
    assert(out.count == 1);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC014, VFAIL_ILLEGAL_LEN);

    /* C2. Truncated in transit: a command carrying 3 data bytes loses its
     * last byte. Still >= minimum length, so the length check decides. */
    const uint8_t three[3] = { 1, 2, 3 };
    uint8_t longer[TC_MAX_PACKET_LEN];
    size_t ln = build_cmd(longer, 21, PUS_ACK_ACCEPT, 17, 1, three, sizeof(three));
    assert(uplink_handle_tc(&ctx, longer, ln - 1u, &out) == TC_ERR_LENGTH);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC015, VFAIL_ILLEGAL_LEN);

    /* C3. Shorter than any legal TC but still 4+ bytes: identifiable, reported. */
    assert(uplink_handle_tc(&ctx, good, 8, &out) == TC_ERR_TOO_SHORT);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC014, VFAIL_ILLEGAL_LEN);

    /* C4. Too short to name at all: dropped, no report possible, no crash. */
    assert(uplink_handle_tc(&ctx, good, 3, &out) == TC_ERR_TOO_SHORT);
    assert(out.count == 0 && ctx.dropped == 1);
    assert(uplink_handle_tc(&ctx, NULL, 0, &out) == TC_ERR_ARG);
    assert(out.count == 0);

    /* C5. Implausibly long. */
    uint8_t big[TC_MAX_PACKET_LEN + 8];
    memset(big, 0, sizeof(big));
    memcpy(big, good, n);
    assert(uplink_handle_tc(&ctx, big, sizeof(big), &out) == TC_ERR_TOO_LONG);
    assert(out.count == 1);

    /* Integrity: flip a payload bit -> CRC failure (stretch goal). */
    memcpy(bad, good, n);
    bad[7] ^= 0x01u; /* service field */
    assert(uplink_handle_tc(&ctx, bad, n, &out) == TC_ERR_CRC);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC014, VFAIL_BAD_CRC);

    /* Wrong destination APID, CRC repaired so only the APID check fires. */
    memcpy(bad, good, n);
    bad[1] = 0x66;
    refresh_crc(bad, n);
    assert(uplink_handle_tc(&ctx, bad, n, &out) == TC_ERR_APID);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, 0x1866, 0xC014, VFAIL_BAD_APID);

    /* A telemetry packet (type bit 0) arriving on the uplink is not a command. */
    memcpy(bad, good, n);
    bad[0] &= (uint8_t) ~0x10u;
    refresh_crc(bad, n);
    assert(uplink_handle_tc(&ctx, bad, n, &out) == TC_ERR_TYPE);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, 0x0865, 0xC014, VFAIL_BAD_HEADER);

    /* Not PUS-C. */
    memcpy(bad, good, n);
    bad[6] = (uint8_t) ((bad[6] & 0x0Fu) | 0x10u);
    refresh_crc(bad, n);
    assert(uplink_handle_tc(&ctx, bad, n, &out) == TC_ERR_PUS_VERSION);
    expect_verif(&out, 0, PUS_VER_ACCEPT_FAIL, TC_APID_CMD_PKT_ID, 0xC014, VFAIL_BAD_HEADER);

    assert(ctx.executed == 0 && ctx.conn_tests == 0 && ctx.accepted == 0);
    printf("PASS malformed packets rejected before dispatch, nothing executed\n\n");
}

/* Nothing corrupted may ever reach a handler: sweep every truncation, one
 * appended byte, and every single-bit flip of a valid command. */
static void check_never_execute_unvalidated(void) {
    uplink_ctx ctx; tm_out out; uint8_t good[TC_MAX_PACKET_LEN], bad[TC_MAX_PACKET_LEN];
    uplink_init(&ctx);
    size_t n = build_cmd(good, 33, 0xF, 17, 1, NULL, 0);
    size_t cases = 0;

    for (size_t l = 0; l < n; ++l) {
        assert(uplink_handle_tc(&ctx, good, l, &out) != TC_OK);
        assert(out.count <= 1);
        ++cases;
    }
    memcpy(bad, good, n);
    bad[n] = 0x00;
    assert(uplink_handle_tc(&ctx, bad, n + 1u, &out) != TC_OK);
    ++cases;

    for (size_t bit = 0; bit < n * 8u; ++bit) {
        memcpy(bad, good, n);
        bad[bit / 8u] ^= (uint8_t) (1u << (bit % 8u));
        assert(uplink_handle_tc(&ctx, bad, n, &out) != TC_OK);
        assert(out.count == 1); /* exactly one report: the acceptance failure */
        assert(out.pkt[0][7] == PUS_SERVICE_VERIFICATION && out.pkt[0][8] == PUS_VER_ACCEPT_FAIL);
        ++cases;
    }
    assert(ctx.executed == 0 && ctx.conn_tests == 0 && ctx.accepted == 0);

    /* ...and the unmodified command still works afterwards. */
    assert(uplink_handle_tc(&ctx, good, n, &out) == TC_OK && ctx.executed == 1);
    printf("PASS %zu truncated/extended/bit-flipped variants: none executed, valid one still runs\n\n",
           cases);
}

void uplink_selfcheck(void) {
    printf("=== Session 8: telecommand path self checks ===\n");
    check_crc_and_roundtrip();
    printf("\n");
    check_valid_command();
    check_unknown_command();
    check_malformed();
    check_never_execute_unvalidated();
    printf("all Session 8 uplink self checks passed\n");
}
