/* labs/01-foundation/hk_telemetry.c
 *
 * Session 3 practical task - Housekeeping Telemetry Warm Up.
 *
 * Implements the sample -> act -> report pattern from Session 3 Section 2:
 * periodically sample a simulated spacecraft state and emit a fixed-format
 * housekeeping (HK) frame, with a simple bus-voltage limit check that drives
 * the vehicle into SAFE mode.
 *
 * apply_limits() and sample_state() are kept separate, free functions (not
 * static) so that we can unit test them directly by compiling this
 * file with -DHK_UNIT_TEST, which suppresses main().
 */
#ifndef HK_UNIT_TEST
#define _POSIX_C_SOURCE 200809L /* nanosleep, under -std=c11 strict mode */
#include <time.h>               /* only needed by the standalone program's loop */
#endif

#include <stdio.h>
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

static uint64_t g_sim_tick_ms = 0;        /* simulated monotonic timestamp */

/* Fills the state with plausible simulated telemetry. Values are a fixed,
 * deterministic profile (not random) on purpose: flight software tests must
 * be repeatable, and a deterministic profile lets unit tests assert exact
 * behavior instead of chasing a moving target. One cycle (index * 4) is
 * intentionally below the SAFE threshold to exercise apply_limits(). */
void sample_state(spacecraft_state *s) {
    static const float bus_profile[HK_NUM_CYCLES] = {
        8.10f, 8.05f, 8.00f, 7.95f, 5.80f, 5.70f, 7.90f, 7.95f, 8.00f, 8.05f
    };
    static uint32_t call_count = 0;
    uint32_t idx = call_count % HK_NUM_CYCLES;

    s->bus_voltage = bus_profile[idx];
    s->battery_pct = (uint8_t)(70u + (idx % 5u));
    s->temp_obc    = 22.0f + 0.3f * (float)idx;
    s->temp_batt   = 18.0f + 0.2f * (float)idx;

    call_count++;
}

/* HK-REQ-003: sets mode to SAFE when bus voltage falls below the defined
 * threshold. Once in SAFE, stays in SAFE (recovery is FDIR's job,
 * otherwise settles to NOMINAL once past BOOT. */
void apply_limits(spacecraft_state *s) {
    if (s->bus_voltage < BUS_VOLTAGE_MIN) {
        s->mode = MODE_SAFE;
    } else if (s->mode != MODE_SAFE) {
        s->mode = MODE_NOMINAL;
    }
}

static const char *mode_name(sc_mode_t m) {
    switch (m) {
        case MODE_BOOT:    return "BOOT";
        case MODE_NOMINAL: return "NOMINAL";
        case MODE_SAFE:    return "SAFE";
    }
    return "UNKNOWN";
}

/* HK-REQ-002: emits one fixed-format HK frame per cycle, with an
 * incrementing frame counter and a timestamp. Marks the frame when the
 * vehicle is in SAFE mode. */
void emit_hk_frame(const spacecraft_state *s, uint32_t frame) {
    printf("HK frame=%04u t=%6llu_ms bus=%.2fV batt=%3u%% "
           "t_obc=%5.1fC t_batt=%5.1fC mode=%-7s%s\n",
           frame, (unsigned long long)g_sim_tick_ms,
           (double)s->bus_voltage, s->battery_pct,
           (double)s->temp_obc, (double)s->temp_batt,
           mode_name(s->mode),
           s->mode == MODE_SAFE ? " *SAFE*" : "");
}

#ifndef HK_UNIT_TEST
int main(void) {
    spacecraft_state s = { .mode = MODE_BOOT };

    for (uint32_t frame = 0; frame < HK_NUM_CYCLES; ++frame) {
        sample_state(&s);
        apply_limits(&s);         /* HK-REQ-003 limit check -> MODE_SAFE */
        emit_hk_frame(&s, frame);

        struct timespec ts = { .tv_sec = 0, .tv_nsec = HK_CYCLE_MS * 1000000L };
        nanosleep(&ts, NULL);       /* fixed interval delay */
        g_sim_tick_ms += HK_CYCLE_MS;
    }
    return 0;
}
#endif
