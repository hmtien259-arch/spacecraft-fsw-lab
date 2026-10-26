/* GENERATED FILE - DO NOT EDIT BY HAND.
 *
 * Step functions of the FDIR state machine generated from the model.
 * Source model : labs/06-mbd/fdir_model.txt
 * Model SHA-256: a8be9fc24fab90b5dccf8efb98047b7feed504b886873cc803948803ff86189b
 * Generator    : labs/06-mbd/generate.py
 * Regenerate   : python3 labs/06-mbd/generate.py --write
 *
 * A change in behaviour is a change to the model. Edit the model, regenerate, and commit the
 * model and the regenerated files together. `generate.py --check` fails if this file no
 * longer matches the model.
 */

#include "fdir_step_gen.h"
#include "../05-fdir/fdir.h" /* HEALTH_*, EVT_*, SEV_*, RECOVERY_*, FDIR_MAX_ATTEMPTS: the shared interface constants */

/* Runtime support: effects are appended in the order they are requested. */
static fdir_gen_effect *fdir_gen_push(fdir_gen_out *out) {
    fdir_gen_effect *e = NULL;
    if (out->n_effects < FDIR_GEN_MAX_EFFECTS) {
        e = &out->effect[out->n_effects];
        out->n_effects++;
        *e = (fdir_gen_effect){ 0 };
    } else {
        out->overflow = 1u;
    }
    return e;
}

static void fdir_gen_emit(const fdir_gen_ctx *c, fdir_gen_out *out,
                          uint16_t event_id, uint8_t severity, uint8_t worst) {
    fdir_gen_effect *e = fdir_gen_push(out);
    if (e != NULL) {
        e->kind        = FDIR_GEN_FX_EVENT;
        e->event_id    = event_id;
        e->severity    = severity;
        e->worst       = worst;
        e->state       = (uint8_t)c->state;
        e->attempts    = c->attempts;
        e->source_mask = c->source_mask;
        e->task_mask   = c->task_mask;
    }
}

static void fdir_gen_req_recover(const fdir_gen_ctx *c, fdir_gen_out *out,
                                 uint8_t action, uint8_t sources) {
    fdir_gen_effect *e = fdir_gen_push(out);
    (void)c;
    if (e != NULL) {
        e->kind        = FDIR_GEN_FX_RECOVER;
        e->action      = action;
        e->source_mask = sources;
    }
}

static void fdir_gen_req_enter_safe(const fdir_gen_ctx *c, fdir_gen_out *out, uint8_t sources) {
    fdir_gen_effect *e = fdir_gen_push(out);
    (void)c;
    if (e != NULL) {
        e->kind        = FDIR_GEN_FX_ENTER_SAFE;
        e->source_mask = sources;
    }
}

static const unsigned char fdir_gen_ids[FDIR_GEN_N_TRANSITIONS] = {
    1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u
};

static const char *const fdir_gen_texts[FDIR_GEN_N_TRANSITIONS] = {
    "T1 NOMINAL --FAULT_CONFIRMED--> RECOVERING",
    "T2 NOMINAL --FAULT--> NOMINAL",
    "T3 NOMINAL --WARN_RISE--> NOMINAL",
    "T4 NOMINAL --ANY--> NOMINAL",
    "T5 RECOVERING --OK--> NOMINAL",
    "T6 RECOVERING --ATTEMPTS_MAX--> SAFE",
    "T7 RECOVERING --ANY--> RECOVERING",
    "T8 SAFE --ANY--> SAFE",
    "T9 SAFE --GROUND_CMD--> NOMINAL",
};

unsigned fdir_gen_transition_id(unsigned index) {
    return (index < FDIR_GEN_N_TRANSITIONS) ? (unsigned)fdir_gen_ids[index] : 0u;
}

const char *fdir_gen_transition_text(unsigned index) {
    return (index < FDIR_GEN_N_TRANSITIONS) ? fdir_gen_texts[index] : "?";
}

const char *fdir_gen_state_name(fdir_gen_state_t s) {
    const char *name = "?";
    switch (s) {
    case FDIR_GEN_STATE_NOMINAL: name = "NOMINAL"; break;
    case FDIR_GEN_STATE_RECOVERING: name = "RECOVERING"; break;
    case FDIR_GEN_STATE_SAFE: name = "SAFE"; break;
    default: break;
    }
    return name;
}

void fdir_init_gen(fdir_gen_ctx *c, uint8_t confirm_cycles) {
    c->state = FDIR_GEN_STATE_NOMINAL;
    c->attempts = 0u;
    c->fault_streak = 0u;
    c->prev_worst = HEALTH_OK;
    c->source_mask = 0u;
    c->task_mask = 0u;
    c->n_faults = 0u;
    c->n_safe_entries = 0u;
    c->n_filtered = 0u;
    c->confirm_cycles = confirm_cycles;
}

fdir_gen_state_t fdir_step_gen(fdir_gen_ctx *c, const fdir_gen_in *in, fdir_gen_out *out) {
    const uint8_t worst = in->worst;
    const uint8_t sources = in->sources;
    const uint8_t stalled = in->stalled;
    const unsigned streak_next = (c->fault_streak < 255u) ? (unsigned)c->fault_streak + 1u : 255u;
    const unsigned need = (c->confirm_cycles == 0u) ? 1u : (unsigned)c->confirm_cycles;
    (void)worst;
    (void)sources;
    (void)stalled;
    (void)streak_next;
    (void)need;

    out->n_effects = 0u;
    out->transition = 0u;
    out->overflow = 0u;

    switch (c->state) {
    case FDIR_GEN_STATE_NOMINAL:
        if (worst == HEALTH_FAULT && streak_next >= need) {
            /* T1 NOMINAL --FAULT_CONFIRMED--> RECOVERING */
            out->transition = 1u;
            c->state = FDIR_GEN_STATE_RECOVERING;
            c->fault_streak = 0u;
            c->n_faults++;
            c->attempts = 0u;
            c->source_mask = sources;
            c->task_mask = stalled;
            fdir_gen_emit(c, out, EVT_FAULT, SEV_MEDIUM, worst);
        } else if (worst == HEALTH_FAULT) {
            /* T2 NOMINAL --FAULT--> NOMINAL */
            out->transition = 2u;
            c->state = FDIR_GEN_STATE_NOMINAL;
            c->fault_streak = (uint8_t)streak_next;
            c->n_filtered++;
        } else if (worst == HEALTH_WARN && c->prev_worst == HEALTH_OK) {
            /* T3 NOMINAL --WARN_RISE--> NOMINAL */
            out->transition = 3u;
            c->state = FDIR_GEN_STATE_NOMINAL;
            c->fault_streak = 0u;
            fdir_gen_emit(c, out, EVT_WARN, SEV_LOW, worst);
        } else {
            /* T4 NOMINAL --ANY--> NOMINAL */
            out->transition = 4u;
            c->state = FDIR_GEN_STATE_NOMINAL;
            c->fault_streak = 0u;
        }
        break;
    case FDIR_GEN_STATE_RECOVERING:
        if (worst == HEALTH_OK) {
            /* T5 RECOVERING --OK--> NOMINAL */
            out->transition = 5u;
            c->state = FDIR_GEN_STATE_NOMINAL;
            c->source_mask |= sources;
            c->task_mask |= stalled;
            fdir_gen_emit(c, out, EVT_RECOVERED, SEV_LOW, worst);
            c->source_mask = 0u;
            c->task_mask = 0u;
        } else if (c->attempts >= FDIR_MAX_ATTEMPTS) {
            /* T6 RECOVERING --ATTEMPTS_MAX--> SAFE */
            out->transition = 6u;
            c->state = FDIR_GEN_STATE_SAFE;
            c->source_mask |= sources;
            c->task_mask |= stalled;
            c->n_safe_entries++;
            fdir_gen_emit(c, out, EVT_SAFE_ENTER, SEV_HIGH, worst);
            fdir_gen_req_enter_safe(c, out, c->source_mask);
        } else {
            /* T7 RECOVERING --ANY--> RECOVERING */
            out->transition = 7u;
            c->state = FDIR_GEN_STATE_RECOVERING;
            c->source_mask |= sources;
            c->task_mask |= stalled;
            c->attempts++;
            fdir_gen_req_recover(c, out, c->attempts <= 1u ? RECOVERY_RETRY : (c->attempts == 2u ? RECOVERY_RESET_DEVICE : RECOVERY_SWITCH_REDUNDANT), c->source_mask);
            fdir_gen_emit(c, out, EVT_RECOVERY_ACTION, SEV_INFO, worst);
        }
        break;
    case FDIR_GEN_STATE_SAFE:
        {
            /* T8 SAFE --ANY--> SAFE */
            out->transition = 8u;
            c->state = FDIR_GEN_STATE_SAFE;
        }
        break;
    default:
        break; /* not a state of the model: do nothing */
    }

    /* ALWAYS */
    c->prev_worst = worst;

    return c->state;
}

int fdir_exit_safe_gen(fdir_gen_ctx *c, fdir_gen_out *out) {
    int taken = 0;

    out->n_effects = 0u;
    out->transition = 0u;
    out->overflow = 0u;

    switch (c->state) {
    case FDIR_GEN_STATE_SAFE:
        /* T9 SAFE --GROUND_CMD--> NOMINAL */
        out->transition = 9u;
        c->state = FDIR_GEN_STATE_NOMINAL;
        c->attempts = 0u;
        c->fault_streak = 0u;
        fdir_gen_emit(c, out, EVT_SAFE_EXIT, SEV_INFO, c->prev_worst);
        c->source_mask = 0u;
        c->task_mask = 0u;
        taken = 1;
        break;
    default:
        break; /* the command has no effect here */
    }

    return taken;
}
