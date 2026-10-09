/* labs/04-obdh/obdh_test.c
 *
 * Session 9 Step 4 self checks (assert based, same style as labs/03-tmtc):
 *   A. required scenario: HK out of contact -> stored; TC[17,1] -> dispatcher
 *      + Session 8 verification; unknown APID -> dropped and counted; ground
 *      pass -> stored HK drains in order BEFORE real-time telemetry
 *   B. store unit checks: FIFO, bounded overflow (overwrite oldest) with a
 *      dropped counter, ring wrap-around, oversized packets
 *   C. overflow through the router, and OBDH housekeeping reporting it
 *   D. stretch: events drain before housekeeping; stored packets carry
 *      increasing onboard time and keep order
 *   E. robustness: malformed telemetry, corrupt TC, NULL arguments
 *
 * Owns main() for the Session 9 build (labs/03-tmtc's main is weak).
 */
#include "obdh.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LOG_MAX 64u

/* ---- Fake ground: records everything that is downlinked, in order. ------- */
typedef struct {
    uint8_t pkt[LOG_MAX][MAX_PKT];
    size_t  len[LOG_MAX];
    size_t  n;
} dl_log;

static void record(void *user, const uint8_t *p, size_t n) {
    dl_log *l = user;
    assert(l->n < LOG_MAX && n <= MAX_PKT);
    memcpy(l->pkt[l->n], p, n);
    l->len[l->n] = n;
    l->n++;
}

static uint16_t apid_of(const uint8_t *p) { ccsds_pri_hdr h; ccsds_parse_primary(p, &h); return h.apid; }
static uint16_t seq_of(const uint8_t *p)  { ccsds_pri_hdr h; ccsds_parse_primary(p, &h); return h.seq_count; }
static uint8_t  svc_of(const uint8_t *p)  { return p[CCSDS_PRI_HDR_LEN + 1]; }
static uint8_t  sub_of(const uint8_t *p)  { return p[CCSDS_PRI_HDR_LEN + 2]; }
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void hex(const char *label, const uint8_t *p, size_t n) {
    printf("  %s (%zu bytes): ", label, n);
    for (size_t i = 0; i < n; ++i) { printf("%02X%s", p[i], (i + 1 < n) ? " " : ""); }
    printf("\n");
}

static void print_log(const dl_log *l) {
    for (size_t i = 0; i < l->n; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "dl[%zu]", i);
        hex(label, l->pkt[i], l->len[i]);
    }
}

/* ---- Packet makers (stand-ins for the real producers) -------------------- */
static size_t make_hk(uint8_t *out, uint16_t seq) {
    uint8_t pl[PUS_HK_PAYLOAD_LEN];
    pus_serialize_hk_payload(pl, 28.0f, 80u, 25.0f, 20.0f, 1u);
    size_t n = obdh_pack_tm(out, MAX_PKT, APID_HK, seq, PUS_SERVICE_HOUSEKEEPING,
                            PUS_HK_SUBTYPE, pl, sizeof(pl));
    assert(n == CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + PUS_HK_PAYLOAD_LEN);
    return n;
}

/* Event content is opaque to the router (it routes on APID alone); PUS
 * Service 5 subtype 1 and a 2-byte event id are stand-ins. */
static size_t make_event(uint8_t *out, uint16_t seq) {
    const uint8_t pl[2] = { 0x00, 0x2A };
    size_t n = obdh_pack_tm(out, MAX_PKT, APID_EVENT, seq, 5u, 1u, pl, sizeof(pl));
    assert(n != 0);
    return n;
}

static size_t make_tc(uint8_t *out, uint16_t seq, uint8_t ack, uint8_t svc, uint8_t sub) {
    pus_tc_hdr tc = { .ack_flags = ack, .service = svc, .subtype = sub, .source_id = 1 };
    size_t n = tc_build(out, seq, &tc, NULL, 0);
    assert(n != 0);
    return n;
}

/* ---- Router table --------------------------------------------------------- */
static void check_router(void) {
    assert(obdh_route(APID_HK, 0, 1) == DEST_DOWNLINK);
    assert(obdh_route(APID_HK, 0, 0) == DEST_STORE);
    assert(obdh_route(APID_OBDH_HK, 0, 1) == DEST_DOWNLINK);
    assert(obdh_route(APID_OBDH_HK, 0, 0) == DEST_STORE);
    assert(obdh_route(APID_EVENT, 0, 1) == DEST_DOWNLINK);
    assert(obdh_route(APID_EVENT, 0, 0) == DEST_STORE);
    assert(obdh_route(CCSDS_APID_CMD, 0, 0) == DEST_DOWNLINK);
    assert(obdh_route(CCSDS_APID_CMD, 0, 1) == DEST_DOWNLINK);
    /* any APID, type 1 -> dispatcher, in or out of contact */
    assert(obdh_route(CCSDS_APID_CMD, 1, 0) == DEST_DISPATCH);
    assert(obdh_route(APID_HK, 1, 1) == DEST_DISPATCH);
    assert(obdh_route(0x2AB, 1, 0) == DEST_DISPATCH);
    /* unmapped TM APID -> drop */
    assert(obdh_route(0x2AB, 0, 1) == DEST_DROP);
    assert(obdh_route(0x000, 0, 0) == DEST_DROP);
    printf("PASS router: HK / event / command-TM / TC / unknown destinations, in and out of contact\n\n");
}

/* ---- A. The required scenario (Step 4) ----------------------------------- */
static void check_required_scenario(void) {
    obdh_ctx o; dl_log log = { .n = 0 };
    obdh_init(&o, record, &log, 0 /* out of contact */);
    uint8_t p[MAX_PKT];

    /* Several HK packets while out of contact: all stored, none downlinked. */
    for (uint16_t i = 0; i < 5; ++i) {
        size_t n = make_hk(p, i);
        assert(obdh_ingest(&o, p, n) == DEST_STORE);
    }
    assert(o.hk_store.count == 5 && o.hk_store.dropped == 0);
    assert(log.n == 0 && o.n_downlinked == 0 && o.n_stored == 5);

    /* A TC[17,1] arrives (the ground is talking to us): dispatched, answered. */
    uint8_t tc[TC_MAX_PACKET_LEN];
    size_t tn = make_tc(tc, 5, PUS_ACK_ACCEPT | PUS_ACK_COMPLETE, 17, 1);
    assert(obdh_ingest(&o, tc, tn) == DEST_DISPATCH);
    assert(o.n_dispatched == 1 && o.uplink.executed == 1 && o.uplink.conn_tests == 1);
    assert(log.n == 3); /* verification reports go out at once, via the router */
    assert(svc_of(log.pkt[0]) == 1  && sub_of(log.pkt[0]) == PUS_VER_ACCEPT_OK);
    assert(svc_of(log.pkt[1]) == 17 && sub_of(log.pkt[1]) == PUS_TEST_CONNECTION_TEST_REPORT);
    assert(svc_of(log.pkt[2]) == 1  && sub_of(log.pkt[2]) == PUS_VER_COMPLETE_OK);
    for (size_t i = 0; i < 3; ++i) { assert(apid_of(log.pkt[i]) == CCSDS_APID_CMD); }
    assert(o.hk_store.count == 5); /* stored HK untouched by the command path */

    /* Unknown APID: dropped and counted, nothing stored or sent. */
    uint8_t unk[MAX_PKT];
    size_t un = obdh_pack_tm(unk, sizeof(unk), 0x2AB, 0, 3u, 25u, NULL, 0);
    assert(un != 0);
    assert(obdh_ingest(&o, unk, un) == DEST_DROP);
    assert(o.n_drop_unknown == 1 && log.n == 3 && o.hk_store.count == 5);

    /* Ground pass: stored HK drains in order... */
    obdh_set_contact(&o, 1);
    assert(log.n == 3 + 5 && o.hk_store.count == 0);
    for (uint16_t i = 0; i < 5; ++i) {
        assert(apid_of(log.pkt[3 + i]) == APID_HK && seq_of(log.pkt[3 + i]) == i);
    }
    /* ...BEFORE real-time telemetry resumes. */
    size_t n = make_hk(p, 5);
    assert(obdh_ingest(&o, p, n) == DEST_DOWNLINK);
    assert(log.n == 9 && seq_of(log.pkt[8]) == 5);
    assert(o.hk_store.count == 0 && o.n_stored == 5);

    printf("Case A - required scenario: 5 HK out of contact, TC[17,1], unknown APID, ground pass, 1 real-time HK\n");
    print_log(&log);
    printf("  counters: stored=%u downlinked=%u dispatched=%u dropped_unknown=%u\n",
           o.n_stored, o.n_downlinked, o.n_dispatched, o.n_drop_unknown);
    printf("PASS HK stored while out of contact, drained in order on contact before real time;\n"
           "     TC reached the Session 8 dispatcher and was verified; unknown APID dropped and counted\n\n");
}

/* ---- B. Store unit checks ------------------------------------------------- */
static void check_store(void) {
    packet_store s; uint8_t buf[MAX_PKT], pkt[MAX_PKT]; size_t n; uint32_t stamp;
    store_init(&s);
    assert(store_get(&s, buf, &n) == 0);          /* empty drain returns 0 */

    /* FIFO and stamps. */
    for (uint8_t i = 0; i < 3; ++i) {
        pkt[0] = i; pkt[1] = (uint8_t) (0xA0 + i);
        assert(store_put_at(&s, pkt, 2, 10u + i) == STORE_OK);
    }
    assert(s.count == 3 && s.dropped == 0);
    for (uint8_t i = 0; i < 3; ++i) {
        assert(store_get_at(&s, buf, &n, &stamp) == 1);
        assert(n == 2 && buf[0] == i && buf[1] == 0xA0 + i && stamp == 10u + i);
    }
    assert(s.count == 0 && store_get(&s, buf, &n) == 0);

    /* Fill to capacity: nothing lost yet. */
    for (unsigned i = 0; i < STORE_CAP; ++i) {
        pkt[0] = (uint8_t) i;
        assert(store_put(&s, pkt, 1) == STORE_OK);
    }
    assert(s.count == STORE_CAP && s.dropped == 0);

    /* One more: bounded - oldest (0) overwritten, count capped, loss counted. */
    pkt[0] = (uint8_t) STORE_CAP;
    assert(store_put(&s, pkt, 1) == STORE_OVERWROTE);
    assert(s.count == STORE_CAP && s.dropped == 1);
    pkt[0] = (uint8_t) (STORE_CAP + 1u);
    assert(store_put(&s, pkt, 1) == STORE_OVERWROTE);
    assert(s.count == STORE_CAP && s.dropped == 2);
    for (unsigned i = 0; i < STORE_CAP; ++i) {   /* 2..STORE_CAP+1, in order */
        assert(store_get(&s, buf, &n) == 1 && buf[0] == i + 2u);
    }
    assert(store_get(&s, buf, &n) == 0 && s.dropped == 2); /* counter is cumulative */

    /* Ring wrap-around: many put/get cycles keep order and indices sane. */
    for (unsigned i = 0; i < 1000; ++i) {
        pkt[0] = (uint8_t) i;
        assert(store_put(&s, pkt, 1) == STORE_OK);
        pkt[0] = (uint8_t) (i + 1u);
        assert(store_put(&s, pkt, 1) == STORE_OK);
        assert(store_get(&s, buf, &n) == 1 && buf[0] == (uint8_t) i);
        assert(store_get(&s, buf, &n) == 1 && buf[0] == (uint8_t) (i + 1u));
        assert(s.head < STORE_CAP && s.tail < STORE_CAP && s.count == 0);
    }
    assert(s.dropped == 2);

    /* Size limits: a packet of exactly MAX_PKT fits; MAX_PKT+1 is rejected
     * and counted (a real packet was lost); empty/NULL are rejected quietly. */
    uint8_t big[MAX_PKT + 1];
    for (size_t i = 0; i < sizeof(big); ++i) { big[i] = (uint8_t) i; }
    assert(store_put(&s, big, MAX_PKT) == STORE_OK);
    assert(store_get(&s, buf, &n) == 1 && n == MAX_PKT && memcmp(buf, big, MAX_PKT) == 0);
    assert(store_put(&s, big, MAX_PKT + 1u) == STORE_REJECTED && s.dropped == 3 && s.count == 0);
    assert(store_put(&s, big, 0) == STORE_REJECTED && s.dropped == 3);
    assert(store_put(&s, NULL, 4) == STORE_REJECTED && s.dropped == 3);
    assert(store_put(NULL, big, 4) == STORE_REJECTED);
    printf("PASS store: FIFO, bounded overwrite-oldest with dropped counter, wrap-around, size limits\n\n");
}

/* ---- C. Overflow through the router; loss reported in telemetry ------------ */
static void check_overflow_and_hk(void) {
    obdh_ctx o; dl_log log = { .n = 0 };
    obdh_init(&o, record, &log, 0);
    uint8_t p[MAX_PKT], rep[MAX_PKT];

    /* A long blackout: 20 HK packets into a 16-slot store. */
    for (uint16_t i = 0; i < 20; ++i) {
        size_t n = make_hk(p, i);
        assert(obdh_ingest(&o, p, n) == DEST_STORE);
    }
    assert(o.hk_store.count == STORE_CAP && o.hk_store.dropped == 4);

    /* The loss is visible in OBDH housekeeping, not silent. */
    size_t rn = obdh_build_hk(&o, rep, sizeof(rep));
    assert(rn == CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + OBDH_HK_PAYLOAD_LEN);
    assert(apid_of(rep) == APID_OBDH_HK && svc_of(rep) == PUS_SERVICE_HOUSEKEEPING);
    const uint8_t *pl = rep + CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN;
    assert(pl[0] == STORE_CAP && pl[1] == 0);
    assert(rd32(pl + 2) == 4 && rd32(pl + 6) == 0 && rd32(pl + 10) == 0);
    printf("Case C - 20 HK into a %u-slot store while out of contact\n", STORE_CAP);
    hex("OBDH HK", rep, rn);
    printf("  -> hk_fill=%u ev_fill=%u hk_dropped=%u ev_dropped=%u unknown_apid=%u\n",
           pl[0], pl[1], rd32(pl + 2), rd32(pl + 6), rd32(pl + 10));

    /* On contact the 16 newest survive, in order (0..3 were overwritten). */
    obdh_set_contact(&o, 1);
    assert(log.n == STORE_CAP);
    for (size_t i = 0; i < STORE_CAP; ++i) { assert(seq_of(log.pkt[i]) == 4u + i); }

    /* Counters are cumulative: still reported after the store is drained. */
    rn = obdh_build_hk(&o, rep, sizeof(rep));
    pl = rep + CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN;
    assert(pl[0] == 0 && rd32(pl + 2) == 4);
    assert(seq_of(rep) == 1); /* OBDH HK has its own sequence count */

    /* OBDH HK is routed like any HK: in contact it goes straight down. */
    assert(obdh_ingest(&o, rep, rn) == DEST_DOWNLINK);
    assert(apid_of(log.pkt[log.n - 1]) == APID_OBDH_HK);
    printf("PASS overflow keeps the newest %u, drops are counted and reported in OBDH housekeeping\n\n",
           STORE_CAP);
}

/* ---- D. Stretch goals: event priority and onboard-time stamps -------------- */
static void check_event_priority_and_stamps(void) {
    obdh_ctx o; dl_log log = { .n = 0 };
    obdh_init(&o, record, &log, 0);
    uint8_t p[MAX_PKT];

    /* Interleaved HK and events while out of contact, one tick apart. */
    size_t n;
    obdh_tick(&o); n = make_hk(p, 0);    assert(obdh_ingest(&o, p, n) == DEST_STORE);
    obdh_tick(&o); n = make_event(p, 0); assert(obdh_ingest(&o, p, n) == DEST_STORE);
    obdh_tick(&o); n = make_hk(p, 1);    assert(obdh_ingest(&o, p, n) == DEST_STORE);
    obdh_tick(&o); n = make_event(p, 1); assert(obdh_ingest(&o, p, n) == DEST_STORE);
    obdh_tick(&o); n = make_hk(p, 2);    assert(obdh_ingest(&o, p, n) == DEST_STORE);
    assert(o.hk_store.count == 3 && o.ev_store.count == 2 && o.onboard_time == 5);

    /* Stamps: strictly increasing, in drain order, matching the clock at the
     * time of storing. (Peek via a scratch copy so the contact drain below
     * still has something to send.) */
    packet_store scratch = o.hk_store;
    uint8_t buf[MAX_PKT]; size_t bn; uint32_t st;
    const uint32_t want_hk[3] = { 1, 3, 5 };
    for (unsigned i = 0; i < 3; ++i) {
        assert(store_get_at(&scratch, buf, &bn, &st) == 1);
        assert(st == want_hk[i] && seq_of(buf) == i);
    }
    scratch = o.ev_store;
    const uint32_t want_ev[2] = { 2, 4 };
    for (unsigned i = 0; i < 2; ++i) {
        assert(store_get_at(&scratch, buf, &bn, &st) == 1);
        assert(st == want_ev[i] && seq_of(buf) == i);
    }

    /* On contact: events first (in order), then housekeeping (in order). */
    obdh_set_contact(&o, 1);
    assert(log.n == 5);
    assert(apid_of(log.pkt[0]) == APID_EVENT && seq_of(log.pkt[0]) == 0);
    assert(apid_of(log.pkt[1]) == APID_EVENT && seq_of(log.pkt[1]) == 1);
    for (uint16_t i = 0; i < 3; ++i) {
        assert(apid_of(log.pkt[2 + i]) == APID_HK && seq_of(log.pkt[2 + i]) == i);
    }

    /* In contact an event goes straight down. */
    n = make_event(p, 2);
    assert(obdh_ingest(&o, p, n) == DEST_DOWNLINK && log.n == 6);

    /* A housekeeping flood cannot evict stored events: separate stores. */
    obdh_ctx o2; dl_log log2 = { .n = 0 };
    obdh_init(&o2, record, &log2, 0);
    n = make_event(p, 7); assert(obdh_ingest(&o2, p, n) == DEST_STORE);
    for (uint16_t i = 0; i < 40; ++i) { n = make_hk(p, i); (void) obdh_ingest(&o2, p, n); }
    assert(o2.ev_store.count == 1 && o2.ev_store.dropped == 0 && o2.hk_store.dropped == 40 - STORE_CAP);
    printf("PASS stretch: events drain before housekeeping; stored packets carry increasing onboard time; HK flood cannot evict events\n\n");
}

/* ---- E. Robustness --------------------------------------------------------- */
static void check_robustness(void) {
    obdh_ctx o; dl_log log = { .n = 0 };
    obdh_init(&o, record, &log, 1);
    uint8_t p[MAX_PKT];
    size_t n = make_hk(p, 0);

    /* Unreadable / inconsistent telemetry: dropped, counted, never stored/sent. */
    assert(obdh_ingest(&o, p, 3) == DEST_DROP);          /* too short to have a header */
    assert(obdh_ingest(&o, p, n - 1u) == DEST_DROP);     /* data_length says more      */
    p[n] = 0;
    assert(obdh_ingest(&o, p, n + 1u) == DEST_DROP);     /* data_length says less      */
    assert(obdh_ingest(&o, NULL, 10) == DEST_DROP);
    assert(obdh_ingest(NULL, p, n) == DEST_DROP);
    assert(o.n_drop_malformed == 4 && o.n_drop_unknown == 0);
    assert(log.n == 0 && o.n_stored == 0 && o.n_downlinked == 0);

    /* A TC shorter than a header cannot even be identified: dropped. */
    uint8_t tc[TC_MAX_PACKET_LEN];
    size_t tn = make_tc(tc, 1, PUS_ACK_ACCEPT, 17, 1);
    assert(obdh_ingest(&o, tc, 4) == DEST_DROP && o.n_drop_malformed == 5);

    /* A corrupt TC (CRC) still reaches the Session 8 path - which rejects it
     * with TM[1,2] and executes nothing - instead of being silently dropped. */
    tc[8] ^= 0x01u;
    assert(obdh_ingest(&o, tc, tn) == DEST_DISPATCH);
    assert(o.uplink.executed == 0 && o.uplink.rejected == 1 && o.n_dispatched == 1);
    assert(log.n == 1 && svc_of(log.pkt[0]) == 1 && sub_of(log.pkt[0]) == PUS_VER_ACCEPT_FAIL);
    assert(log.pkt[0][CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + 4] == VFAIL_BAD_CRC);

    /* Unknown APID while out of contact is dropped too (not stored). */
    obdh_set_contact(&o, 0);
    n = obdh_pack_tm(p, sizeof(p), 0x155, 3, 3u, 25u, NULL, 0);
    assert(obdh_ingest(&o, p, n) == DEST_DROP && o.n_drop_unknown == 1 && o.hk_store.count == 0);

    /* Contact transitions: re-asserting contact does not re-drain or duplicate. */
    n = make_hk(p, 9); assert(obdh_ingest(&o, p, n) == DEST_STORE);
    obdh_set_contact(&o, 1);
    size_t after = log.n;
    obdh_set_contact(&o, 1);
    assert(log.n == after && o.hk_store.count == 0);
    printf("PASS robustness: malformed TM dropped and counted; corrupt TC rejected via TM[1,2]; no crashes on NULL\n\n");
}

int main(void) {
    printf("=== Session 9: OBDH core self checks ===\n");
    check_router();
    check_required_scenario();
    check_store();
    check_overflow_and_hk();
    check_event_priority_and_stamps();
    check_robustness();
    printf("all Session 9 OBDH self checks passed\n");
    return 0;
}
