/* labs/01-foundation/hk_state.h
 *
 * The spacecraft state record, HK constants and the Session 3 function
 * prototypes, split out of hk_telemetry.c (Session 10) so other translation
 * units - the FDIR monitors in labs/05-fdir - can use spacecraft_state
 * without #including a .c file that defines non-static functions (which
 * would give duplicate symbols at link time). No behaviour change: the
 * definitions and values below are exactly the ones hk_telemetry.c had.
 */
#ifndef HK_STATE_H
#define HK_STATE_H

#include <stdint.h>

typedef enum { MODE_BOOT, MODE_NOMINAL, MODE_SAFE } sc_mode_t;

typedef struct {
    float    bus_voltage;  /* volts */
    uint8_t  battery_pct;  /* percent */
    float    temp_obc;     /* celsius */
    float    temp_batt;    /* celsius */
    sc_mode_t mode;
} spacecraft_state;

#define HK_NUM_CYCLES      10
#define HK_CYCLE_MS        200            /* fixed cycle interval, 5 Hz */
#define BUS_VOLTAGE_MIN    6.0f           /* volts; below this -> SAFE mode */

/* Defined in hk_telemetry.c (see there for the behaviour of each). */
void sample_state(spacecraft_state *s);
void apply_limits(spacecraft_state *s);
void emit_hk_frame(const spacecraft_state *s, uint32_t frame);

#endif /* HK_STATE_H */
