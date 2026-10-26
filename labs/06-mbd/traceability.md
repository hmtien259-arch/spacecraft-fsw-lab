# FDIR Model - Transition Traceability

> GENERATED from `fdir_model.txt` (SHA-256 `abb44330e856749a06d9da0eb0c92d7ab64059d45752ef789dfe7f968af11639`) by `generate.py`. Do not edit by hand;
> change the model or `requirements.md` and regenerate.

Forward: every model transition names the requirement(s) it realizes. Backward: every
requirement below is realized by at least one transition, or is listed as having none.

## Transitions

| ID | State | Condition | Next state | Actions | Requirements |
|---|---|---|---|---|---|
| T1 | NOMINAL | FAULT_CONFIRMED | RECOVERING | streak_clear; count_fault; attempts_clear; set_causes; emit_fault | FDIR-REQ-001, FDIR-REQ-002 |
| T2 | NOMINAL | FAULT | NOMINAL | streak_inc; count_filtered | FDIR-REQ-001 |
| T3 | NOMINAL | WARN_RISE | MONITORING | streak_clear; emit_warn | FDIR-REQ-008, FDIR-REQ-009 |
| T4 | NOMINAL | ANY | NOMINAL | streak_clear | FDIR-REQ-001 |
| T10 | MONITORING | FAULT_CONFIRMED | RECOVERING | streak_clear; count_fault; attempts_clear; set_causes; emit_fault | FDIR-REQ-001, FDIR-REQ-002, FDIR-REQ-009 |
| T11 | MONITORING | FAULT | MONITORING | streak_inc; count_filtered | FDIR-REQ-001, FDIR-REQ-009 |
| T12 | MONITORING | OK | NOMINAL | streak_clear | FDIR-REQ-009 |
| T13 | MONITORING | ANY | MONITORING | streak_clear | FDIR-REQ-009 |
| T5 | RECOVERING | OK | NOMINAL | add_causes; emit_recovered; clear_causes | FDIR-REQ-004 |
| T6 | RECOVERING | ATTEMPTS_MAX | SAFE | add_causes; count_safe_entry; emit_safe_enter; enter_safe | FDIR-REQ-005 |
| T7 | RECOVERING | ANY | RECOVERING | add_causes; count_attempt; run_recovery; emit_recovery_action | FDIR-REQ-003 |
| T8 | SAFE | ANY | SAFE | - | FDIR-REQ-006 |
| T9 | SAFE | GROUND_CMD | NOMINAL | attempts_clear; streak_clear; emit_safe_exit; clear_causes | FDIR-REQ-007 |

## Requirements

| Requirement | Transitions |
|---|---|
| FDIR-REQ-001 | T1, T2, T4, T10, T11 |
| FDIR-REQ-002 | T1, T10 |
| FDIR-REQ-003 | T7 |
| FDIR-REQ-004 | T5 |
| FDIR-REQ-005 | T6 |
| FDIR-REQ-006 | T8 |
| FDIR-REQ-007 | T9 |
| FDIR-REQ-008 | T3 |
| FDIR-REQ-009 | T3, T10, T11, T12, T13 |

Requirement texts and the tests that verify them: `requirements.md`.
