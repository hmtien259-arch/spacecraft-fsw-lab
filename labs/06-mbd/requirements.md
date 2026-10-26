# FDIR Decision Logic - Requirements

Atomic, testable, uniquely identified requirements for the FDIR state machine. Session 10 built
the logic by hand without a requirement list, so these were written down from its behaviour
(and from `docs/08-fdir.md`) when the logic became a model in Session 11. Every model transition
in `fdir_model.txt` cites the requirement(s) it realizes, and `traceability.md` (generated)
lists the result. "Verified by" names the tests in `mbd_test.c`; `b2b` is the back-to-back
comparison against the hand-written Session 10 `fdir_step()` / `fdir_exit_safe()`.

| ID | Requirement | Verified by |
|---|---|---|
| FDIR-REQ-001 | A FAULT severity shall be acted on only after it has been seen on `confirm_cycles` consecutive cycles; a shorter or interrupted run shall be counted as filtered and shall produce no event. | `req_001_persistence`, b2b |
| FDIR-REQ-002 | When a fault is confirmed in NOMINAL, FDIR shall enter RECOVERING with zero attempts, latch the monitors and tasks responsible, and report EVT_FAULT with medium severity. | `req_002_detection`, b2b |
| FDIR-REQ-003 | While RECOVERING and not healthy, FDIR shall perform exactly one local recovery action per cycle, in the order retry, device reset, redundant unit, and report each as EVT_RECOVERY_ACTION. | `req_003_graduated_recovery`, b2b |
| FDIR-REQ-004 | When health is OK while RECOVERING, FDIR shall return to NOMINAL, report EVT_RECOVERED and clear the latched causes; this takes precedence over escalation on the same cycle. | `req_004_recovery`, b2b |
| FDIR-REQ-005 | When FDIR_MAX_ATTEMPTS recovery actions have been performed and health is still not OK, FDIR shall enter SAFE, report EVT_SAFE_ENTER with high severity and request the safe configuration. | `req_005_escalation`, b2b |
| FDIR-REQ-006 | In SAFE, FDIR shall not change state, request actions or report events because of monitor severity. | `req_006_safe_absorbing`, b2b |
| FDIR-REQ-007 | On the ground exit command in SAFE, FDIR shall return to NOMINAL, report EVT_SAFE_EXIT and clear the latched causes; the command shall have no effect in any other state. | `req_007_ground_exit`, b2b |
| FDIR-REQ-008 | In NOMINAL, a rise from OK to WARN shall be reported once as EVT_WARN with low severity; staying in WARN shall not repeat the report and no recovery action shall be taken. | `req_008_warning_edge`, b2b |
| FDIR-REQ-009 | A reported warning shall move FDIR from NOMINAL to MONITORING, where it stays, without recovery actions or further events, while the severity is WARN; OK returns it to NOMINAL silently, and a confirmed FAULT acts as in NOMINAL. Behavior seen through events, actions and counters shall be identical to FDIR-REQ-001..008. | `req_009_monitoring`, b2b |
