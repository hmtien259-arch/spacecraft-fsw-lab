# Housekeeping Telemetry - Requirements

Six atomic, testable, uniquely identified requirements for
`labs/01-foundation/hk_telemetry.c`. Each uses "shall", targets one behavior, and is
verifiable by inspection or by a unit test in `test_hk.c` (see `traceability.md`).

| ID | Requirement |
|---|---|
| HK-REQ-001 | The software shall sample the spacecraft state once per cycle. |
| HK-REQ-002 | The software shall emit one HK frame per cycle containing bus voltage, battery percentage, OBC temperature, battery temperature, mode, a frame counter, and a timestamp. |
| HK-REQ-003 | The software shall set the mode to SAFE when bus voltage falls below the defined threshold (6.0 V). |
| HK-REQ-004 | The frame counter shall increment by one on each cycle and shall not repeat within a run. |
| HK-REQ-005 | The software shall complete exactly ten cycles and then exit with success. |
| HK-REQ-006 | The battery percentage field shall always be reported within the physically valid range 0 to 100 inclusive. |

HK-REQ-006 was added during review (Session 4 between-session task: "add one requirement you
think is missing") - none of the original five constrain the *validity* of a reported value,
only its presence and its effect on mode. A frame with, say, `batt=140%` would be silently
accepted by HK-REQ-002 alone, which is exactly the kind of gap traceability is supposed to
catch.
