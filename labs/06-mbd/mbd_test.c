/* labs/06-mbd/mbd_test.c
 *
 * Session 11 Step 3: verification of the code generated from fdir_model.txt.
 *
 *   A. requirement tests   the generated component alone, one test per FDIR-REQ (requirements.md).
 *                          They say what the logic must do without looking at Session 10's code.
 *   B. back-to-back        the generated fdir_step_gen() / fdir_exit_safe_gen() against the hand
 *                          written Session 10 fdir_step() / fdir_exit_safe(), fed the same inputs.
 *                          Compared after EVERY step, not just the state: the state, all counters
 *                          and latched masks, every PUS event the hand-written code sends through
 *                          OBDH (decoded from the downlinked packet) and every recovery / safe-mode
 *                          hook call, in the same order.
 *        B1  exhaustive: every input sequence up to MBD_EXHAUSTIVE_DEPTH steps
 *        B2  randomized: long seeded sequences, to reach deep states repeatedly
 *        B3  single step from randomized contexts, reachable or not (attempts of 200, a streak of
 *            255, masks of 0xFF, ...), so the equivalence does not lean on reachability
 *   C. model coverage      every transition of the model fired at least once in B1..B3
 *
 * Terminology: this is SIL (software in the loop, generated code on the host) plus a
 * back-to-back comparison with an independent implementation. The model is a table, not an
 * executable diagram, so there is no separate MIL (model in the loop) run: the requirement
 * tests in A play that role. PIL (processor in the loop) needs the target and is not done.
 *
 * No main(): labs/04-obdh/obdh_test.c runs the earlier labs' checks and then calls
 * mbd_selfcheck().
 */
#include "fdir_step_gen.h"
#include "../05-fdir/fdir.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MBD_EXHAUSTIVE_DEPTH 8u  /* 5^1 + ... + 5^8 = 488280 sequences per confirm setting */
#define MBD_RANDOM_RUNS      4000u
#define MBD_RANDOM_LENGTH    80u
#define MBD_RANDOM_CONTEXTS  400000u

/* ============================================================================================
 * The inputs the tests drive both implementations with.
 * ========================================================================================== */

typedef enum { S_OK, S_WARN, S_FAULT_PWR, S_FAULT_WD, S_EXIT, N_SYM } sym_t;
static const char *const sym_name[N_SYM] = { "OK", "WARN", "FAULT_PWR", "FAULT_WD", "EXIT" };

typedef struct { uint8_t worst, sources, stalled; } stim_t;

/* FAULT_PWR: a power fault. FAULT_WD: a watchdog fault with task 1 stalled. Two different
 * causes, so the latched source / task masks are exercised as well as the severities. */
static stim_t stim_of(sym_t s) {
    stim_t t = { HEALTH_OK, 0u, 0u };
    switch (s) {
    case S_WARN:      t.worst = HEALTH_WARN;  t.sources = FDIR_SRC_POWER; break;
    case S_FAULT_PWR: t.worst = HEALTH_FAULT; t.sources = FDIR_SRC_POWER; break;
    case S_FAULT_WD:  t.worst = HEALTH_FAULT; t.sources = FDIR_SRC_WATCHDOG; t.stalled = 0x02u; break;
    default: break;
    }
    return t;
}

/* ============================================================================================
 * What an implementation did in one step, in the order it did it.
 * ========================================================================================== */

enum { OBS_EVENT = 1, OBS_RECOVER, OBS_ENTER_SAFE };

typedef struct {
    uint8_t  kind;
    uint16_t id;        /* EVENT: event id                                           */
    uint8_t  subtype;   /* EVENT: PUS Service 5 subtype (the severity on the wire)   */
    uint8_t  worst, state, attempts, src, tasks;   /* EVENT payload                  */
    uint8_t  action;    /* RECOVER                                                   */
} obs_t;

#define OBS_MAX 4u

static uint16_t rd16(const uint8_t *p) { return (uint16_t) (((uint16_t) p[0] << 8) | p[1]); }

/* ---- failure reporting: say WHICH input sequence the two implementations disagree on ------ */

static sym_t    g_seq[MBD_EXHAUSTIVE_DEPTH + 1u];
static unsigned g_len;
static unsigned g_confirm;
static int      g_random_ctx;
static struct { unsigned st, attempts, streak, prev, smask, tmask, confirm, worst, sources, stalled; } g_rc;

static void b2b_fail(const char *cond, int line) {
    fprintf(stderr, "\nBACK-TO-BACK MISMATCH (mbd_test.c:%d): %s\n", line, cond);
    if (g_random_ctx) {
        fprintf(stderr, "  single step from context state=%u attempts=%u streak=%u prev_worst=%u "
                        "source_mask=0x%02X task_mask=0x%02X confirm_cycles=%u\n"
                        "  input worst=%u sources=0x%02X stalled=0x%02X\n",
                g_rc.st, g_rc.attempts, g_rc.streak, g_rc.prev, g_rc.smask, g_rc.tmask, g_rc.confirm,
                g_rc.worst, g_rc.sources, g_rc.stalled);
    } else {
        fprintf(stderr, "  confirm_cycles=%u, input sequence (%u steps):", g_confirm, g_len);
        for (unsigned i = 0; i < g_len; ++i) { fprintf(stderr, " %s", sym_name[g_seq[i]]); }
        fprintf(stderr, "\n");
    }
    abort();
}
#define B2B_CHECK(cond) do { if (!(cond)) { b2b_fail(#cond, __LINE__); } } while (0)

/* ============================================================================================
 * The reference: Session 10's hand-written FDIR, wired to a real OBDH router and to hooks that
 * record what they are asked to do.
 * ========================================================================================== */

typedef struct {
    obdh_ctx o;
    fdir_ctx f;
    obs_t    obs[OBS_MAX];
    unsigned n_obs;
} ref_t;

static obs_t *ref_next_obs(ref_t *r) {
    B2B_CHECK(r->n_obs < OBS_MAX);
    obs_t *o = &r->obs[r->n_obs++];
    memset(o, 0, sizeof(*o));
    return o;
}

static void ref_downlink(void *user, const uint8_t *p, size_t n) {
    ref_t *r = user;
    ccsds_pri_hdr h;
    ccsds_parse_primary(p, &h);
    B2B_CHECK(h.apid == APID_EVENT);
    B2B_CHECK(n == CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + FDIR_EVT_PAYLOAD_LEN);
    B2B_CHECK(p[CCSDS_PRI_HDR_LEN + 1] == PUS_SERVICE_EVENT);
    const uint8_t *pl = p + CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN;
    obs_t *o = ref_next_obs(r);
    o->kind    = OBS_EVENT;
    o->subtype = p[CCSDS_PRI_HDR_LEN + 2];
    o->id      = rd16(pl);
    o->state   = pl[2];
    o->attempts = pl[3];
    o->worst   = pl[4];
    o->src     = pl[5];
    o->tasks   = pl[6];
}

static void ref_recover(void *user, recovery_action_t a, uint8_t src) {
    obs_t *o = ref_next_obs(user);
    o->kind = OBS_RECOVER;
    o->action = (uint8_t) a;
    o->src = src;
}

static void ref_enter_safe(void *user, uint8_t src) {
    obs_t *o = ref_next_obs(user);
    o->kind = OBS_ENTER_SAFE;
    o->src = src;
}

/* One reference and one generated component, driven in lock step. */
typedef struct {
    ref_t         r;
    fdir_gen_ctx  g;
    uint32_t      n_gen_events;
} pair_t;

static void pair_init(pair_t *p, unsigned confirm) {
    memset(p, 0, sizeof(*p));
    obdh_init(&p->r.o, ref_downlink, &p->r, 1);          /* in contact: events go straight down */
    fdir_hooks hooks = { .recover = ref_recover, .enter_safe = ref_enter_safe, .user = &p->r };
    fdir_init(&p->r.f, &p->r.o, &hooks);
    p->r.f.confirm_cycles = (uint8_t) confirm;
    fdir_init_gen(&p->g, (uint8_t) confirm);
}

/* Copies a whole pair (the exhaustive search branches from every node) and re-aims the
 * pointers that point back into it. */
static void pair_clone(pair_t *dst, const pair_t *src) {
    memcpy(dst, src, sizeof(*dst));
    dst->r.o.downlink_user = &dst->r;
    dst->r.f.obdh = &dst->r.o;
    dst->r.f.hooks.user = &dst->r;
}

/* ============================================================================================
 * Comparison
 * ========================================================================================== */

/* The model's state, seen as the reference's state. */
static uint8_t as_ref_state(uint8_t gen_state) { return gen_state; }

static uint32_t g_hits[256];
static unsigned long g_compared_steps;

static void compare_effects(const pair_t *p, const fdir_gen_out *out) {
    const ref_t *r = &p->r;
    B2B_CHECK(out->overflow == 0u);
    B2B_CHECK(r->n_obs == out->n_effects);
    for (unsigned i = 0; i < out->n_effects; ++i) {
        const fdir_gen_effect *e = &out->effect[i];
        const obs_t *o = &r->obs[i];
        switch (e->kind) {
        case FDIR_GEN_FX_EVENT:
            B2B_CHECK(o->kind == OBS_EVENT);
            B2B_CHECK(o->id == e->event_id);
            B2B_CHECK(o->subtype == fdir_event_subtype((fdir_sev_t) e->severity));
            B2B_CHECK(o->worst == e->worst);
            B2B_CHECK(o->state == as_ref_state(e->state));
            B2B_CHECK(o->attempts == e->attempts);
            B2B_CHECK(o->src == e->source_mask);
            B2B_CHECK(o->tasks == e->task_mask);
            break;
        case FDIR_GEN_FX_RECOVER:
            B2B_CHECK(o->kind == OBS_RECOVER);
            B2B_CHECK(o->action == e->action);
            B2B_CHECK(o->src == e->source_mask);
            break;
        case FDIR_GEN_FX_ENTER_SAFE:
            B2B_CHECK(o->kind == OBS_ENTER_SAFE);
            B2B_CHECK(o->src == e->source_mask);
            break;
        default:
            B2B_CHECK(0 && "unknown effect kind");
        }
    }
}

static void compare_context(const pair_t *p) {
    const fdir_ctx *f = &p->r.f;
    const fdir_gen_ctx *g = &p->g;
    B2B_CHECK(as_ref_state((uint8_t) g->state) == (uint8_t) f->state);
    B2B_CHECK(g->attempts == f->attempts);
    B2B_CHECK(g->fault_streak == f->fault_streak);
    B2B_CHECK(g->prev_worst == (uint8_t) f->prev_worst);
    B2B_CHECK(g->source_mask == f->source_mask);
    B2B_CHECK(g->task_mask == f->task_mask);
    B2B_CHECK(g->n_faults == f->n_faults);
    B2B_CHECK(g->n_safe_entries == f->n_safe_entries);
    B2B_CHECK(g->n_filtered == f->n_filtered);
    B2B_CHECK(g->confirm_cycles == f->confirm_cycles);
    B2B_CHECK(f->n_event_lost == 0u);
    B2B_CHECK(p->n_gen_events == f->n_events);
}

static void count_gen_events(pair_t *p, const fdir_gen_out *out) {
    for (unsigned i = 0; i < out->n_effects; ++i) {
        if (out->effect[i].kind == FDIR_GEN_FX_EVENT) { p->n_gen_events++; }
    }
}

/* One FDIR cycle on both implementations. */
static void step_cycle(pair_t *p, uint8_t worst, uint8_t sources, uint8_t stalled) {
    fdir_gen_in in = { .worst = worst, .sources = sources, .stalled = stalled };
    fdir_gen_out out;

    p->r.n_obs = 0;
    p->r.f.stalled_now = stalled;                 /* what fdir_cycle() would have set */
    fdir_state_t rs = fdir_step(&p->r.f, (health_t) worst, sources);
    fdir_gen_state_t gs = fdir_step_gen(&p->g, &in, &out);

    B2B_CHECK(as_ref_state((uint8_t) gs) == (uint8_t) rs);   /* what fdir_step() returns */
    count_gen_events(p, &out);
    compare_effects(p, &out);
    compare_context(p);
    if (out.transition != 0u) { g_hits[out.transition]++; }
    g_compared_steps++;
}

/* The ground exit command on both implementations. */
static void step_exit(pair_t *p) {
    fdir_gen_out out;

    p->r.n_obs = 0;
    int rr = fdir_exit_safe(&p->r.f);
    int gr = fdir_exit_safe_gen(&p->g, &out);

    B2B_CHECK(rr == gr);
    count_gen_events(p, &out);
    compare_effects(p, &out);
    compare_context(p);
    if (out.transition != 0u) { g_hits[out.transition]++; }
    g_compared_steps++;
}

static void step_sym(pair_t *p, sym_t s) {
    if (s == S_EXIT) {
        step_exit(p);
    } else {
        stim_t t = stim_of(s);
        step_cycle(p, t.worst, t.sources, t.stalled);
    }
}

/* ============================================================================================
 * B1. Exhaustive: every input sequence up to MBD_EXHAUSTIVE_DEPTH steps.
 * ========================================================================================== */

static pair_t g_node[MBD_EXHAUSTIVE_DEPTH + 1u];
static unsigned long g_sequences;

static void search(unsigned depth) {
    if (depth == MBD_EXHAUSTIVE_DEPTH) { return; }
    for (int s = 0; s < N_SYM; ++s) {
        pair_clone(&g_node[depth + 1u], &g_node[depth]);
        g_seq[depth] = (sym_t) s;
        g_len = depth + 1u;
        step_sym(&g_node[depth + 1u], (sym_t) s);
        g_sequences++;
        search(depth + 1u);
    }
}

static const unsigned k_confirm_settings[] = { 0u, 1u, 2u, 3u };   /* 0 behaves as 1 */

static void check_exhaustive(void) {
    g_random_ctx = 0;
    for (size_t i = 0; i < sizeof(k_confirm_settings) / sizeof(k_confirm_settings[0]); ++i) {
        g_confirm = k_confirm_settings[i];
        pair_init(&g_node[0], g_confirm);
        search(0);
    }
    printf("PASS B1 exhaustive: %lu input sequences (every sequence of up to %u inputs from "
           "{OK, WARN, FAULT_PWR, FAULT_WD, EXIT}, confirm_cycles 0..3), generated and "
           "hand-written agree after every step\n",
           g_sequences, MBD_EXHAUSTIVE_DEPTH);
}

/* ============================================================================================
 * B2. Randomized long sequences (deterministic: fixed seed).
 * ========================================================================================== */

static uint32_t g_rng = 0x2F6E2B1u;

static uint32_t rnd(void) {            /* xorshift32 */
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* Weighted so that long runs reach SAFE, leave it, and fault again. */
static sym_t random_sym(void) {
    uint32_t v = rnd() % 100u;
    if (v < 30u) { return S_OK; }
    if (v < 42u) { return S_WARN; }
    if (v < 70u) { return S_FAULT_PWR; }
    if (v < 85u) { return S_FAULT_WD; }
    return S_EXIT;
}

static void check_random(void) {
    static sym_t seq[MBD_RANDOM_LENGTH];
    g_random_ctx = 0;
    for (unsigned run = 0; run < MBD_RANDOM_RUNS; ++run) {
        pair_t *p = &g_node[0];
        g_confirm = rnd() % 6u;
        pair_init(p, g_confirm);
        for (unsigned i = 0; i < MBD_RANDOM_LENGTH; ++i) {
            seq[i] = random_sym();
            step_sym(p, seq[i]);
        }
    }
    printf("PASS B2 randomized: %u seeded runs of %u inputs, confirm_cycles 0..5, agree after every step\n",
           MBD_RANDOM_RUNS, MBD_RANDOM_LENGTH);
}

/* ============================================================================================
 * B3. One step from a randomized context, reachable or not.
 * ========================================================================================== */

#define INVALID_STATE 9u   /* not a state of either implementation */

static uint8_t pick_small_or_any(void) {
    return (rnd() & 1u) ? (uint8_t) (rnd() % 6u) : (uint8_t) (rnd() & 0xFFu);
}

/* Gives both implementations the same context. */
static void load_context(pair_t *p, unsigned st, unsigned attempts, unsigned streak, unsigned prev,
                         unsigned smask, unsigned tmask, unsigned confirm, unsigned n) {
    pair_init(p, confirm);
    p->r.f.state = (fdir_state_t) st;
    p->g.state = (fdir_gen_state_t) st;
    p->r.f.attempts = p->g.attempts = (uint8_t) attempts;
    p->r.f.fault_streak = p->g.fault_streak = (uint8_t) streak;
    p->r.f.prev_worst = (health_t) prev;
    p->g.prev_worst = (uint8_t) prev;
    p->r.f.source_mask = p->g.source_mask = (uint8_t) smask;
    p->r.f.task_mask = p->g.task_mask = (uint8_t) tmask;
    p->r.f.n_faults = p->g.n_faults = (uint16_t) n;
    p->r.f.n_safe_entries = p->g.n_safe_entries = (uint16_t) (n * 7u);
    p->r.f.n_filtered = p->g.n_filtered = (uint16_t) (n * 13u);
}

static unsigned random_state_code(void) {
    unsigned v = rnd() % 10u;
    return (v < 3u) ? v : (v < 9u ? (v % 3u) : INVALID_STATE);
}

static void check_random_contexts(void) {
    static pair_t base, work;
    g_random_ctx = 1;
    for (unsigned i = 0; i < MBD_RANDOM_CONTEXTS; ++i) {
        g_rc.st = random_state_code();
        g_rc.attempts = pick_small_or_any();
        g_rc.streak = pick_small_or_any();
        g_rc.prev = rnd() % 3u;
        g_rc.smask = (rnd() & 1u) ? 0u : (rnd() & 0xFFu);
        g_rc.tmask = (rnd() & 1u) ? 0u : (rnd() & 0xFFu);
        g_rc.confirm = pick_small_or_any();
        g_rc.worst = rnd() % 3u;
        g_rc.sources = (g_rc.worst == HEALTH_OK) ? 0u : (rnd() & 0xFFu);
        g_rc.stalled = (rnd() & 1u) ? 0u : (rnd() & 0xFFu);

        load_context(&base, g_rc.st, g_rc.attempts, g_rc.streak, g_rc.prev, g_rc.smask, g_rc.tmask,
                     g_rc.confirm, i & 0xFFFFu);
        pair_clone(&work, &base);
        step_cycle(&work, (uint8_t) g_rc.worst, (uint8_t) g_rc.sources, (uint8_t) g_rc.stalled);
        pair_clone(&work, &base);
        step_exit(&work);
    }
    g_random_ctx = 0;
    printf("PASS B3 randomized contexts: %u single steps and %u exit commands from arbitrary contexts "
           "(attempts and streak up to 255, any masks, invalid state codes), agree\n",
           MBD_RANDOM_CONTEXTS, MBD_RANDOM_CONTEXTS);
}

/* ============================================================================================
 * C. Model coverage: every transition of the model must have fired.
 * ========================================================================================== */

static void check_model_coverage(void) {
    unsigned covered = 0;
    printf("\nModel coverage (transition: times fired in B1..B3)\n");
    for (unsigned i = 0; i < FDIR_GEN_N_TRANSITIONS; ++i) {
        unsigned id = fdir_gen_transition_id(i);
        printf("  %-42s %lu\n", fdir_gen_transition_text(i), (unsigned long) g_hits[id]);
        if (g_hits[id] > 0u) { covered++; }
    }
    assert(covered == FDIR_GEN_N_TRANSITIONS);
    printf("PASS C model coverage: %u of %u transitions exercised\n", covered, FDIR_GEN_N_TRANSITIONS);
    assert(fdir_gen_transition_id(FDIR_GEN_N_TRANSITIONS) == 0u);
}

/* ============================================================================================
 * A. Requirement tests on the generated component alone.
 * ========================================================================================== */

typedef struct {
    fdir_gen_ctx g;
    fdir_gen_out out;
    int          last_exit;
} gen_t;

static void gen_start(gen_t *t, unsigned confirm) {
    memset(t, 0, sizeof(*t));
    fdir_init_gen(&t->g, (uint8_t) confirm);
}

static fdir_gen_state_t gen_cycle(gen_t *t, uint8_t worst, uint8_t sources, uint8_t stalled) {
    fdir_gen_in in = { .worst = worst, .sources = sources, .stalled = stalled };
    fdir_gen_state_t s = fdir_step_gen(&t->g, &in, &t->out);
    assert(t->out.overflow == 0u);
    return s;
}

static void gen_exit(gen_t *t) {
    t->last_exit = fdir_exit_safe_gen(&t->g, &t->out);
    assert(t->out.overflow == 0u);
}

static const fdir_gen_effect *fx(const gen_t *t, unsigned i) {
    assert(i < t->out.n_effects);
    return &t->out.effect[i];
}

static void expect_event(const gen_t *t, unsigned i, uint16_t id, fdir_sev_t sev,
                         uint8_t state, uint8_t attempts, uint8_t worst) {
    const fdir_gen_effect *e = fx(t, i);
    assert(e->kind == FDIR_GEN_FX_EVENT);
    assert(e->event_id == id && e->severity == (uint8_t) sev);
    assert(e->state == state && e->attempts == attempts && e->worst == worst);
}

#define FAULT_PWR  HEALTH_FAULT, FDIR_SRC_POWER, 0u
#define OKAY       HEALTH_OK, 0u, 0u
#define WARNING    HEALTH_WARN, FDIR_SRC_POWER, 0u

/* Drives a power fault until the component is in RECOVERING (confirm_cycles = 1). */
static void gen_into_recovering(gen_t *t) {
    gen_start(t, 1u);
    assert(gen_cycle(t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING);
}

static void gen_into_safe(gen_t *t) {
    gen_into_recovering(t);
    for (unsigned i = 0; i < FDIR_MAX_ATTEMPTS; ++i) { (void) gen_cycle(t, FAULT_PWR); }
    assert(gen_cycle(t, FAULT_PWR) == FDIR_GEN_STATE_SAFE);
}

static void req_001_persistence(void) {
    gen_t t;
    /* confirm_cycles = 2: one FAULT is filtered, silently. */
    gen_start(&t, 2u);
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_NOMINAL);
    assert(t.out.n_effects == 0u && t.g.n_filtered == 1u && t.g.n_faults == 0u);
    /* An interruption restarts the count: FAULT, OK, FAULT is not two in a row. */
    (void) gen_cycle(&t, OKAY);
    assert(t.g.fault_streak == 0u);
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_NOMINAL);
    assert(t.out.n_effects == 0u && t.g.n_filtered == 2u);
    /* ...and a WARN in between interrupts it too. */
    (void) gen_cycle(&t, WARNING);
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_NOMINAL && t.g.n_filtered == 3u);
    /* Two in a row: acted on at the second. */
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING);
    assert(t.g.n_faults == 1u && t.g.n_filtered == 3u);
    /* confirm_cycles = 1, and 0 (treated as 1): the first FAULT acts. */
    gen_start(&t, 1u);
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING);
    gen_start(&t, 0u);
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING);
    /* confirm_cycles = 4: acts exactly on the fourth. */
    gen_start(&t, 4u);
    for (unsigned i = 1; i <= 3u; ++i) { assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_NOMINAL); }
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING && t.g.n_filtered == 3u);
}

static void req_002_detection(void) {
    gen_t t;
    gen_start(&t, 1u);
    /* A watchdog fault with task 2 stalled, and a power fault at the same time. */
    assert(gen_cycle(&t, HEALTH_FAULT, FDIR_SRC_WATCHDOG | FDIR_SRC_POWER, 0x04u) == FDIR_GEN_STATE_RECOVERING);
    assert(t.out.n_effects == 1u && t.out.transition != 0u);
    expect_event(&t, 0, EVT_FAULT, SEV_MEDIUM, FDIR_GEN_STATE_RECOVERING, 0u, HEALTH_FAULT);
    assert(fx(&t, 0)->source_mask == (FDIR_SRC_WATCHDOG | FDIR_SRC_POWER) && fx(&t, 0)->task_mask == 0x04u);
    assert(t.g.attempts == 0u && t.g.n_faults == 1u);
    assert(t.g.source_mask == (FDIR_SRC_WATCHDOG | FDIR_SRC_POWER) && t.g.task_mask == 0x04u);
    /* A fresh episode starts from zero attempts even if the previous one left some behind. */
    t.g.attempts = 2u;
    t.g.state = FDIR_GEN_STATE_NOMINAL;
    (void) gen_cycle(&t, FAULT_PWR);
    assert(t.g.attempts == 0u);
}

static void req_003_graduated_recovery(void) {
    gen_t t;
    static const uint8_t rung[3] = { RECOVERY_RETRY, RECOVERY_RESET_DEVICE, RECOVERY_SWITCH_REDUNDANT };
    gen_into_recovering(&t);
    assert(FDIR_MAX_ATTEMPTS == 3u);
    for (unsigned i = 0; i < FDIR_MAX_ATTEMPTS; ++i) {
        assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING);
        assert(t.out.n_effects == 2u);                       /* exactly one action and its report */
        assert(fx(&t, 0)->kind == FDIR_GEN_FX_RECOVER);
        assert(fx(&t, 0)->action == rung[i] && fx(&t, 0)->source_mask == FDIR_SRC_POWER);
        expect_event(&t, 1, EVT_RECOVERY_ACTION, SEV_INFO, FDIR_GEN_STATE_RECOVERING,
                     (uint8_t) (i + 1u), HEALTH_FAULT);
    }
    /* A WARN is not recovery: it still costs an attempt. */
    gen_into_recovering(&t);
    assert(gen_cycle(&t, WARNING) == FDIR_GEN_STATE_RECOVERING && t.g.attempts == 1u);
    assert(fx(&t, 0)->kind == FDIR_GEN_FX_RECOVER);
    /* The causes seen while recovering are merged into the latched ones. */
    (void) gen_cycle(&t, HEALTH_FAULT, FDIR_SRC_WATCHDOG, 0x01u);
    assert(t.g.source_mask == (FDIR_SRC_POWER | FDIR_SRC_WATCHDOG) && t.g.task_mask == 0x01u);
}

static void req_004_recovery(void) {
    gen_t t;
    gen_into_recovering(&t);
    (void) gen_cycle(&t, FAULT_PWR);                          /* one attempt made */
    assert(gen_cycle(&t, OKAY) == FDIR_GEN_STATE_NOMINAL);
    assert(t.out.n_effects == 1u);
    expect_event(&t, 0, EVT_RECOVERED, SEV_LOW, FDIR_GEN_STATE_NOMINAL, 1u, HEALTH_OK);
    assert(fx(&t, 0)->source_mask == FDIR_SRC_POWER);         /* the event still names the cause... */
    assert(t.g.source_mask == 0u && t.g.task_mask == 0u);     /* ...and it is cleared afterwards */
    assert(t.g.n_safe_entries == 0u);
    /* Precedence over escalation: OK on the cycle that would have entered SAFE. */
    gen_into_recovering(&t);
    for (unsigned i = 0; i < FDIR_MAX_ATTEMPTS; ++i) { (void) gen_cycle(&t, FAULT_PWR); }
    assert(t.g.attempts == FDIR_MAX_ATTEMPTS);
    assert(gen_cycle(&t, OKAY) == FDIR_GEN_STATE_NOMINAL);
    expect_event(&t, 0, EVT_RECOVERED, SEV_LOW, FDIR_GEN_STATE_NOMINAL, FDIR_MAX_ATTEMPTS, HEALTH_OK);
    assert(t.g.n_safe_entries == 0u);
}

static void req_005_escalation(void) {
    gen_t t;
    gen_into_recovering(&t);
    for (unsigned i = 0; i < FDIR_MAX_ATTEMPTS; ++i) {        /* all allowed attempts are made first */
        assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING);
    }
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_SAFE);  /* then, still faulty: SAFE */
    assert(t.out.n_effects == 2u);
    expect_event(&t, 0, EVT_SAFE_ENTER, SEV_HIGH, FDIR_GEN_STATE_SAFE, FDIR_MAX_ATTEMPTS, HEALTH_FAULT);
    assert(fx(&t, 1)->kind == FDIR_GEN_FX_ENTER_SAFE && fx(&t, 1)->source_mask == FDIR_SRC_POWER);
    assert(t.g.n_safe_entries == 1u);
    /* A WARN that never clears escalates too: WARN is not OK. */
    gen_into_recovering(&t);
    for (unsigned i = 0; i < FDIR_MAX_ATTEMPTS; ++i) { (void) gen_cycle(&t, WARNING); }
    assert(gen_cycle(&t, WARNING) == FDIR_GEN_STATE_SAFE);
}

static void req_006_safe_absorbing(void) {
    gen_t t;
    gen_into_safe(&t);
    const fdir_gen_ctx before = t.g;
    const uint8_t severities[3] = { HEALTH_OK, HEALTH_WARN, HEALTH_FAULT };
    for (unsigned round = 0; round < 4u; ++round) {
        for (unsigned i = 0; i < 3u; ++i) {
            assert(gen_cycle(&t, severities[i], FDIR_SRC_POWER, 0x01u) == FDIR_GEN_STATE_SAFE);
            assert(t.out.n_effects == 0u);
            assert(t.g.attempts == before.attempts && t.g.n_faults == before.n_faults);
            assert(t.g.n_safe_entries == before.n_safe_entries && t.g.n_filtered == before.n_filtered);
            assert(t.g.source_mask == before.source_mask && t.g.task_mask == before.task_mask);
        }
    }
}

static void req_007_ground_exit(void) {
    gen_t t;
    gen_into_safe(&t);
    (void) gen_cycle(&t, OKAY);
    gen_exit(&t);
    assert(t.last_exit == 1 && t.g.state == FDIR_GEN_STATE_NOMINAL);
    assert(t.out.n_effects == 1u);
    expect_event(&t, 0, EVT_SAFE_EXIT, SEV_INFO, FDIR_GEN_STATE_NOMINAL, 0u, HEALTH_OK);
    assert(fx(&t, 0)->source_mask == FDIR_SRC_POWER);         /* the cause is reported on the way out */
    assert(t.g.source_mask == 0u && t.g.task_mask == 0u && t.g.attempts == 0u && t.g.fault_streak == 0u);
    /* Exit has no effect anywhere else, and says so. */
    gen_start(&t, 2u);
    gen_exit(&t);
    assert(t.last_exit == 0 && t.out.n_effects == 0u && t.out.transition == 0u);
    gen_into_recovering(&t);
    const fdir_gen_ctx before = t.g;
    gen_exit(&t);
    assert(t.last_exit == 0 && t.out.n_effects == 0u && memcmp(&before, &t.g, sizeof(before)) == 0);
    /* A fault that is still there is detected again after the exit. */
    gen_into_safe(&t);
    gen_exit(&t);
    assert(gen_cycle(&t, FAULT_PWR) == FDIR_GEN_STATE_RECOVERING && t.g.n_faults == 2u);
}

static void req_008_warning_edge(void) {
    gen_t t;
    gen_start(&t, 2u);
    (void) gen_cycle(&t, OKAY);
    assert(gen_cycle(&t, WARNING) == FDIR_GEN_STATE_NOMINAL || t.g.state != FDIR_GEN_STATE_RECOVERING);
    assert(t.out.n_effects == 1u);
    expect_event(&t, 0, EVT_WARN, SEV_LOW, (uint8_t) t.g.state, 0u, HEALTH_WARN);
    (void) gen_cycle(&t, WARNING);                            /* staying in WARN: silent */
    assert(t.out.n_effects == 0u);
    (void) gen_cycle(&t, WARNING);
    assert(t.out.n_effects == 0u);
    (void) gen_cycle(&t, OKAY);
    (void) gen_cycle(&t, WARNING);                            /* a new rise is reported again */
    assert(t.out.n_effects == 1u && fx(&t, 0)->event_id == EVT_WARN);
    /* WARN after a filtered FAULT is not a rise from OK: no event. */
    gen_start(&t, 3u);
    (void) gen_cycle(&t, FAULT_PWR);
    (void) gen_cycle(&t, WARNING);
    assert(t.out.n_effects == 0u);
    /* No recovery action is ever requested for a warning. */
    gen_start(&t, 1u);
    for (unsigned i = 0; i < 10u; ++i) {
        (void) gen_cycle(&t, (i % 2u) ? HEALTH_WARN : HEALTH_OK, FDIR_SRC_POWER, 0u);
        for (unsigned k = 0; k < t.out.n_effects; ++k) { assert(t.out.effect[k].kind == FDIR_GEN_FX_EVENT); }
    }
}

static void check_requirements(void) {
    req_001_persistence();
    req_002_detection();
    req_003_graduated_recovery();
    req_004_recovery();
    req_005_escalation();
    req_006_safe_absorbing();
    req_007_ground_exit();
    req_008_warning_edge();
    printf("PASS A requirement tests: FDIR-REQ-001 .. FDIR-REQ-008 on the generated component alone\n\n");
}

void mbd_selfcheck(void) {
    printf("=== Session 11: model-based FDIR self checks ===\n");
    printf("model: %u states, %u transitions; generated step component vs hand-written Session 10 FDIR\n\n",
           (unsigned) FDIR_GEN_N_STATES, (unsigned) FDIR_GEN_N_TRANSITIONS);
    check_requirements();
    check_exhaustive();
    check_random();
    check_random_contexts();
    printf("     %lu steps compared in total\n", g_compared_steps);
    check_model_coverage();
    printf("all Session 11 model-based FDIR self checks passed\n");
}
