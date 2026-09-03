# Housekeeping Telemetry - Bidirectional Traceability Matrix

Every requirement in `requirements.md` maps to the code that implements it and the test that
verifies it. Reading down: forward traceability (requirement -> implementation -> test).
Reading up from either code or test column: backward traceability (what requirement justifies
this line existing).

| Requirement | Implemented by | Verified by |
|---|---|---|
| HK-REQ-001 | `sample_state()` | `test_sample_state_first_call` |
| HK-REQ-002 | `emit_hk_frame()` | `test_frame_fields` |
| HK-REQ-003 | `apply_limits()` | `test_safe_mode_on_low_voltage`, `test_nominal_mode_stays_nominal` |
| HK-REQ-004 | `main()` loop (frame counter `frame`) | `test_full_run_counter_and_range` |
| HK-REQ-005 | `main()` loop (`HK_NUM_CYCLES` bound) | `test_full_run_counter_and_range` |
| HK-REQ-006 | `sample_state()` (`battery_pct` derivation) | `test_full_run_counter_and_range` |

All six requirements have at least one test; none rely on inspection alone. `test_hk.c` runs
every assertion above and prints `all tests passed` on success (`gcc -Wall -Wextra -std=c11
test_hk.c -o test_hk && ./test_hk`).
