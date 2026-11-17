/* GENERATED FILE - DO NOT EDIT BY HAND.
 *
 * Interface of the FDIR step component generated from the model.
 * Source model : labs/06-mbd/fdir_model.txt
 * Model SHA-256: abb44330e856749a06d9da0eb0c92d7ab64059d45752ef789dfe7f968af11639
 * Generator    : labs/06-mbd/generate.py
 * Regenerate   : python3 labs/06-mbd/generate.py --write
 *
 * A change in behaviour is a change to the model. Edit the model, regenerate, and commit the
 * model and the regenerated files together. `generate.py --check` fails if this file no
 * longer matches the model.
 */
#ifndef FDIR_STEP_GEN_H
#define FDIR_STEP_GEN_H

#include <stdint.h>

/* State codes: the value is what an event reports in its state field. */
typedef enum {
    FDIR_GEN_STATE_NOMINAL = 0,
    FDIR_GEN_STATE_RECOVERING = 1,
    FDIR_GEN_STATE_SAFE = 2,
    FDIR_GEN_STATE_MONITORING = 3
} fdir_gen_state_t;

#define FDIR_GEN_N_STATES 4u
#define FDIR_GEN_HAS_STATE_NOMINAL 1
#define FDIR_GEN_HAS_STATE_RECOVERING 1
#define FDIR_GEN_HAS_STATE_SAFE 1
#define FDIR_GEN_HAS_STATE_MONITORING 1

#define FDIR_GEN_N_TRANSITIONS 13u
#define FDIR_GEN_MAX_EFFECTS 2u

/* One cycle's inputs (INPUT lines of the model). */
typedef struct {
    uint8_t worst;
    uint8_t sources;
    uint8_t stalled;
} fdir_gen_in;

/* What a step asks the outside world to do, in order. The component performs none of it:
 * the integrator turns each effect into a PUS event or a call into the platform. */
typedef enum {
    FDIR_GEN_FX_NONE = 0,
    FDIR_GEN_FX_EVENT = 1,      /* report a PUS event                          */
    FDIR_GEN_FX_RECOVER = 2,    /* run one local recovery action (`action`)    */
    FDIR_GEN_FX_ENTER_SAFE = 3  /* put the vehicle in its safe configuration   */
} fdir_gen_fx_kind;

typedef struct {
    uint8_t  kind;         /* fdir_gen_fx_kind                                       */
    uint16_t event_id;     /* EVENT: EVT_*                                           */
    uint8_t  severity;     /* EVENT: SEV_*                                           */
    uint8_t  worst;        /* EVENT: health value the event reports                  */
    uint8_t  state;        /* EVENT: state after the transition (a state code)       */
    uint8_t  attempts;     /* EVENT: snapshot of the context at the moment of the event */
    uint8_t  source_mask;  /* EVENT, RECOVER, ENTER_SAFE: monitors responsible       */
    uint8_t  task_mask;    /* EVENT: stalled tasks responsible                       */
    uint8_t  action;       /* RECOVER: recovery rung                                 */
} fdir_gen_effect;

typedef struct {
    uint8_t         n_effects;                            /* valid entries in effect[]       */
    uint8_t         transition;                           /* id of the row that fired, 0 = none */
    uint8_t         overflow;                             /* 1 if an effect did not fit (a bug) */
    fdir_gen_effect effect[FDIR_GEN_MAX_EFFECTS];
} fdir_gen_out;

/* Component state (VAR lines of the model). */
typedef struct {
    fdir_gen_state_t state;
    uint8_t attempts;
    uint8_t fault_streak;
    uint8_t prev_worst;
    uint8_t source_mask;
    uint8_t task_mask;
    uint16_t n_faults;
    uint16_t n_safe_entries;
    uint16_t n_filtered;
    uint8_t confirm_cycles;
} fdir_gen_ctx;

/* Puts the component in its initial state. */
void fdir_init_gen(fdir_gen_ctx *c, uint8_t confirm_cycles);

/* One FDIR cycle. Returns the new state; the requests are in `out`. */
fdir_gen_state_t fdir_step_gen(fdir_gen_ctx *c, const fdir_gen_in *in, fdir_gen_out *out);

/* The ground exit command (GROUND_CMD rows). Returns 1 if a transition fired, 0 if the
 * command has no effect in the current state. */
int fdir_exit_safe_gen(fdir_gen_ctx *c, fdir_gen_out *out);

/* Names and transition table, for logs and for model-coverage reports. */
const char *fdir_gen_state_name(fdir_gen_state_t s);
unsigned fdir_gen_transition_id(unsigned index);     /* index < FDIR_GEN_N_TRANSITIONS */
const char *fdir_gen_transition_text(unsigned index);

#endif /* FDIR_STEP_GEN_H */
