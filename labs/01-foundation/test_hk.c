/* labs/01-foundation/test_hk.c
 *
 * Session 4 practical task - unit tests tracing back to requirements.md.
 * Plain assertions, no framework, as specified. Compiles hk_telemetry.c
 * directly with HK_UNIT_TEST defined, which suppresses that file's main()
 * (see the #ifndef HK_UNIT_TEST guard there) so both files share exactly
 * one implementation of sample_state()/apply_limits()/emit_hk_frame().
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DHK_UNIT_TEST labs/01-foundation/test_hk.c -o test_hk
 *   ./test_hk
 */
#define HK_UNIT_TEST
#include "hk_telemetry.c"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- HK-REQ-001: sample_state() populates the struct once per call ----- */
static void test_sample_state_first_call(void) {
    spacecraft_state s = { .mode = MODE_BOOT };
    sample_state(&s); /* first call in the process -> profile index 0 */

    assert(fabsf(s.bus_voltage - 8.10f) < 1e-6f);
    assert(s.battery_pct == 70);
    assert(fabsf(s.temp_obc - 22.0f) < 1e-6f);
    assert(fabsf(s.temp_batt - 18.0f) < 1e-6f);
    printf("PASS HK-REQ-001 sample_state populates plausible values\n");
}

/* --- HK-REQ-003: bus voltage below threshold forces MODE_SAFE ---------- */
static void test_safe_mode_on_low_voltage(void) {
    spacecraft_state s = { .bus_voltage = 5.0f, .mode = MODE_NOMINAL };
    apply_limits(&s); /* function under test */
    assert(s.mode == MODE_SAFE);
    printf("PASS HK-REQ-003 safe mode on low voltage\n");
}

/* --- HK-REQ-003 (converse): nominal voltage does not force SAFE -------- */
static void test_nominal_mode_stays_nominal(void) {
    spacecraft_state s = { .bus_voltage = 8.0f, .mode = MODE_NOMINAL };
    apply_limits(&s);
    assert(s.mode == MODE_NOMINAL);
    printf("PASS HK-REQ-003 nominal voltage stays nominal\n");
}

/* Helper: run one HK cycle and capture emit_hk_frame's stdout line into
 * `out` (size `out_sz`). Used by the frame-format, counter, and range
 * tests below, which all need to inspect what actually got printed. */
static void capture_one_frame(spacecraft_state *s, uint32_t frame,
                               char *out, size_t out_sz) {
    FILE *tmp = tmpfile();
    assert(tmp != NULL);
    FILE *saved_stdout = stdout;
    stdout = tmp;

    emit_hk_frame(s, frame);

    fflush(tmp);
    rewind(tmp);
    stdout = saved_stdout;

    size_t n = fread(out, 1, out_sz - 1, tmp);
    out[n] = '\0';
    fclose(tmp);
}

/* --- HK-REQ-002: one frame per cycle, all required fields present ------ */
static void test_frame_fields(void) {
    spacecraft_state s = { .mode = MODE_BOOT };
    sample_state(&s);
    apply_limits(&s);

    char line[256];
    capture_one_frame(&s, 42, line, sizeof(line));

    assert(strstr(line, "frame=0042") != NULL);
    assert(strstr(line, "bus=") != NULL);
    assert(strstr(line, "batt=") != NULL);
    assert(strstr(line, "t_obc=") != NULL);
    assert(strstr(line, "t_batt=") != NULL);
    assert(strstr(line, "mode=") != NULL);
    assert(strstr(line, "t=") != NULL);
    printf("PASS HK-REQ-002 frame contains all required fields\n");
}

/* --- HK-REQ-004 / HK-REQ-005 / HK-REQ-006: full 10-cycle run ----------- */
static void test_full_run_counter_and_range(void) {
    spacecraft_state s = { .mode = MODE_BOOT };
    uint32_t frames_seen = 0;

    for (uint32_t frame = 0; frame < HK_NUM_CYCLES; ++frame) {
        sample_state(&s);
        apply_limits(&s);

        char line[256];
        capture_one_frame(&s, frame, line, sizeof(line));

        /* HK-REQ-004: the frame counter printed must match the loop index,
         * i.e. it increments by exactly one and does not repeat. */
        char expect_tag[32];
        snprintf(expect_tag, sizeof(expect_tag), "frame=%04u", frame);
        assert(strstr(line, expect_tag) != NULL);

        /* HK-REQ-006: battery percentage stays within 0..100. uint8_t
         * already rules out negative values; the upper bound still needs
         * an explicit check. */
        assert(s.battery_pct <= 100);

        frames_seen++;
    }

    /* HK-REQ-005: exactly ten cycles were produced. */
    assert(frames_seen == HK_NUM_CYCLES);
    printf("PASS HK-REQ-004 frame counter increments without repeating\n");
    printf("PASS HK-REQ-005 exactly ten cycles completed\n");
    printf("PASS HK-REQ-006 battery percentage stays within 0..100\n");
}

int main(void) {
    test_sample_state_first_call();
    test_safe_mode_on_low_voltage();
    test_nominal_mode_stays_nominal();
    test_frame_fields();
    test_full_run_counter_and_range();

    printf("all tests passed\n");
    return 0;
}
