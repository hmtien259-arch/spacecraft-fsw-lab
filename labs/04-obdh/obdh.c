/* labs/04-obdh/obdh.c - router, store-and-forward and TM/TC integration. */
#include "obdh.h"

#include <string.h>

obdh_dest obdh_route(uint16_t apid, uint8_t pkt_type, int in_contact) {
    if (pkt_type == 1) { return DEST_DISPATCH; } /* TC -> Session 8 dispatcher */
    switch (apid) {
    case APID_HK:
    case APID_OBDH_HK:
        return in_contact ? DEST_DOWNLINK : DEST_STORE;
    case APID_EVENT:
        return in_contact ? DEST_DOWNLINK : DEST_STORE;
    case CCSDS_APID_CMD:
        return DEST_DOWNLINK;           /* verification / test reports */
    default:
        return DEST_DROP;               /* unknown: drop and count */
    }
}

void obdh_init(obdh_ctx *c, obdh_downlink_fn downlink, void *user, int in_contact) {
    memset(c, 0, sizeof(*c));
    store_init(&c->hk_store);
    store_init(&c->ev_store);
    uplink_init(&c->uplink);
    c->downlink      = downlink;
    c->downlink_user = user;
    c->in_contact    = in_contact ? 1 : 0;
}

void obdh_tick(obdh_ctx *c) {
    c->onboard_time++;
}

static void deliver(obdh_ctx *c, const uint8_t *pkt, size_t len) {
    if (c->downlink != NULL) { c->downlink(c->downlink_user, pkt, len); }
    c->n_downlinked++;
}

static void drain(obdh_ctx *c, packet_store *s) {
    uint8_t buf[MAX_PKT];
    size_t n;
    while (store_get(s, buf, &n)) {
        deliver(c, buf, n); /* forward stored telemetry first, in order */
    }
}

void obdh_set_contact(obdh_ctx *c, int in_contact) {
    int was = c->in_contact;
    c->in_contact = in_contact ? 1 : 0;
    if (!was && c->in_contact) {
        drain(c, &c->ev_store); /* critical data first ... */
        drain(c, &c->hk_store); /* ... then routine housekeeping */
    }
}

obdh_dest obdh_ingest(obdh_ctx *c, const uint8_t *pkt, size_t len) {
    if (c == NULL) { return DEST_DROP; }
    if (pkt == NULL || len < CCSDS_PRI_HDR_LEN) {
        c->n_drop_malformed++;
        return DEST_DROP;
    }

    ccsds_pri_hdr h;
    ccsds_parse_primary(pkt, &h);

    /* Telemetry must be self-consistent before it is believed. (TCs are
     * validated, and rejected with a report, by the Session 8 path.) */
    if (h.type == 0 && (size_t) h.data_length + 1u + CCSDS_PRI_HDR_LEN != len) {
        c->n_drop_malformed++;
        return DEST_DROP;
    }

    obdh_dest dest = obdh_route(h.apid, h.type, c->in_contact);
    switch (dest) {
    case DEST_DOWNLINK:
        deliver(c, pkt, len);
        break;

    case DEST_STORE: {
        packet_store *s = (h.apid == APID_EVENT) ? &c->ev_store : &c->hk_store;
        (void) store_put_at(s, pkt, len, c->onboard_time); /* losses are counted by the store */
        c->n_stored++;
        break;
    }

    case DEST_DISPATCH: {
        tm_out out;
        c->n_dispatched++;
        (void) uplink_handle_tc(&c->uplink, pkt, len, &out);
        /* The command path's reports are telemetry like any other: they go
         * back through the router rather than around it. */
        for (size_t i = 0; i < out.count; ++i) {
            (void) obdh_ingest(c, out.pkt[i], out.len[i]);
        }
        break;
    }

    case DEST_DROP:
    default:
        c->n_drop_unknown++;
        break;
    }
    return dest;
}

size_t obdh_pack_tm(uint8_t *out, size_t cap, uint16_t apid, uint16_t seq,
                    uint8_t service, uint8_t subtype,
                    const uint8_t *payload, size_t payload_len) {
    size_t total = CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + payload_len;
    if (out == NULL || (payload_len > 0 && payload == NULL) || total > cap ||
        total - CCSDS_PRI_HDR_LEN - 1u > 0xFFFFu) {
        return 0;
    }
    seq = (uint16_t) (seq & 0x3FFFu);
    ccsds_pri_hdr pri = {
        .version      = 0,
        .type         = 0, /* TM */
        .sec_hdr_flag = 1,
        .apid         = apid,
        .seq_flags    = 3,
        .seq_count    = seq,
        .data_length  = (uint16_t) (total - CCSDS_PRI_HDR_LEN - 1u),
    };
    pus_tm_hdr sec = {
        .pus_version = PUS_TM_VERSION,
        .service     = service,
        .subtype     = subtype,
        .counter     = seq, /* same convention as the Session 7 HK packet */
    };
    size_t n = ccsds_pack_primary(out, &pri);
    n += pus_pack_secondary(out + n, &sec);
    if (payload_len > 0) { memcpy(out + n, payload, payload_len); }
    return n + payload_len;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

size_t obdh_build_hk(obdh_ctx *c, uint8_t *out, size_t cap) {
    uint8_t pl[OBDH_HK_PAYLOAD_LEN];
    pl[0] = (uint8_t) c->hk_store.count;
    pl[1] = (uint8_t) c->ev_store.count;
    put_be32(pl + 2,  c->hk_store.dropped);
    put_be32(pl + 6,  c->ev_store.dropped);
    put_be32(pl + 10, c->n_drop_unknown);

    size_t n = obdh_pack_tm(out, cap, APID_OBDH_HK, c->hk_seq,
                            PUS_SERVICE_HOUSEKEEPING, PUS_HK_SUBTYPE, pl, sizeof(pl));
    if (n != 0) { c->hk_seq = (uint16_t) ((c->hk_seq + 1u) % 16384u); }
    return n;
}
