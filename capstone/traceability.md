# Capstone requirement-to-test traceability (living table)

Every requirement ID in `requirements.md` has exactly one row here. The table is updated in the
same pull request as the code and tests it describes. **A requirement with no test is incomplete,
not merely untested.**

Columns, as read by `tools/check_traceability.sh` (it finds them by header name, so keep the
headers):

- **Implemented by**: the capstone function(s) or component(s). At the baseline this is the
  planned component and the lab code it comes from.
- **Verified by**: backticked names of tests in `capstone/tests/`. The check accepts a name only
  if it is defined in a capstone test source **and called** (the Session 12 rule). Write "-"
  while there is none.
- **Scenario**: SCN IDs from `scenarios.md`. The check fails on an ID that does not exist there.
- **Status**: `pending` (no test yet, allowed during the build sessions) or `verified` (at least
  one test listed in Verified by passes). The check fails on `verified` with no test. In
  `--strict` mode, the acceptance gate, it also fails on any `pending` row.
- **Planned tests**: notes for whoever writes the tests. The check ignores this column.

| Requirement | Implemented by | Verified by | Scenario | Status | Planned tests |
|---|---|---|---|---|---|
| SYS-01 | whole application: task set under one scheduler, no ground dependency | - | SCN-01, SCN-03 | pending | scenario runs with no TC uplinked |
| SYS-02 | `tc_parse()`, `obdh_pack_tm()` / HK packet builder (labs 03, 04) | - | SCN-02, SCN-06 | pending | scenario evidence only |
| SYS-03 | HK task + HK packet builder (labs 01, 03) | - | SCN-01 | pending | test_hk_one_per_cycle |
| SYS-04 | OBDH store-and-forward (lab 04) | - | SCN-01, SCN-05 | pending | scenario evidence only |
| SYS-05 | monitors + watchdog + FDIR step (labs 05, 06) | - | SCN-03, SCN-04 | pending | scenario evidence only |
| SYS-06 | FDIR event emission + OBDH event store (labs 04, 05) | - | SCN-03, SCN-04, SCN-05 | pending | scenario evidence only |
| SW-RT-01 | task table / priorities in the application start-up (lab 02) | - | - | pending | test_rate_monotonic_priorities |
| SW-RT-02 | queues between sampler, telemetry, TC and FDIR tasks; state mutex (lab 02) | - | - | pending | test_shared_state_under_mutex, plus review |
| SW-TM-01 | HK packet builder (lab 03 `main.c` build path) | - | SCN-01 | pending | test_hk_packet_header |
| SW-TM-02 | per-APID sequence counters (labs 03, 04, 05) | - | - | pending | test_seq_wrap_per_apid |
| SW-TC-01 | `dispatch()` (lab 03) | - | - | pending | test_dispatch_by_pair |
| SW-TC-02 | Service 17 handler in `uplink_handle_tc()` (lab 03) | - | SCN-02 | pending | test_conn_test_round_trip |
| SW-TC-03 | `verify.c` Service 1 reports (lab 03) | - | SCN-02 | pending | test_verification_reports |
| SW-TC-04 | `tc_parse()` / `dispatch()` (lab 03) | - | SCN-06 | pending | test_reject_unknown, test_reject_malformed |
| SW-FD-01 | `monitor_power()`, `monitor_thermal()`, `monitor_battery()` (lab 05) | - | - | pending | test_monitor_classification |
| SW-FD-02 | `watchdog_check()` (lab 05) | - | SCN-04 | pending | test_watchdog_stall |
| SW-FD-03 | `fdir_step()`: generated step from the lab 06 model | - | SCN-03 | pending | test_escalate_safe |
| SW-FD-04 | FDIR event emission through `obdh_ingest()` (lab 05) | - | SCN-03 | pending | test_event_per_transition |
| SW-FD-05 | persistence filter in `fdir_step()` (lab 06 model) | - | - | pending | test_persistence_filter |
| SW-OB-01 | `obdh_route()` / `obdh_ingest()` (lab 04) | - | - | pending | test_route_table |
| SW-OB-02 | `obdh_set_contact()` drain (lab 04) | - | SCN-01 | pending | test_store_forward_order |
| SW-OB-03 | **new**: production time stamp in the TM secondary header or payload | - | - | pending | test_stamp_at_production |
| SW-OB-04 | `store_put()` (lab 04) | - | SCN-05 | pending | test_store_overflow |
| SW-OB-05 | `obdh_set_contact()` event-first drain (lab 04) | - | SCN-03 | pending | test_event_before_hk |
| SW-OB-06 | **new**: store clear / fill-report TC handlers | - | - | pending | test_store_clear_tc, test_store_fill_report_tc |
| SW-OB-07 | **new**: time-set TC handler (lab 04 `onboard_time`) | - | - | pending | test_time_set_tc |
| SW-FI-01 | `capstone/tests/fault_inject.h` (FSW_TEST guard) | `test_fault_inject_isolation` | - | verified | nominal TU calling fault_inject() must not compile (CI capstone gate) |
| SW-FI-02 | fault-injection hooks compile to constants when inactive | - | - | pending | test_no_injection_back_to_back |

## Summary at the baseline

28 requirements: 1 verified (SW-FI-01, by the header isolation check written with the
interface), 27 pending. Three of the pending ones (SW-OB-03, SW-OB-06, SW-OB-07) need new code, not
only integration of lab code.
