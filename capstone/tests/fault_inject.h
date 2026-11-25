/* capstone/tests/fault_inject.h - TEST BUILD ONLY (compile with -DFSW_TEST)
 *
 * Fault injection for the capstone verification campaign (Session 13 section 5, SW-FI-01/02).
 * FDIR can only be verified if faults can be produced on demand and repeatably, so the scenario
 * harness (capstone/tests/, Session 14 onwards) drives these calls from a scenario script
 * (format: capstone/scenarios.md).
 *
 * Two halves:
 *
 *   1. The injection API (fault_inject, fault_clear, fault_clear_all, fault_active) and
 *      fault_id_t. These are declared ONLY when FSW_TEST is defined. In a nominal build they do
 *      not exist, so test code that calls them without the flag fails to compile (SW-FI-01).
 *      This deviates on purpose from the session sketch's "no-op in nominal build": a silent
 *      no-op would let a scenario run in the wrong build, inject nothing, and still report
 *      what it saw as if the fault had happened.
 *
 *   2. The product-side hook, FI_ACTIVE(id, &param), placed at the few points where a fault
 *      enters the flight code (sensor read, task heartbeat, uplink input, store put).
 *        - With FSW_TEST it asks the injector whether `id` is active and returns its parameter.
 *        - Without FSW_TEST it is the constant 0: its arguments are discarded by the
 *          preprocessor, the guarded branch is dead code, and the compiler removes it. The
 *          nominal binary contains no trace of the injector.
 *      SW-FI-02: with no injection active, FI_ACTIVE is 0 in a test build as well, so a test
 *      build must produce byte-identical telemetry to a nominal one (checked back to back).
 *
 * Hook pattern in product code (the only form allowed, so the nominal build stays clean):
 *
 *     #include "fault_inject.h"
 *     ...
 *     int fi_mv = 0;
 *     if (FI_ACTIVE(FI_LOW_VOLTAGE, &fi_mv)) {
 *         s->bus_voltage = (float) fi_mv / 1000.0f;
 *     }
 *
 * Build flags:
 *     nominal:  gcc ... -Icapstone/tests                  (hooks only, API absent)
 *     test:     gcc ... -Icapstone/tests -DFSW_TEST       (+ the injector, capstone/tests/
 *                                                         fault_inject.c, written in Session 14)
 * The CI capstone gate proves both directions on every change (tools/capstone_gates.sh):
 * a nominal translation unit that calls fault_inject() must FAIL to compile, and the same
 * unit with -DFSW_TEST must compile.
 */
#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#if defined(FSW_TEST)

#define FAULT_INJECT_ENABLED 1

/* Injection points. Parameter meaning per fault: */
typedef enum {
    FI_NONE = 0,
    FI_LOW_VOLTAGE,  /* param: forced bus voltage in millivolts (e.g. 5500 = 5.5 V < V_FAULT) */
    FI_OVERTEMP,     /* param: forced OBC temperature in 0.1 degC (e.g. 800 = 80.0 degC)      */
    FI_TASK_STALL,   /* param: task index (TASK_SAMPLER, TASK_TELEMETRY, TASK_REPORTER)
                      *        whose heartbeat is suppressed while active                    */
    FI_BAD_TC,       /* param: malformed TC variant handed to the uplink once:
                      *        1 bad CRC, 2 truncated, 3 length field inconsistent,
                      *        4 wrong APID, 5 wrong PUS version                             */
    FI_STORE_FULL,   /* param: store to fill to capacity: 0 housekeeping, 1 event           */
    FI_COUNT         /* number of IDs, not a fault                                           */
} fault_id_t;

/* Activates fault `id` with `param` until cleared. Re-injecting replaces the parameter.
 * One-shot faults (FI_BAD_TC, FI_STORE_FULL) act once when the hook is next reached
 * and then clear themselves. FI_NONE and out-of-range IDs are ignored. */
void fault_inject(fault_id_t id, int param);

/* Deactivates fault `id`. Clearing an inactive fault is not an error. */
void fault_clear(fault_id_t id);

/* Deactivates every fault: called by the harness at the start of every scenario. */
void fault_clear_all(void);

/* Product-side query behind FI_ACTIVE(). Returns 1 and stores the parameter in *param
 * (if param != NULL) when `id` is active, else returns 0 and leaves *param untouched. */
int fault_active(fault_id_t id, int *param);

#define FI_ACTIVE(id, param_ptr) fault_active((id), (param_ptr))

#else /* nominal build: no injector, hooks are the constant 0 */

#define FI_ACTIVE(id, param_ptr) 0

#endif /* FSW_TEST */

#endif /* FAULT_INJECT_H */
