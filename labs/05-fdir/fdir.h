/* labs/05-fdir/fdir.h
 *
 * Session 10: Fault Detection, Isolation and Recovery over the Session 3-9
 * stack. Reuses spacecraft_state (labs/01-foundation/hk_state.h), the OBDH
 * router for every event (labs/04-obdh), and the Session 8 dispatcher for the
 * ground's way out of safe mode (TC[130,1]).
 *
 *   monitors (power / thermal / battery) --\
 *   watchdog (task heartbeats) ------------+--> worst severity + source mask
 *                                                   |
 *   NOMINAL --fault confirmed--> RECOVERING --attempts exhausted--> SAFE
 *      ^                             |                                |
 *      +-------- fault clears -------+      ground TC[130,1] --------+
 *
 *   every transition and every recovery action -> PUS Service 5 event
 *   -> obdh_ingest() -> downlink (in contact) or event store (out of contact)
 *
 * Event subtypes (Service 5, by severity) and the TC service number are lab
 * values: confirm against ECSS-E-ST-70-41 before real use (see
 * docs/08-fdir.md).
 */
#ifndef FDIR_H
#define FDIR_H

#include "../01-foundation/hk_state.h"
#include "../04-obdh/obdh.h"

#include <stddef.h>
#include <stdint.h>

/* ---- Thresholds (lab values; a real mission derives these from its power
 * and thermal budgets and from how fast each fault becomes critical). ------- */
#define V_FAULT                BUS_VOLTAGE_MIN /* 6.0 V: the Session 3 SAFE limit, now "fault" */
#define V_WARN                 7.0f
#define OBC_TEMP_WARN_C        60.0f
#define OBC_TEMP_FAULT_C       75.0f
#define BATT_TEMP_HIGH_WARN_C  40.0f
#define BATT_TEMP_HIGH_FAULT_C 50.0f
#define BATT_TEMP_LOW_WARN_C   0.0f
#define BATT_TEMP_LOW_FAULT_C  (-10.0f)
#define BATT_SOC_WARN_PCT      30u
#define BATT_SOC_FAULT_PCT     15u

/* ---- Escalation policy ------------------------------------------------------ */
#define FDIR_MAX_ATTEMPTS    3u /* local recovery actions before safe mode (rationale: docs/08-fdir.md) */
#define FDIR_CONFIRM_CYCLES  2u /* consecutive FAULT cycles before acting (stretch goal) */

typedef enum { HEALTH_OK, HEALTH_WARN, HEALTH_FAULT } health_t;

/* ---- Health monitors (Step 1) ------------------------------------------------
 * Each returns a severity, not a boolean, so the response can be proportionate.
 * Comparisons are strict (< and >), matching apply_limits(): exactly V_FAULT is
 * WARN, not FAULT. */
health_t monitor_power(const spacecraft_state *s);   /* bus voltage        */
health_t monitor_thermal(const spacecraft_state *s); /* OBC and battery temperature */
health_t monitor_battery(const spacecraft_state *s); /* state of charge    */
health_t health_worst(health_t a, health_t b);

/* Which monitor(s) produced a severity: bit mask, carried in events and HK. */
#define FDIR_SRC_POWER     0x01u
#define FDIR_SRC_THERMAL   0x02u
#define FDIR_SRC_BATTERY   0x04u
#define FDIR_SRC_WATCHDOG  0x08u

/* Runs all monitors and the watchdog result; returns the worst severity.
 * *sources (may be NULL) is set to the monitors AT that worst severity: the
 * isolation answer to "what is the fault coming from". 0 when worst == OK. */
health_t fdir_evaluate(const spacecraft_state *s, health_t watchdog_result, uint8_t *sources);

/* ---- Liveness watchdog (Step 2) ----------------------------------------------
 * Monitored tasks are the PERIODIC ones of the Session 6 system. vSafeModeTask
 * is deliberately not listed: it blocks on a semaphore and only runs on SAFE
 * entry, so "did its heartbeat advance" would flag a healthy task as stalled
 * forever. (An aperiodic task can be monitored by blocking with a timeout and
 * kicking on the timeout.) */
enum { TASK_SAMPLER, TASK_TELEMETRY, TASK_REPORTER, N_TASKS };

typedef struct {
    volatile uint32_t heartbeat[N_TASKS]; /* each task bumps only its own */
    uint32_t          last[N_TASKS];      /* snapshot at the previous check */
} watchdog_t;

/* Snapshots the current heartbeats. Call once all monitored tasks are started;
 * the first watchdog_check() then compares against this snapshot. */
void wd_init(watchdog_t *w);
/* Called by task `id` once per cycle (cheap: one increment, no lock). */
void wd_kick(watchdog_t *w, int id);
/* Compares every heartbeat with the previous snapshot. HEALTH_FAULT if any
 * advanced zero times since the last check. *stalled_mask (may be NULL) gets
 * bit i for each stalled task. The check interval must be longer than the
 * slowest monitored task's period. Wrap-around of the 32-bit counters is
 * harmless: only inequality is tested. */
health_t watchdog_check(watchdog_t *w, uint8_t *stalled_mask);

/* ---- Graduated state machine (Step 3) ---------------------------------------- */
typedef enum { FDIR_NOMINAL, FDIR_RECOVERING, FDIR_SAFE } fdir_state_t;

/* Local recovery ladder, one rung per attempt (the session's three examples:
 * retry an operation, reset a device, switch to a redundant unit). */
typedef enum {
    RECOVERY_RETRY = 1,
    RECOVERY_RESET_DEVICE = 2,
    RECOVERY_SWITCH_REDUNDANT = 3
} recovery_action_t;

typedef struct {
    /* Called for each local recovery attempt. */
    void (*recover)(void *user, recovery_action_t action, uint8_t sources);
    /* Called once on entry to SAFE: put the vehicle in its known, minimal,
     * power-positive configuration (sun-pointing, loads shed, radio on). */
    void (*enter_safe)(void *user, uint8_t sources);
    void *user;
} fdir_hooks;

/* Events (Step 3 / design principle "observable"). */
typedef enum { SEV_INFO = 1, SEV_LOW, SEV_MEDIUM, SEV_HIGH } fdir_sev_t;
enum {
    EVT_WARN            = 0x1001, /* OK -> WARN edge                         (LOW)    */
    EVT_FAULT           = 0x1002, /* fault confirmed, recovery starts        (MEDIUM) */
    EVT_RECOVERY_ACTION = 0x1003, /* one local recovery attempt              (INFO)   */
    EVT_RECOVERED       = 0x1004, /* fault cleared during recovery           (LOW)    */
    EVT_SAFE_ENTER      = 0x1005, /* recovery exhausted, safe mode           (HIGH)   */
    EVT_SAFE_EXIT       = 0x1006  /* ground commanded exit from safe mode    (INFO)   */
};

#define PUS_SERVICE_EVENT       5u
/* Service 5 report subtypes by severity: 1 informative, 2 low, 3 medium,
 * 4 high severity - lab values, to be confirmed against ECSS-E-ST-70-41. */
uint8_t fdir_event_subtype(fdir_sev_t sev);
const char *fdir_event_name(uint16_t evt_id);

/* Event payload, big endian:
 *   event_id u16 | fdir_state u8 | attempts u8 | worst_health u8 | source_mask u8 |
 *   task_mask u8
 * source_mask names the monitors behind the episode (FDIR_SRC_*); task_mask
 * names WHICH tasks stalled (bit i = task i, see N_TASKS), 0 if the watchdog
 * is not involved. Both are the episode's cause, kept until it ends. */
#define FDIR_EVT_PAYLOAD_LEN 7u
#define FDIR_HK_PAYLOAD_LEN  10u

typedef struct {
    obdh_ctx    *obdh;            /* every event goes through this router */
    fdir_hooks   hooks;
    fdir_state_t state;
    uint8_t      attempts;        /* recovery actions performed in this episode */
    uint8_t      fault_streak;    /* consecutive raw FAULT cycles (persistence filter) */
    uint8_t      confirm_cycles;  /* streak needed to act; default FDIR_CONFIRM_CYCLES */
    health_t     prev_worst;      /* previous cycle's raw severity (for the WARN edge) */
    uint8_t      source_mask;     /* monitors behind the current episode */
    uint8_t      task_mask;       /* stalled tasks behind the current episode */
    uint8_t      stalled_now;     /* stalled tasks seen by this cycle's watchdog check */
    uint16_t     ev_seq;          /* 14-bit sequence count of FDIR events (APID_EVENT) */
    uint16_t     hk_seq;          /* sequence count of FDIR HK (APID_FDIR_HK) */
    uint32_t     n_events;        /* events emitted */
    uint32_t     n_event_lost;    /* events the router refused (should stay 0) */
    uint16_t     n_faults;        /* episodes started (fault confirmed) */
    uint16_t     n_safe_entries;
    uint16_t     n_filtered;      /* FAULT readings rejected by the persistence filter */
} fdir_ctx;

void fdir_init(fdir_ctx *c, obdh_ctx *obdh, const fdir_hooks *hooks);

/* One state-machine step on an already-evaluated severity. Emits the events
 * and calls the hooks; returns the new state. Differs from the session's
 * sketch (fdir_step(state, worst, &attempts)) because it must emit events and
 * run recovery, which need context; see docs/08-fdir.md. */
fdir_state_t fdir_step(fdir_ctx *c, health_t worst, uint8_t sources);

/* One full FDIR cycle: watchdog + monitors -> worst -> fdir_step -> mode.
 * Sets s->mode (SAFE while in FDIR_SAFE, NOMINAL otherwise): in labs that use
 * FDIR this replaces apply_limits(), whose single-reading latch to SAFE cannot
 * coexist with graduated recovery. Returns the new state. */
fdir_state_t fdir_cycle(fdir_ctx *c, spacecraft_state *s, watchdog_t *w);

/* Ground exit from SAFE (what TC[130,1] ends up calling). Returns 1 and
 * returns to NOMINAL if the state was SAFE; returns 0 and changes nothing
 * otherwise. A persisting fault is re-detected by the normal path. */
int fdir_exit_safe(fdir_ctx *c);

/* Wires TC[130,1] in the OBDH's command dispatcher to this FDIR instance. */
void fdir_attach_uplink(fdir_ctx *c);

/* FDIR housekeeping (stretch goal): Service 3 report on APID_FDIR_HK,
 *   state u8 | attempts u8 | last_worst u8 | source_mask u8 |
 *   n_faults u16 | n_safe_entries u16 | n_filtered u16
 * Returns the packet length (0 if `cap` is too small); route it with
 * obdh_ingest() like any other packet. */
size_t fdir_build_hk(fdir_ctx *c, uint8_t *out, size_t cap);

#endif /* FDIR_H */
