/* labs/05-fdir/fdir_test.c
 *
 * Session 10 Step 4 self checks (assert based, same style as labs 03 / 04):
 *   1. monitors: boundaries, worst-severity combining, source (isolation) mask
 *   2. watchdog: stalled / resumed / never started / wrap-around
 *   3. state machine: detect -> recover -> escalate to SAFE, clear during
 *      recovery, absorbing SAFE, WARN edge, exactly FDIR_MAX_ATTEMPTS actions
 *   4. persistence filter (stretch): a noisy single reading does not act
 *   5. watchdog -> FDIR integration
 *   6. every event routed through the OBDH router: downlinked in contact,
 *      stored out of contact and drained in order on the next pass
 *   7. ground telecommand TC[130,1] exits SAFE through the Session 8 path
 *   8. FDIR housekeeping packet (stretch)
 *   9. two timelines printed: the Session 3 HK profile through FDIR, and a
 *      persistent fault that escalates to SAFE
 *
 * No main() here: labs/04-obdh/obdh_test.c's main() runs the Session 9 checks
 * and then calls fdir_selfcheck(). sample_state()/apply_limits() are the
 * Session 3 functions, linked in from labs/03-tmtc (main.c includes
 * hk_telemetry.c) and used unchanged.
 */
#include "fdir.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LOG_MAX 128u

/* ---- Fake ground: records everything that is downlinked, in order. -------- */
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

static uint16_t rd16(const uint8_t *p) { return (uint16_t) (((uint16_t) p[0] << 8) | p[1]); }
static uint16_t apid_of(const uint8_t *p) { ccsds_pri_hdr h; ccsds_parse_primary(p, &h); return h.apid; }
static uint16_t seq_of(const uint8_t *p)  { ccsds_pri_hdr h; ccsds_parse_primary(p, &h); return h.seq_count; }

/* Packet field accessors: primary header 6 B, PUS TM header 5 B, payload after. */
#define SVC(p)   ((p)[CCSDS_PRI_HDR_LEN + 1])
#define SUB(p)   ((p)[CCSDS_PRI_HDR_LEN + 2])
#define PL(p)    ((p) + CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN)
#define EV_ID(p)     rd16(PL(p))
#define EV_STATE(p)  (PL(p)[2])
#define EV_ATT(p)    (PL(p)[3])
#define EV_WORST(p)  (PL(p)[4])
#define EV_SRC(p)    (PL(p)[5])
#define EV_TASKS(p)  (PL(p)[6])

static int is_event(const uint8_t *p) { return apid_of(p) == APID_EVENT && SVC(p) == PUS_SERVICE_EVENT; }

static const char *state_name(fdir_state_t s) {
    return s == FDIR_NOMINAL ? "NOMINAL" : s == FDIR_RECOVERING ? "RECOVERING" : "SAFE";
}
static const char *health_name(health_t h) {
    return h == HEALTH_OK ? "OK" : h == HEALTH_WARN ? "WARN" : "FAULT";
}

static void hex(const char *label, const uint8_t *p, size_t n) {
    printf("  %s (%zu bytes): ", label, n);
    for (size_t i = 0; i < n; ++i) { printf("%02X%s", p[i], (i + 1 < n) ? " " : ""); }
    printf("\n");
}

/* ---- Test harness: one full system (OBDH + FDIR + watchdog + state) --------- */
typedef struct {
    obdh_ctx         o;
    dl_log           log;
    fdir_ctx         f;
    watchdog_t       wd;
    spacecraft_state s;
    int              silent_task;       /* task that does NOT kick this cycle, -1 = none */
    int              n_recover;
    recovery_action_t actions[16];
    uint8_t          rec_sources[16];
    int              n_safe;
    uint8_t          safe_sources;
} hn;

static void hook_recover(void *u, recovery_action_t a, uint8_t src) {
    hn *h = u;
    assert(h->n_recover < 16);
    h->actions[h->n_recover]     = a;
    h->rec_sources[h->n_recover] = src;
    h->n_recover++;
}
static void hook_safe(void *u, uint8_t src) {
    hn *h = u;
    h->n_safe++;
    h->safe_sources = src;
}

/* confirm == 0 keeps the library default (FDIR_CONFIRM_CYCLES). */
static void hn_init(hn *h, int in_contact, unsigned confirm) {
    memset(h, 0, sizeof(*h));
    obdh_init(&h->o, record, &h->log, in_contact);
    fdir_hooks hooks = { .recover = hook_recover, .enter_safe = hook_safe, .user = h };
    fdir_init(&h->f, &h->o, &hooks);
    fdir_attach_uplink(&h->f);
    if (confirm != 0u) { h->f.confirm_cycles = (uint8_t) confirm; }
    wd_init(&h->wd);
    h->s = (spacecraft_state) { .bus_voltage = 8.0f, .battery_pct = 80, .temp_obc = 25.0f,
                                .temp_batt = 20.0f, .mode = MODE_BOOT };
    h->silent_task = -1;
}

/* One FDIR cycle: every task kicks (except the silent one), then FDIR runs. */
static fdir_state_t hn_cycle_state(hn *h) {
    for (int t = 0; t < N_TASKS; ++t) {
        if (t != h->silent_task) { wd_kick(&h->wd, t); }
    }
    fdir_state_t st = fdir_cycle(&h->f, &h->s, &h->wd);
    assert(h->s.mode == (st == FDIR_SAFE ? MODE_SAFE : MODE_NOMINAL)); /* mode follows FDIR */
    return st;
}
static fdir_state_t hn_cycle(hn *h, float volts) {
    h->s.bus_voltage = volts;
    return hn_cycle_state(h);
}

/* Drives a persistent undervoltage until SAFE; returns the cycles it took. */
static unsigned hn_run_to_safe(hn *h) {
    unsigned n = 0;
    while (h->f.state != FDIR_SAFE) {
        (void) hn_cycle(h, 5.0f);
        assert(++n < 32u);
    }
    return n;
}

/* ---- 1. Monitors ------------------------------------------------------------ */
static void check_monitors(void) {
    spacecraft_state s = { .bus_voltage = 8.0f, .battery_pct = 80, .temp_obc = 25.0f,
                           .temp_batt = 20.0f, .mode = MODE_NOMINAL };

    /* Power: strict comparisons, matching apply_limits (exactly V_FAULT is WARN). */
    const struct { float v; health_t h; } pw[] = {
        { 8.10f, HEALTH_OK }, { 7.00f, HEALTH_OK }, { 6.99f, HEALTH_WARN },
        { 6.00f, HEALTH_WARN }, { 5.99f, HEALTH_FAULT }, { 0.0f, HEALTH_FAULT },
    };
    for (size_t i = 0; i < sizeof(pw) / sizeof(pw[0]); ++i) {
        s.bus_voltage = pw[i].v;
        assert(monitor_power(&s) == pw[i].h);
    }
    s.bus_voltage = 8.0f;

    /* Thermal: OBC high side, battery high and low side. */
    const struct { float obc, batt; health_t h; } th[] = {
        { 25.0f, 20.0f, HEALTH_OK },
        { 60.0f, 20.0f, HEALTH_OK },   { 60.1f, 20.0f, HEALTH_WARN },
        { 75.0f, 20.0f, HEALTH_WARN }, { 75.1f, 20.0f, HEALTH_FAULT },
        { 25.0f, 40.0f, HEALTH_OK },   { 25.0f, 40.1f, HEALTH_WARN },
        { 25.0f, 50.0f, HEALTH_WARN }, { 25.0f, 50.1f, HEALTH_FAULT },
        { 25.0f, 0.0f, HEALTH_OK },    { 25.0f, -0.1f, HEALTH_WARN },
        { 25.0f, -10.0f, HEALTH_WARN },{ 25.0f, -10.1f, HEALTH_FAULT },
        { 61.0f, 51.0f, HEALTH_FAULT }, /* worst of the two wins inside the monitor */
    };
    for (size_t i = 0; i < sizeof(th) / sizeof(th[0]); ++i) {
        s.temp_obc = th[i].obc;
        s.temp_batt = th[i].batt;
        assert(monitor_thermal(&s) == th[i].h);
    }
    s.temp_obc = 25.0f; s.temp_batt = 20.0f;

    /* Battery state of charge. */
    const struct { uint8_t pct; health_t h; } bt[] = {
        { 100, HEALTH_OK }, { 30, HEALTH_OK }, { 29, HEALTH_WARN },
        { 15, HEALTH_WARN }, { 14, HEALTH_FAULT }, { 0, HEALTH_FAULT },
    };
    for (size_t i = 0; i < sizeof(bt) / sizeof(bt[0]); ++i) {
        s.battery_pct = bt[i].pct;
        assert(monitor_battery(&s) == bt[i].h);
    }
    s.battery_pct = 80;

    /* Worst severity wins, and the source mask names the monitors AT that level. */
    uint8_t src;
    assert(health_worst(HEALTH_OK, HEALTH_WARN) == HEALTH_WARN);
    assert(health_worst(HEALTH_FAULT, HEALTH_WARN) == HEALTH_FAULT);
    assert(fdir_evaluate(&s, HEALTH_OK, &src) == HEALTH_OK && src == 0);

    s.bus_voltage = 6.5f;                       /* power WARN only */
    assert(fdir_evaluate(&s, HEALTH_OK, &src) == HEALTH_WARN && src == FDIR_SRC_POWER);
    s.temp_obc = 80.0f;                         /* + thermal FAULT: FAULT wins, power drops out */
    assert(fdir_evaluate(&s, HEALTH_OK, &src) == HEALTH_FAULT && src == FDIR_SRC_THERMAL);
    s.battery_pct = 10;                         /* + battery FAULT: two sources at FAULT */
    assert(fdir_evaluate(&s, HEALTH_OK, &src) == HEALTH_FAULT &&
           src == (FDIR_SRC_THERMAL | FDIR_SRC_BATTERY));
    s = (spacecraft_state) { .bus_voltage = 8.0f, .battery_pct = 80, .temp_obc = 25.0f,
                             .temp_batt = 20.0f, .mode = MODE_NOMINAL };
    assert(fdir_evaluate(&s, HEALTH_FAULT, &src) == HEALTH_FAULT && src == FDIR_SRC_WATCHDOG);
    assert(fdir_evaluate(&s, HEALTH_FAULT, NULL) == HEALTH_FAULT); /* NULL mask is allowed */
    printf("PASS monitors: power/thermal/battery boundaries, worst severity wins, source mask isolates the cause\n\n");
}

/* ---- 2. Watchdog ------------------------------------------------------------ */
static void check_watchdog(void) {
    watchdog_t w;
    uint8_t m;
    memset(&w, 0, sizeof(w));

    /* Tasks that never started: the first check after init flags all of them. */
    wd_init(&w);
    assert(watchdog_check(&w, &m) == HEALTH_FAULT && m == ((1u << N_TASKS) - 1u));

    /* Healthy: everyone checked in since the last check. */
    wd_init(&w);
    for (int t = 0; t < N_TASKS; ++t) { wd_kick(&w, t); }
    assert(watchdog_check(&w, &m) == HEALTH_OK && m == 0);

    /* One task stops: exactly that task is named. */
    wd_kick(&w, TASK_SAMPLER);
    wd_kick(&w, TASK_REPORTER);
    assert(watchdog_check(&w, &m) == HEALTH_FAULT && m == (1u << TASK_TELEMETRY));

    /* Nobody kicks for an interval: all stalled. */
    assert(watchdog_check(&w, &m) == HEALTH_FAULT && m == ((1u << N_TASKS) - 1u));

    /* The task resumes: the very next check is clean again. */
    for (int t = 0; t < N_TASKS; ++t) { wd_kick(&w, t); }
    assert(watchdog_check(&w, &m) == HEALTH_OK && m == 0);
    assert(watchdog_check(&w, NULL) == HEALTH_FAULT); /* NULL mask allowed */

    /* Counter wrap-around is harmless: only inequality is tested. */
    memset(&w, 0, sizeof(w));
    w.heartbeat[TASK_SAMPLER] = 0xFFFFFFFFu;
    wd_init(&w);
    for (int t = 0; t < N_TASKS; ++t) { wd_kick(&w, t); }
    assert(w.heartbeat[TASK_SAMPLER] == 0u);
    assert(watchdog_check(&w, &m) == HEALTH_OK && m == 0);

    /* An invalid task id is ignored, not a memory write. */
    uint32_t before[N_TASKS];
    for (int t = 0; t < N_TASKS; ++t) { before[t] = w.heartbeat[t]; }
    wd_kick(&w, -1);
    wd_kick(&w, N_TASKS);
    for (int t = 0; t < N_TASKS; ++t) { assert(w.heartbeat[t] == before[t]); }
    printf("PASS watchdog: stalled task named, resume clears, never-started flagged, wrap-around and bad id safe\n\n");
}

/* ---- 3. State machine -------------------------------------------------------- */
static void check_state_machine(void) {
    hn h;

    /* A. Persistent fault: detect -> recover x MAX -> SAFE. (confirm = 1) */
    hn_init(&h, 1, 1);
    assert(hn_cycle(&h, 8.0f) == FDIR_NOMINAL && h.f.n_events == 0);

    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);               /* detection */
    assert(h.log.n == 1 && is_event(h.log.pkt[0]));
    assert(EV_ID(h.log.pkt[0]) == EVT_FAULT && SUB(h.log.pkt[0]) == 3);  /* medium severity */
    assert(EV_STATE(h.log.pkt[0]) == FDIR_RECOVERING && EV_ATT(h.log.pkt[0]) == 0);
    assert(EV_WORST(h.log.pkt[0]) == HEALTH_FAULT && EV_SRC(h.log.pkt[0]) == FDIR_SRC_POWER);
    assert(h.n_recover == 0);                                     /* no action on the detection cycle */

    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);               /* rung 1: retry */
    assert(h.n_recover == 1 && h.actions[0] == RECOVERY_RETRY && h.rec_sources[0] == FDIR_SRC_POWER);
    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);               /* rung 2: reset */
    assert(h.n_recover == 2 && h.actions[1] == RECOVERY_RESET_DEVICE);
    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);               /* rung 3: redundant unit */
    assert(h.n_recover == 3 && h.actions[2] == RECOVERY_SWITCH_REDUNDANT);
    assert(h.n_safe == 0 && h.s.mode == MODE_NOMINAL);            /* still not safe: last attempt not yet judged */

    assert(hn_cycle(&h, 5.0f) == FDIR_SAFE);                     /* all attempts made AND seen to fail */
    assert(h.n_safe == 1 && h.safe_sources == FDIR_SRC_POWER && h.s.mode == MODE_SAFE);
    assert(h.f.n_safe_entries == 1);
    /* No off-by-one: exactly FDIR_MAX_ATTEMPTS recovery actions were executed. */
    assert(h.n_recover == (int) FDIR_MAX_ATTEMPTS);

    const uint16_t want_ids[5] = { EVT_FAULT, EVT_RECOVERY_ACTION, EVT_RECOVERY_ACTION,
                                   EVT_RECOVERY_ACTION, EVT_SAFE_ENTER };
    const uint8_t  want_sub[5] = { 3, 1, 1, 1, 4 };
    assert(h.log.n == 5);
    for (size_t i = 0; i < 5; ++i) {
        assert(EV_ID(h.log.pkt[i]) == want_ids[i] && SUB(h.log.pkt[i]) == want_sub[i]);
        assert(seq_of(h.log.pkt[i]) == i);                        /* consecutive sequence counts */
    }
    assert(EV_STATE(h.log.pkt[4]) == FDIR_SAFE && EV_ATT(h.log.pkt[4]) == FDIR_MAX_ATTEMPTS);
    assert(EV_ATT(h.log.pkt[1]) == 1 && EV_ATT(h.log.pkt[2]) == 2 && EV_ATT(h.log.pkt[3]) == 3);

    /* SAFE is absorbing: a healthy bus does not leave it, and nothing more is reported. */
    for (int i = 0; i < 5; ++i) {
        assert(hn_cycle(&h, 8.0f) == FDIR_SAFE && h.s.mode == MODE_SAFE);
    }
    assert(h.log.n == 5 && h.n_recover == 3 && h.n_safe == 1);

    /* B. Clears during recovery -> NOMINAL with a recovery event, no safe mode. */
    hn_init(&h, 1, 1);
    (void) hn_cycle(&h, 5.0f);
    (void) hn_cycle(&h, 5.0f);                                    /* rung 1 */
    assert(hn_cycle(&h, 8.0f) == FDIR_NOMINAL && h.s.mode == MODE_NOMINAL);
    assert(h.log.n == 3);
    assert(EV_ID(h.log.pkt[2]) == EVT_RECOVERED && SUB(h.log.pkt[2]) == 2);
    assert(EV_STATE(h.log.pkt[2]) == FDIR_NOMINAL && EV_ATT(h.log.pkt[2]) == 1);
    assert(EV_SRC(h.log.pkt[2]) == FDIR_SRC_POWER);               /* the episode's cause is still reported */
    assert(h.n_recover == 1 && h.n_safe == 0 && h.f.n_safe_entries == 0);
    assert(h.f.source_mask == 0 && h.f.attempts == 1);            /* mask cleared, attempts kept for HK */
    /* ...and a later fault starts a fresh episode with the counter reset. */
    (void) hn_cycle(&h, 5.0f);
    assert(h.f.state == FDIR_RECOVERING && h.f.attempts == 0 && h.f.n_faults == 2);

    /* C. The cycle on which attempts run out: a fault that clears is believed. */
    hn_init(&h, 1, 1);
    for (int i = 0; i < 4; ++i) { (void) hn_cycle(&h, 5.0f); }    /* detect + 3 rungs */
    assert(h.f.attempts == FDIR_MAX_ATTEMPTS && h.f.state == FDIR_RECOVERING);
    assert(hn_cycle(&h, 8.0f) == FDIR_NOMINAL && h.n_safe == 0);
    assert(EV_ID(h.log.pkt[h.log.n - 1]) == EVT_RECOVERED && EV_ATT(h.log.pkt[h.log.n - 1]) == FDIR_MAX_ATTEMPTS);

    /* D. WARN is proportionate: one event on the OK -> WARN edge, no action. */
    hn_init(&h, 1, 1);
    (void) hn_cycle(&h, 8.0f);
    assert(hn_cycle(&h, 6.5f) == FDIR_NOMINAL);
    assert(h.log.n == 1 && EV_ID(h.log.pkt[0]) == EVT_WARN && SUB(h.log.pkt[0]) == 2);
    assert(EV_WORST(h.log.pkt[0]) == HEALTH_WARN);
    (void) hn_cycle(&h, 6.5f);
    (void) hn_cycle(&h, 6.5f);
    assert(h.log.n == 1);                                         /* staying in WARN is silent */
    (void) hn_cycle(&h, 8.0f);
    assert(hn_cycle(&h, 6.5f) == FDIR_NOMINAL && h.log.n == 2);   /* a new edge is reported again */
    assert(h.n_recover == 0);

    /* E. WARN during recovery is not "recovered": it still costs an attempt. */
    hn_init(&h, 1, 1);
    (void) hn_cycle(&h, 5.0f);                                    /* detect */
    assert(hn_cycle(&h, 6.5f) == FDIR_RECOVERING && h.n_recover == 1);
    assert(hn_cycle(&h, 8.0f) == FDIR_NOMINAL);

    /* F. Isolation: the cause latched at detection grows if another monitor joins. */
    hn_init(&h, 1, 1);
    h.s.temp_obc = 80.0f;                                         /* thermal fault only */
    (void) hn_cycle(&h, 8.0f);
    assert(h.f.state == FDIR_RECOVERING && h.f.source_mask == FDIR_SRC_THERMAL);
    assert(EV_SRC(h.log.pkt[0]) == FDIR_SRC_THERMAL);
    h.s.battery_pct = 5;                                          /* battery joins */
    (void) hn_cycle(&h, 8.0f);
    assert(h.rec_sources[0] == (FDIR_SRC_THERMAL | FDIR_SRC_BATTERY));
    printf("PASS state machine: detect -> recover x%u -> SAFE; clear -> NOMINAL; SAFE absorbing; WARN edge; "
           "no off-by-one in attempts\n\n", (unsigned) FDIR_MAX_ATTEMPTS);
}

/* ---- 4. Persistence filter ---------------------------------------------------- */
static void check_persistence(void) {
    hn h;
    hn_init(&h, 1, 0);                                            /* library default */
    assert(h.f.confirm_cycles == FDIR_CONFIRM_CYCLES && FDIR_CONFIRM_CYCLES == 2u);

    /* Isolated noisy readings never act. */
    const float noisy[] = { 8.0f, 5.0f, 8.0f, 5.0f, 8.0f };
    for (size_t i = 0; i < sizeof(noisy) / sizeof(noisy[0]); ++i) {
        assert(hn_cycle(&h, noisy[i]) == FDIR_NOMINAL);
    }
    assert(h.log.n == 0 && h.f.n_filtered == 2 && h.f.n_faults == 0);

    /* FAULT, WARN, FAULT is not "two consecutive" either. */
    (void) hn_cycle(&h, 5.0f);
    (void) hn_cycle(&h, 6.5f);
    assert(hn_cycle(&h, 5.0f) == FDIR_NOMINAL);
    (void) hn_cycle(&h, 8.0f);
    assert(h.log.n == 0 && h.f.n_filtered == 4);

    /* Two in a row: confirmed on the second. */
    assert(hn_cycle(&h, 5.0f) == FDIR_NOMINAL);
    assert(h.f.n_filtered == 5);
    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);
    assert(h.log.n == 1 && EV_ID(h.log.pkt[0]) == EVT_FAULT && h.f.n_faults == 1);

    /* With confirm = 1 the same single reading acts at once; 0 behaves as 1. */
    hn_init(&h, 1, 1);
    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);
    hn_init(&h, 1, 1);
    h.f.confirm_cycles = 0;
    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);

    /* Confirmation delays detection by exactly (confirm - 1) cycles, no more. */
    hn_init(&h, 1, 0);
    unsigned cyc = hn_run_to_safe(&h);
    assert(cyc == FDIR_CONFIRM_CYCLES + 1u /* detect */ + FDIR_MAX_ATTEMPTS + 1u /* judge last attempt */ - 1u);
    printf("PASS persistence: isolated/non-consecutive noise rejected and counted, 2 in a row acts; "
           "persistent fault reaches SAFE in %u cycles\n\n", cyc);
}

/* ---- 5. Watchdog -> FDIR ---------------------------------------------------------- */
static void check_watchdog_integration(void) {
    hn h;
    hn_init(&h, 1, 1);
    (void) hn_cycle(&h, 8.0f);
    (void) hn_cycle(&h, 8.0f);
    assert(h.f.state == FDIR_NOMINAL && h.log.n == 0);

    h.silent_task = TASK_SAMPLER;                                 /* the sampler hangs */
    assert(hn_cycle_state(&h) == FDIR_RECOVERING);
    assert(h.log.n == 1 && EV_ID(h.log.pkt[0]) == EVT_FAULT);
    assert(EV_SRC(h.log.pkt[0]) == FDIR_SRC_WATCHDOG);            /* isolated to liveness, not power */
    assert(EV_TASKS(h.log.pkt[0]) == (1u << TASK_SAMPLER));       /* ...and to WHICH task */
    assert(hn_cycle_state(&h) == FDIR_RECOVERING && h.n_recover == 1);
    assert(h.rec_sources[0] == FDIR_SRC_WATCHDOG);

    h.silent_task = -1;                                           /* task comes back after the retry */
    assert(hn_cycle_state(&h) == FDIR_NOMINAL);
    assert(EV_ID(h.log.pkt[h.log.n - 1]) == EVT_RECOVERED);
    assert(EV_TASKS(h.log.pkt[h.log.n - 1]) == (1u << TASK_SAMPLER)); /* the episode's cause survives to the end */
    assert(h.f.task_mask == 0);                                   /* and is cleared afterwards */

    /* A task that stays dead escalates exactly like a power fault. */
    hn_init(&h, 1, 1);
    (void) hn_cycle(&h, 8.0f);
    h.silent_task = TASK_TELEMETRY;
    unsigned n = 0;
    while (h.f.state != FDIR_SAFE) { (void) hn_cycle_state(&h); assert(++n < 32u); }
    assert(n == 1u + FDIR_MAX_ATTEMPTS + 1u && h.s.mode == MODE_SAFE);
    assert(h.safe_sources == FDIR_SRC_WATCHDOG);
    assert(EV_TASKS(h.log.pkt[h.log.n - 1]) == (1u << TASK_TELEMETRY));   /* SAFE_ENTER names the task too */
    /* A power fault involves no task: the field stays 0. */
    hn_init(&h, 1, 1);
    (void) hn_cycle(&h, 5.0f);
    assert(EV_TASKS(h.log.pkt[0]) == 0);
    printf("PASS watchdog -> FDIR: a stalled task is detected, isolated to WATCHDOG, retried, and escalates if it stays dead\n\n");
}

/* ---- 6. Events through the OBDH router --------------------------------------------- */
static void check_events_via_obdh(void) {
    hn h;

    /* In contact: every FDIR event reaches the downlink, well-formed. */
    hn_init(&h, 1, 1);
    (void) hn_run_to_safe(&h);
    assert(h.log.n == 5 && h.o.n_downlinked == 5 && h.o.n_stored == 0);
    assert(h.f.n_events == 5 && h.f.n_event_lost == 0);
    printf("Case 6 - the five events of one escalation, as downlinked (APID 0x066, Service 5)\n");
    for (size_t i = 0; i < h.log.n; ++i) {
        const uint8_t *p = h.log.pkt[i];
        ccsds_pri_hdr hd;
        ccsds_parse_primary(p, &hd);
        assert(hd.type == 0 && hd.sec_hdr_flag == 1 && hd.apid == APID_EVENT && hd.seq_flags == 3);
        assert((size_t) hd.data_length + 1u + CCSDS_PRI_HDR_LEN == h.log.len[i]);
        assert(h.log.len[i] == CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + FDIR_EVT_PAYLOAD_LEN);
        assert(SVC(p) == PUS_SERVICE_EVENT);
        char label[40];
        snprintf(label, sizeof(label), "%-15s TM[5,%u]", fdir_event_name(EV_ID(p)), SUB(p));
        hex(label, p, h.log.len[i]);
    }

    /* Out of contact: the events are STORED (event store), nothing is lost,
     * and the next pass forwards them in order. */
    hn_init(&h, 0, 1);
    (void) hn_run_to_safe(&h);
    assert(h.log.n == 0 && h.o.n_downlinked == 0);
    assert(h.o.ev_store.count == 5 && h.o.ev_store.dropped == 0 && h.o.hk_store.count == 0);
    assert(h.o.n_stored == 5 && h.f.n_event_lost == 0);
    obdh_set_contact(&h.o, 1);
    assert(h.log.n == 5 && h.o.ev_store.count == 0);
    const uint16_t want[5] = { EVT_FAULT, EVT_RECOVERY_ACTION, EVT_RECOVERY_ACTION,
                               EVT_RECOVERY_ACTION, EVT_SAFE_ENTER };
    for (size_t i = 0; i < 5; ++i) {
        assert(is_event(h.log.pkt[i]) && EV_ID(h.log.pkt[i]) == want[i] && seq_of(h.log.pkt[i]) == i);
    }

    /* An FDIR event does not displace stored housekeeping, and drains first. */
    hn_init(&h, 0, 1);
    uint8_t hk[MAX_PKT];
    uint8_t pl[PUS_HK_PAYLOAD_LEN];
    pus_serialize_hk_payload(pl, 28.0f, 80u, 25.0f, 20.0f, 1u);
    size_t hn_ = obdh_pack_tm(hk, sizeof(hk), APID_HK, 0, PUS_SERVICE_HOUSEKEEPING, PUS_HK_SUBTYPE, pl, sizeof(pl));
    assert(obdh_ingest(&h.o, hk, hn_) == DEST_STORE);
    (void) hn_cycle(&h, 5.0f);                                    /* FAULT event stored too */
    assert(h.o.hk_store.count == 1 && h.o.ev_store.count == 1);
    obdh_set_contact(&h.o, 1);
    assert(h.log.n == 2 && is_event(h.log.pkt[0]) && apid_of(h.log.pkt[1]) == APID_HK);
    printf("PASS events: every FDIR transition and action goes through obdh_ingest(): downlinked in contact, "
           "stored out of contact and forwarded first on the next pass\n\n");
}

/* ---- 7. Ground telecommand exits SAFE ---------------------------------------------- */
static size_t make_exit_tc(uint8_t *out, uint16_t seq, uint8_t ack, uint8_t sub) {
    pus_tc_hdr tc = { .ack_flags = ack, .service = CMD_SERVICE_FDIR_CTRL, .subtype = sub, .source_id = 1 };
    size_t n = tc_build(out, seq, &tc, NULL, 0);
    assert(n != 0);
    return n;
}
static int is_verif(const uint8_t *p, uint8_t sub) { return SVC(p) == 1 && SUB(p) == sub; }

static void check_ground_exit(void) {
    hn h;
    uint8_t tc[TC_MAX_PACKET_LEN];
    size_t tn;
    const uint8_t ACK = PUS_ACK_ACCEPT | PUS_ACK_COMPLETE;

    /* Exit from SAFE: executed by FDIR, event + verification reports downlinked. */
    hn_init(&h, 1, 1);
    (void) hn_run_to_safe(&h);
    size_t mark = h.log.n;
    tn = make_exit_tc(tc, 1, ACK, CMD_EXIT_SAFE_MODE);
    assert(obdh_ingest(&h.o, tc, tn) == DEST_DISPATCH);
    assert(h.f.state == FDIR_NOMINAL && h.o.uplink.executed == 1);
    assert(h.log.n == mark + 3);
    /* The event is raised while the command executes, so it precedes the
     * acceptance report, which the Session 8 path delivers after the handler. */
    assert(is_event(h.log.pkt[mark]) && EV_ID(h.log.pkt[mark]) == EVT_SAFE_EXIT);
    assert(SUB(h.log.pkt[mark]) == 1 && EV_STATE(h.log.pkt[mark]) == FDIR_NOMINAL);
    assert(is_verif(h.log.pkt[mark + 1], PUS_VER_ACCEPT_OK));
    assert(is_verif(h.log.pkt[mark + 2], PUS_VER_COMPLETE_OK));
    assert(hn_cycle(&h, 8.0f) == FDIR_NOMINAL && h.s.mode == MODE_NOMINAL);
    assert(h.f.n_safe_entries == 1);

    /* Exit while NOT in safe mode is refused, visibly: TM[1,8], no event, no state change. */
    mark = h.log.n;
    tn = make_exit_tc(tc, 2, ACK, CMD_EXIT_SAFE_MODE);
    assert(obdh_ingest(&h.o, tc, tn) == DEST_DISPATCH);
    assert(h.log.n == mark + 2 && h.f.state == FDIR_NOMINAL);
    assert(is_verif(h.log.pkt[mark], PUS_VER_ACCEPT_OK));
    assert(is_verif(h.log.pkt[mark + 1], PUS_VER_COMPLETE_FAIL));
    assert(h.log.pkt[mark + 1][CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + 4] == VFAIL_EXEC_ERROR);

    /* Command layer not wired to any FDIR: also a visible failure, not a crash. */
    obdh_ctx lone; dl_log lone_log = { .n = 0 };
    obdh_init(&lone, record, &lone_log, 1);
    tn = make_exit_tc(tc, 3, ACK, CMD_EXIT_SAFE_MODE);
    assert(obdh_ingest(&lone, tc, tn) == DEST_DISPATCH);
    assert(lone_log.n == 2 && is_verif(lone_log.pkt[1], PUS_VER_COMPLETE_FAIL));

    /* A corrupt or unknown command never moves FDIR out of SAFE. */
    hn_init(&h, 1, 1);
    (void) hn_run_to_safe(&h);
    mark = h.log.n;
    tn = make_exit_tc(tc, 4, ACK, CMD_EXIT_SAFE_MODE);
    tc[CCSDS_PRI_HDR_LEN + 2] ^= 0x01u;                           /* flip a bit inside the packet */
    assert(obdh_ingest(&h.o, tc, tn) == DEST_DISPATCH);
    assert(h.f.state == FDIR_SAFE && h.log.n == mark + 1 && is_verif(h.log.pkt[mark], PUS_VER_ACCEPT_FAIL));
    tn = make_exit_tc(tc, 5, ACK, 2 /* no such subtype */);
    assert(obdh_ingest(&h.o, tc, tn) == DEST_DISPATCH);
    assert(h.f.state == FDIR_SAFE && is_verif(h.log.pkt[h.log.n - 1], PUS_VER_ACCEPT_FAIL));
    assert(h.o.uplink.executed == 0);

    /* The ground's exit does not hide a fault that is still there: it is
     * re-detected through the normal path and a NEW episode begins. */
    hn_init(&h, 1, 0);                                            /* default: confirm = 2 */
    (void) hn_run_to_safe(&h);
    tn = make_exit_tc(tc, 6, ACK, CMD_EXIT_SAFE_MODE);
    assert(obdh_ingest(&h.o, tc, tn) == DEST_DISPATCH && h.f.state == FDIR_NOMINAL);
    assert(hn_cycle(&h, 5.0f) == FDIR_NOMINAL);                   /* first reading: filtered */
    assert(hn_cycle(&h, 5.0f) == FDIR_RECOVERING);                /* second: confirmed again */
    assert(h.f.n_faults == 2 && h.f.n_safe_entries == 1);
    assert(EV_ID(h.log.pkt[h.log.n - 1]) == EVT_FAULT);
    printf("PASS ground exit: TC[130,1] leaves SAFE via the Session 8 path (event + TM[1,x]); refused/corrupt/unknown "
           "commands leave state alone; a persisting fault is re-detected\n\n");
}

/* ---- 8. FDIR housekeeping ------------------------------------------------------------ */
static void check_fdir_hk(void) {
    hn h;
    uint8_t rep[MAX_PKT];

    assert(obdh_route(APID_FDIR_HK, 0, 1) == DEST_DOWNLINK);
    assert(obdh_route(APID_FDIR_HK, 0, 0) == DEST_STORE);

    hn_init(&h, 1, 0);                                            /* confirm = 2 */
    (void) hn_cycle(&h, 5.0f);                                    /* one filtered reading */
    (void) hn_run_to_safe(&h);
    size_t n = fdir_build_hk(&h.f, rep, sizeof(rep));
    assert(n == CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + FDIR_HK_PAYLOAD_LEN);
    assert(apid_of(rep) == APID_FDIR_HK && SVC(rep) == PUS_SERVICE_HOUSEKEEPING && seq_of(rep) == 0);
    const uint8_t *pl = PL(rep);
    assert(pl[0] == FDIR_SAFE && pl[1] == FDIR_MAX_ATTEMPTS && pl[2] == HEALTH_FAULT && pl[3] == FDIR_SRC_POWER);
    assert(rd16(pl + 4) == 1 && rd16(pl + 6) == 1 && rd16(pl + 8) == h.f.n_filtered);
    printf("Case 8 - FDIR housekeeping in SAFE\n");
    hex("FDIR HK", rep, n);
    printf("  -> state=%s attempts=%u last_worst=%s sources=0x%02X faults=%u safe_entries=%u filtered=%u\n",
           state_name((fdir_state_t) pl[0]), pl[1], health_name((health_t) pl[2]), pl[3],
           rd16(pl + 4), rd16(pl + 6), rd16(pl + 8));

    size_t mark = h.log.n;
    assert(obdh_ingest(&h.o, rep, n) == DEST_DOWNLINK && h.log.n == mark + 1);
    assert(apid_of(h.log.pkt[mark]) == APID_FDIR_HK);
    n = fdir_build_hk(&h.f, rep, sizeof(rep));
    assert(seq_of(rep) == 1);                                     /* its own sequence count */
    assert(fdir_build_hk(&h.f, rep, 8) == 0 && h.f.hk_seq == 2);  /* too small: nothing built, count kept */

    hn_init(&h, 0, 1);                                            /* out of contact: stored like any HK */
    n = fdir_build_hk(&h.f, rep, sizeof(rep));
    assert(obdh_ingest(&h.o, rep, n) == DEST_STORE && h.o.hk_store.count == 1);
    obdh_set_contact(&h.o, 1);
    assert(h.log.n == 1 && apid_of(h.log.pkt[0]) == APID_FDIR_HK);
    printf("PASS FDIR housekeeping: state, attempts, severity, sources and counters, routed like any HK\n\n");
}

/* ---- 9. Timelines ------------------------------------------------------------------------ */
static void print_cycle(const hn *h, unsigned cyc, float v, health_t raw, size_t ev_before) {
    printf("  cyc=%02u bus=%.2fV raw=%-5s state=%-10s attempts=%u mode=%-7s",
           cyc, (double) v, health_name(raw), state_name(h->f.state), h->f.attempts,
           h->s.mode == MODE_SAFE ? "SAFE" : "NOMINAL");
    for (size_t i = ev_before; i < h->log.n; ++i) {
        printf(" [%s TM[5,%u]]", fdir_event_name(EV_ID(h->log.pkt[i])), SUB(h->log.pkt[i]));
    }
    printf("\n");
}

static void scenario_session3_profile(void) {
    /* The Session 3 HK profile: two consecutive 5.8 V / 5.7 V samples (cycles 4, 5). */
    hn h;
    hn_init(&h, 1, 0);
    spacecraft_state legacy = { .mode = MODE_BOOT };
    int legacy_safe_from = -1;

    printf("Timeline 1 - Session 3 HK profile through FDIR (confirm = %u cycles)\n", (unsigned) FDIR_CONFIRM_CYCLES);
    for (unsigned cyc = 0; cyc < HK_NUM_CYCLES; ++cyc) {
        size_t ev_before = h.log.n;
        sample_state(&h.s);                                       /* Session 3, unchanged */
        if (cyc == 0) { assert(h.s.bus_voltage == 8.10f); }       /* profile aligned at index 0 */
        legacy = (spacecraft_state) { .bus_voltage = h.s.bus_voltage, .battery_pct = h.s.battery_pct,
                                      .temp_obc = h.s.temp_obc, .temp_batt = h.s.temp_batt,
                                      .mode = legacy.mode };
        apply_limits(&legacy);                                    /* Session 3 behaviour, for comparison */
        if (legacy.mode == MODE_SAFE && legacy_safe_from < 0) { legacy_safe_from = (int) cyc; }

        float v = h.s.bus_voltage;
        health_t raw = monitor_power(&h.s);
        (void) hn_cycle_state(&h);
        print_cycle(&h, cyc, v, raw, ev_before);
    }
    /* The 2-cycle dip is detected once confirmed, recovered as soon as it clears,
     * and never reaches safe mode. */
    assert(h.f.n_safe_entries == 0 && h.f.n_faults == 1 && h.f.n_filtered == 1);
    assert(h.log.n == 2 && EV_ID(h.log.pkt[0]) == EVT_FAULT && EV_ID(h.log.pkt[1]) == EVT_RECOVERED);
    assert(h.f.state == FDIR_NOMINAL && h.s.mode == MODE_NOMINAL);
    assert(legacy_safe_from == 4 && legacy.mode == MODE_SAFE);    /* Session 3's latch: SAFE at cycle 4, forever */
    printf("  (Session 3 apply_limits() alone: SAFE from cycle %d and it never leaves; FDIR: 1 fault, 1 recovery, 0 safe entries)\n", legacy_safe_from);
    printf("PASS Session 3 profile: a 2-cycle dip is confirmed, reported, recovered - no safe mode\n\n");
}

static void scenario_persistent_fault(void) {
    hn h;
    hn_init(&h, 1, 0);
    printf("Timeline 2 - persistent undervoltage from cycle 2 (confirm = %u, max attempts = %u)\n",
           (unsigned) FDIR_CONFIRM_CYCLES, (unsigned) FDIR_MAX_ATTEMPTS);
    int safe_at = -1;
    for (unsigned cyc = 0; cyc < 10; ++cyc) {
        size_t ev_before = h.log.n;
        float v = (cyc < 2) ? 8.0f : 5.0f;
        h.s.bus_voltage = v;
        health_t raw = monitor_power(&h.s);
        (void) hn_cycle_state(&h);
        print_cycle(&h, cyc, v, raw, ev_before);
        if (h.f.state == FDIR_SAFE && safe_at < 0) { safe_at = (int) cyc; }
    }
    /* first bad sample at cycle 2, SAFE at cycle 7: 5 cycles later. */
    assert(safe_at == 7 && h.n_safe == 1 && h.n_recover == 3);
    assert(h.log.n == 5);
    printf("  first bad sample cycle 2 -> SAFE at cycle %d: %d cycles = %d ms at the %d ms HK cycle\n",
           safe_at, safe_at - 2, (safe_at - 2) * HK_CYCLE_MS, HK_CYCLE_MS);
    printf("PASS persistent fault: confirmed, 3 local recoveries, then SAFE with a HIGH-severity event\n\n");
}

void fdir_selfcheck(void) {
    printf("=== Session 10: FDIR self checks ===\n");
    scenario_session3_profile();   /* first: sample_state() must start at profile index 0 */
    check_monitors();
    check_watchdog();
    check_state_machine();
    check_persistence();
    check_watchdog_integration();
    check_events_via_obdh();
    check_ground_exit();
    check_fdir_hk();
    scenario_persistent_fault();
    printf("all Session 10 FDIR self checks passed\n");
}
