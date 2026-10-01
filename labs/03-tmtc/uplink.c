/* labs/03-tmtc/uplink.c - command validation, dispatch table, execution and
 * verification-report sequencing. */
#include "uplink.h"

#include <stddef.h>

/* ---- Command table -------------------------------------------------------
 * One handler per (service, subtype). The dispatch key is the PAIR: TC[17,1]
 * and TC[3,1] are unrelated commands. */
typedef int (*cmd_handler)(uplink_ctx *ctx, const uint8_t *app, size_t app_len, tm_out *out);

typedef struct {
    uint8_t     service;
    uint8_t     subtype;
    size_t      app_len;   /* exact application-data length this command takes */
    cmd_handler handler;
} cmd_entry;

static int tm_out_add(tm_out *out, size_t len) {
    if (out->count >= UPLINK_MAX_TM_PER_TC || len == 0) { return 0; }
    out->len[out->count] = len;
    out->count++;
    return 1;
}

/* TC[17,1] perform connection test -> TM[17,2]. Takes no application data. */
static int handle_connection_test(uplink_ctx *ctx, const uint8_t *app, size_t app_len,
                                  tm_out *out) {
    (void) app;
    (void) app_len;
    ctx->conn_tests++;
    size_t n = build_conn_test_report(out->pkt[out->count], CCSDS_APID_CMD, &ctx->tm_seq);
    return tm_out_add(out, n) ? VFAIL_NONE : VFAIL_EXEC_ERROR;
}

static const cmd_entry cmd_table[] = {
    { PUS_SERVICE_TEST, PUS_TEST_CONNECTION_TEST, 0u, handle_connection_test },
};

static const cmd_entry *cmd_find(uint8_t service, uint8_t subtype) {
    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(cmd_table[0]); ++i) {
        if (cmd_table[i].service == service && cmd_table[i].subtype == subtype) {
            return &cmd_table[i];
        }
    }
    return NULL;
}

int dispatch(const pus_tc_hdr *tc, const uint8_t *app, size_t app_len) {
    (void) app;
    if (tc == NULL) { return CMD_UNKNOWN; }
    const cmd_entry *e = cmd_find(tc->service, tc->subtype);
    if (e == NULL) { return CMD_UNKNOWN; }   /* reject cleanly, never crash */
    if (app_len != e->app_len) { return CMD_BAD_ARGS; }
    return CMD_OK;
}

/* ---- Reporting helpers --------------------------------------------------- */

static void emit_verification(uplink_ctx *ctx, tm_out *out, uint8_t v_subtype,
                              uint16_t pkt_id, uint16_t seq_ctrl, uint8_t failure_code) {
    size_t n = build_verification(out->pkt[out->count], CCSDS_APID_CMD, &ctx->tm_seq,
                                  v_subtype, pkt_id, seq_ctrl, failure_code);
    (void) tm_out_add(out, n);
}

static uint8_t failure_for_parse_error(int status) {
    switch (status) {
    case TC_ERR_TOO_SHORT:
    case TC_ERR_TOO_LONG:
    case TC_ERR_LENGTH:      return VFAIL_ILLEGAL_LEN;
    case TC_ERR_CRC:         return VFAIL_BAD_CRC;
    case TC_ERR_APID:        return VFAIL_BAD_APID;
    case TC_ERR_TYPE:
    case TC_ERR_PUS_VERSION: return VFAIL_BAD_HEADER;
    default:                 return VFAIL_BAD_HEADER;
    }
}

/* Even a packet that failed validation usually still has four readable
 * octets at the front (packet id + sequence control). Quoting them back is
 * what lets the ground match the failure report to the command it sent. */
static uint16_t be16(const uint8_t *p) { return (uint16_t) (((uint16_t) p[0] << 8) | p[1]); }

/* ---- The command path ---------------------------------------------------- */

void uplink_init(uplink_ctx *ctx) {
    ctx->tm_seq = 0;
    ctx->accepted = ctx->rejected = ctx->dropped = 0;
    ctx->executed = ctx->conn_tests = 0;
}

int uplink_handle_tc(uplink_ctx *ctx, const uint8_t *buf, size_t len, tm_out *out) {
    out->count = 0;

    /* 1. Validate. Nothing below this block runs for a bad packet. */
    ccsds_pri_hdr pri;
    pus_tc_hdr tc;
    const uint8_t *app = NULL;
    size_t app_len = 0;
    int st = tc_parse(buf, len, &pri, &tc, &app, &app_len);
    if (st != TC_OK) {
        if (buf != NULL && len >= 4u) {
            ctx->rejected++;
            emit_verification(ctx, out, PUS_VER_ACCEPT_FAIL, be16(buf), be16(buf + 2),
                              failure_for_parse_error(st));
        } else {
            ctx->dropped++; /* cannot identify the command, so no report can name it */
        }
        return st;
    }

    /* Identity of this command, quoted in every report about it:
     * packet id = version|type|sec-hdr-flag|APID, seq ctrl = flags|count. */
    uint16_t pkt_id   = be16(buf);
    uint16_t seq_ctrl = be16(buf + 2);

    /* 2. Is it a command we know, with the right arguments? */
    int d = dispatch(&tc, app, app_len);
    if (d != CMD_OK) {
        ctx->rejected++;
        emit_verification(ctx, out, PUS_VER_ACCEPT_FAIL, pkt_id, seq_ctrl,
                          d == CMD_UNKNOWN ? VFAIL_UNKNOWN_CMD : VFAIL_BAD_APP_DATA);
        return d == CMD_UNKNOWN ? UPLINK_ERR_UNKNOWN_CMD : UPLINK_ERR_BAD_APP_DATA;
    }

    /* 3. Accepted. */
    ctx->accepted++;
    if (tc.ack_flags & PUS_ACK_ACCEPT) {
        emit_verification(ctx, out, PUS_VER_ACCEPT_OK, pkt_id, seq_ctrl, VFAIL_NONE);
    }
    if (tc.ack_flags & PUS_ACK_START) {
        emit_verification(ctx, out, PUS_VER_START_OK, pkt_id, seq_ctrl, VFAIL_NONE);
    }

    /* 4. Execute. dispatch() just proved the entry exists. */
    const cmd_entry *e = cmd_find(tc.service, tc.subtype);
    ctx->executed++;
    int fail = e->handler(ctx, app, app_len, out);

    /* 5. Completion. (No PUS_ACK_PROGRESS stage: TC[17,1] has no intermediate
     * steps, so a progress flag on it correctly yields no report.) */
    if (fail != VFAIL_NONE) {
        emit_verification(ctx, out, PUS_VER_COMPLETE_FAIL, pkt_id, seq_ctrl, (uint8_t) fail);
    } else if (tc.ack_flags & PUS_ACK_COMPLETE) {
        emit_verification(ctx, out, PUS_VER_COMPLETE_OK, pkt_id, seq_ctrl, VFAIL_NONE);
    }
    return TC_OK;
}
