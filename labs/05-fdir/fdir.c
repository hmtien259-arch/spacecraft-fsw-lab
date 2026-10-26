/* labs/05-fdir/fdir.c - monitors, watchdog, graduated state machine, events. */
#include "fdir.h"

#include <string.h>

/* ---- Monitors ------------------------------------------------------------- */

health_t health_worst(health_t a, health_t b) {
    return (a > b) ? a : b; /* OK < WARN < FAULT */
}

health_t monitor_power(const spacecraft_state *s) {
    if (s->bus_voltage < V_FAULT) { return HEALTH_FAULT; } /* generalizes the S3 check */
    if (s->bus_voltage < V_WARN)  { return HEALTH_WARN; }
    return HEALTH_OK;
}

health_t monitor_thermal(const spacecraft_state *s) {
    health_t h = HEALTH_OK;
    if (s->temp_obc  > OBC_TEMP_FAULT_C)       { h = health_worst(h, HEALTH_FAULT); }
    else if (s->temp_obc > OBC_TEMP_WARN_C)    { h = health_worst(h, HEALTH_WARN); }
    if (s->temp_batt > BATT_TEMP_HIGH_FAULT_C || s->temp_batt < BATT_TEMP_LOW_FAULT_C) {
        h = health_worst(h, HEALTH_FAULT);
    } else if (s->temp_batt > BATT_TEMP_HIGH_WARN_C || s->temp_batt < BATT_TEMP_LOW_WARN_C) {
        h = health_worst(h, HEALTH_WARN);
    }
    return h;
}

health_t monitor_battery(const spacecraft_state *s) {
    if (s->battery_pct < BATT_SOC_FAULT_PCT) { return HEALTH_FAULT; }
    if (s->battery_pct < BATT_SOC_WARN_PCT)  { return HEALTH_WARN; }
    return HEALTH_OK;
}

health_t fdir_evaluate(const spacecraft_state *s, health_t watchdog_result, uint8_t *sources) {
    health_t per[4] = {
        monitor_power(s), monitor_thermal(s), monitor_battery(s), watchdog_result
    };
    static const uint8_t bit[4] = {
        FDIR_SRC_POWER, FDIR_SRC_THERMAL, FDIR_SRC_BATTERY, FDIR_SRC_WATCHDOG
    };
    health_t worst = HEALTH_OK;
    for (int i = 0; i < 4; ++i) { worst = health_worst(worst, per[i]); }

    uint8_t mask = 0;
    if (worst != HEALTH_OK) {
        for (int i = 0; i < 4; ++i) {
            if (per[i] == worst) { mask |= bit[i]; }
        }
    }
    if (sources != NULL) { *sources = mask; }
    return worst;
}

/* ---- Watchdog ------------------------------------------------------------- */

void wd_init(watchdog_t *w) {
    for (int i = 0; i < N_TASKS; ++i) { w->last[i] = w->heartbeat[i]; }
}

void wd_kick(watchdog_t *w, int id) {
    if (id >= 0 && id < N_TASKS) { w->heartbeat[id]++; }
}

health_t watchdog_check(watchdog_t *w, uint8_t *stalled_mask) {
    health_t worst = HEALTH_OK;
    uint8_t mask = 0;
    for (int i = 0; i < N_TASKS; ++i) {
        uint32_t now = w->heartbeat[i];
        if (now == w->last[i]) {          /* did not move since last check: stuck */
            worst = HEALTH_FAULT;
            mask |= (uint8_t) (1u << i);
        }
        w->last[i] = now;
    }
    if (stalled_mask != NULL) { *stalled_mask = mask; }
    return worst;
}

/* ---- Events --------------------------------------------------------------- */

uint8_t fdir_event_subtype(fdir_sev_t sev) {
    switch (sev) {
    case SEV_INFO:   return 1u;
    case SEV_LOW:    return 2u;
    case SEV_MEDIUM: return 3u;
    case SEV_HIGH:   return 4u;
    }
    return 1u;
}

const char *fdir_event_name(uint16_t evt_id) {
    switch (evt_id) {
    case EVT_WARN:            return "WARN";
    case EVT_FAULT:           return "FAULT";
    case EVT_RECOVERY_ACTION: return "RECOVERY_ACTION";
    case EVT_RECOVERED:       return "RECOVERED";
    case EVT_SAFE_ENTER:      return "SAFE_ENTER";
    case EVT_SAFE_EXIT:       return "SAFE_EXIT";
    }
    return "?";
}

/* Builds the Service 5 report and hands it to the OBDH router, the same path
 * every other packet takes: in contact it is downlinked, out of contact it
 * lands in the event store and is forwarded first on the next pass. The state
 * recorded is the one in effect AFTER the transition being reported. */
static void emit_event(fdir_ctx *c, uint16_t evt, fdir_sev_t sev, health_t worst) {
    uint8_t pl[FDIR_EVT_PAYLOAD_LEN];
    uint8_t pkt[MAX_PKT];

    pl[0] = (uint8_t) (evt >> 8);
    pl[1] = (uint8_t) (evt & 0xFFu);
    pl[2] = (uint8_t) c->state;
    pl[3] = c->attempts;
    pl[4] = (uint8_t) worst;
    pl[5] = c->source_mask;
    pl[6] = c->task_mask;

    size_t n = obdh_pack_tm(pkt, sizeof(pkt), APID_EVENT, c->ev_seq,
                            PUS_SERVICE_EVENT, fdir_event_subtype(sev), pl, sizeof(pl));
    if (n == 0) { c->n_event_lost++; return; }

    c->ev_seq = (uint16_t) ((c->ev_seq + 1u) % 16384u);
    c->n_events++;
    if (obdh_ingest(c->obdh, pkt, n) == DEST_DROP) { c->n_event_lost++; }
}

/* ---- State machine -------------------------------------------------------- */

void fdir_init(fdir_ctx *c, obdh_ctx *obdh, const fdir_hooks *hooks) {
    memset(c, 0, sizeof(*c));
    c->obdh           = obdh;
    c->state          = FDIR_NOMINAL;
    c->confirm_cycles = (uint8_t) FDIR_CONFIRM_CYCLES;
    c->prev_worst     = HEALTH_OK;
    if (hooks != NULL) { c->hooks = *hooks; }
}

static recovery_action_t action_for_attempt(unsigned attempt) {
    if (attempt <= 1u) { return RECOVERY_RETRY; }
    if (attempt == 2u) { return RECOVERY_RESET_DEVICE; }
    return RECOVERY_SWITCH_REDUNDANT; /* attempt 3, and anything beyond if the cap is raised */
}

fdir_state_t fdir_step(fdir_ctx *c, health_t worst, uint8_t sources) {
    health_t raw = worst;

    switch (c->state) {
    case FDIR_NOMINAL:
        if (worst == HEALTH_FAULT) {
            if (c->fault_streak < 255u) { c->fault_streak++; }
            uint8_t need = (c->confirm_cycles == 0u) ? 1u : c->confirm_cycles;
            if (c->fault_streak >= need) {
                /* Detection confirmed. Isolation: latch which monitors are
                 * responsible; recovery starts on the NEXT cycle. */
                c->fault_streak = 0;
                c->n_faults++;
                c->attempts    = 0;
                c->source_mask = sources;
                c->task_mask   = c->stalled_now;
                c->state       = FDIR_RECOVERING;
                emit_event(c, EVT_FAULT, SEV_MEDIUM, worst);
            } else {
                c->n_filtered++; /* one noisy reading: counted (see HK), not acted on */
            }
        } else {
            c->fault_streak = 0;
            if (worst == HEALTH_WARN && c->prev_worst == HEALTH_OK) {
                emit_event(c, EVT_WARN, SEV_LOW, worst); /* edge only: no per-cycle storm */
            }
        }
        break;

    case FDIR_RECOVERING:
        c->source_mask |= sources;
        c->task_mask   |= c->stalled_now;
        if (worst == HEALTH_OK) {
            /* Checked first: a fault that clears is believed even on the
             * cycle where the attempts ran out. */
            c->state = FDIR_NOMINAL;
            emit_event(c, EVT_RECOVERED, SEV_LOW, worst);
            c->source_mask = 0;
            c->task_mask   = 0;
        } else if (c->attempts >= FDIR_MAX_ATTEMPTS) {
            /* Every allowed attempt has been made AND observed to fail. */
            c->state = FDIR_SAFE;
            c->n_safe_entries++;
            emit_event(c, EVT_SAFE_ENTER, SEV_HIGH, worst);
            if (c->hooks.enter_safe != NULL) { c->hooks.enter_safe(c->hooks.user, c->source_mask); }
        } else {
            /* Still not healthy (WARN counts as not recovered): next rung. */
            c->attempts++;
            recovery_action_t act = action_for_attempt(c->attempts);
            if (c->hooks.recover != NULL) { c->hooks.recover(c->hooks.user, act, c->source_mask); }
            emit_event(c, EVT_RECOVERY_ACTION, SEV_INFO, worst);
        }
        break;

    case FDIR_SAFE:
        /* Absorbing: no events, no actions. Exit only via fdir_exit_safe(),
         * i.e. the ground telecommand through the Session 8 dispatcher. */
        break;
    }

    c->prev_worst = raw;
    return c->state;
}

fdir_state_t fdir_cycle(fdir_ctx *c, spacecraft_state *s, watchdog_t *w) {
    uint8_t sources = 0;
    health_t wd = watchdog_check(w, &c->stalled_now);
    health_t worst = fdir_evaluate(s, wd, &sources);
    fdir_state_t st = fdir_step(c, worst, sources);
    s->mode = (st == FDIR_SAFE) ? MODE_SAFE : MODE_NOMINAL;
    return st;
}

int fdir_exit_safe(fdir_ctx *c) {
    if (c->state != FDIR_SAFE) { return 0; }
    c->state        = FDIR_NOMINAL;
    c->attempts     = 0;
    c->fault_streak = 0;
    emit_event(c, EVT_SAFE_EXIT, SEV_INFO, c->prev_worst);
    c->source_mask  = 0;
    c->task_mask    = 0;
    return 1;
}

static int exit_safe_trampoline(void *user) {
    return fdir_exit_safe((fdir_ctx *) user);
}

void fdir_attach_uplink(fdir_ctx *c) {
    c->obdh->uplink.exit_safe_hook = exit_safe_trampoline;
    c->obdh->uplink.hook_user      = c;
}

/* ---- Housekeeping --------------------------------------------------------- */

size_t fdir_build_hk(fdir_ctx *c, uint8_t *out, size_t cap) {
    uint8_t pl[FDIR_HK_PAYLOAD_LEN];
    pl[0] = (uint8_t) c->state;
    pl[1] = c->attempts;
    pl[2] = (uint8_t) c->prev_worst;
    pl[3] = c->source_mask;
    pl[4] = (uint8_t) (c->n_faults >> 8);        pl[5] = (uint8_t) c->n_faults;
    pl[6] = (uint8_t) (c->n_safe_entries >> 8);  pl[7] = (uint8_t) c->n_safe_entries;
    pl[8] = (uint8_t) (c->n_filtered >> 8);      pl[9] = (uint8_t) c->n_filtered;

    size_t n = obdh_pack_tm(out, cap, APID_FDIR_HK, c->hk_seq,
                            PUS_SERVICE_HOUSEKEEPING, PUS_HK_SUBTYPE, pl, sizeof(pl));
    if (n != 0) { c->hk_seq = (uint16_t) ((c->hk_seq + 1u) % 16384u); }
    return n;
}
