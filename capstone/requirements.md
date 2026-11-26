# Capstone requirements - Mini Flight Software Stack (baseline, frozen in Session 13)

This is the frozen requirement set for the capstone. **IDs are stable**: an ID is never reused or
renumbered. Changing, adding or deleting a requirement after the freeze needs a pull request that
updates, in the same change, the requirement here, its tests, its row in `traceability.md`, and
the report (see "Change control" at the end).

Every requirement is verified by **test** (T): a unit or integration test in `capstone/tests/`, a
scenario in `scenarios.md`, or both. Properties that only review can judge (are the thresholds
right for the mission? is the safe configuration appropriate?) are not written as requirements.
They are listed as review items in `report/outline.md` section 7.

"Heritage" names the lab requirement or component the capstone reuses. A heritage test proves the
lab component in isolation. It does **not** verify the capstone requirement: the capstone tests
must exercise the integrated stack.

Numbers that make a requirement testable (16383, 3 attempts, 2 cycles, 16 slots) are lab values
taken from the Session 3-11 code. They are fixed here so a test can assert them.

## System requirements

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SYS-01 | The stack shall operate autonomously between ground contacts: with no telecommand received, it shall keep sampling, producing housekeeping, monitoring health and acting on faults. | SCN-01, SCN-03 | lab 02, lab 05 |
| SYS-02 | The stack shall downlink telemetry as CCSDS Space Packets with PUS-C TM secondary headers and accept telecommands as CCSDS Space Packets with PUS-C TC secondary headers. | SCN-02, SCN-06 | TMTC-REQ-001, TMTC-REQ-003 |
| SYS-03 | The stack shall produce housekeeping telemetry periodically, once per housekeeping cycle. | SCN-01 | HK-REQ-002 |
| SYS-04 | The stack shall preserve telemetry across loss of ground contact and deliver it on the next contact. | SCN-01, SCN-05 | lab 04 `check_required_scenario` |
| SYS-05 | The stack shall detect faults, attempt recovery, and reach a safe state when recovery fails. | SCN-03, SCN-04 | FDIR-REQ-001..009 |
| SYS-06 | The stack shall report significant events (fault detection, recovery actions, safe mode entry and exit, telemetry loss) to the ground. | SCN-03, SCN-04, SCN-05 | lab 05 `check_events_via_obdh` |

## Software requirements - real time (SW-RT)

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SW-RT-01 | Tasks shall run under a fixed-priority preemptive scheduler with rate-monotonic priorities: a periodic task with a shorter period shall have a higher priority than every periodic task with a longer period. (Aperiodic tasks, such as lab 02's safe-mode task, are placed by urgency and listed with a reason.) | test (priority table check) | lab 02 |
| SW-RT-02 | Inter-task data shall pass via queues; state shared between tasks shall be accessed only while holding its mutex. | test, review | lab 02 `hk_freertos_ipc.c` |

## Software requirements - telemetry (SW-TM)

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SW-TM-01 | Housekeeping shall be emitted as a CCSDS TM packet with a PUS Service 3 (subtype 25) secondary header on the housekeeping APID. | test, SCN-01 | TMTC-REQ-001, TMTC-REQ-002 |
| SW-TM-02 | The packet sequence count shall increment by one per packet, independently per APID, and wrap from 16383 to 0. | test | TMTC-REQ-004 |

## Software requirements - telecommand (SW-TC)

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SW-TC-01 | Telecommands shall be parsed and dispatched by the (service, subtype) pair. | test | lab 03 `dispatch()` |
| SW-TC-02 | A Service 17 connection test shall be supported end to end: TC[17,1] received on the uplink shall produce TM[17,2] on the downlink. | test, SCN-02 | TMTC-REQ-005 |
| SW-TC-03 | Command acceptance and completion shall be reported with PUS Service 1 (TM[1,1] / TM[1,7]) as the acknowledgement flags request; a failure report shall always be sent. | test, SCN-02 | TMTC-REQ-005 |
| SW-TC-04 | Malformed or unknown commands shall be rejected with a Service 1 failure report (TM[1,2]) and shall not be executed, fully or partly. | test, SCN-06 | TMTC-REQ-006..008 |

## Software requirements - FDIR (SW-FD)

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SW-FD-01 | Health monitors (power, thermal, battery) shall classify the state as OK, WARN or FAULT. | test | lab 05 `check_monitors` |
| SW-FD-02 | A watchdog shall detect a stalled periodic task (a heartbeat that did not advance within one watchdog check interval) and report it as a FAULT naming the task. | test, SCN-04 | lab 05 `check_watchdog` |
| SW-FD-03 | Recovery shall be graduated: one local action per cycle (retry, device reset, switch to redundant unit), escalating to SAFE mode after FDIR_MAX_ATTEMPTS = 3 attempts without recovery. SAFE shall be left only by ground command TC[130,1]. | test, SCN-03 | FDIR-REQ-003, -005, -007 |
| SW-FD-04 | Each FDIR transition shall emit a PUS Service 5 event whose subtype gives its severity. | test, SCN-03 | FDIR-REQ-002..008 |
| SW-FD-05 | A fault shall persist for FDIR_CONFIRM_CYCLES = 2 consecutive cycles before any action, to reject noise; a shorter fault shall be counted as filtered. | test | FDIR-REQ-001 |

## Software requirements - OBDH (SW-OB), added in Session 13

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SW-OB-01 | Packets shall be routed to their destination (downlink, store, command dispatcher, drop) by APID and packet type; an unmapped APID shall be dropped and counted. | test | lab 04 `check_router` |
| SW-OB-02 | Housekeeping shall be stored while out of contact and forwarded in FIFO order on contact, before any real-time telemetry. | test, SCN-01 | lab 04 `check_required_scenario` |
| SW-OB-03 | Outgoing telemetry shall be stamped with onboard time at the point of production (the time is carried in the packet, not only next to it in the store). | test | **gap**: lab 04 stamps at storage time only |
| SW-OB-04 | The packet store shall be bounded (STORE_CAP = 16 packets per store), with an overwrite-oldest overflow policy and a cumulative dropped count reported in housekeeping. | test, SCN-05 | lab 04 `check_overflow_and_hk` |
| SW-OB-05 | Event telemetry shall take priority over housekeeping on the downlink: on contact the event store shall drain before the housekeeping store. | test, SCN-03 | lab 04 `check_event_priority_and_stamps` |
| SW-OB-06 | The packet store shall be clearable, and its fill level reportable, by telecommand. | test | **gap**: no such TC in lab 04 |
| SW-OB-07 | Onboard time shall be settable by telecommand, to support time correlation. | test | **gap**: no such TC in lab 04 |

## Software requirements - fault injection (SW-FI), added in Session 13

| ID | Requirement | Verified by | Heritage |
|---|---|---|---|
| SW-FI-01 | The fault injection interface shall exist only in builds made with `-DFSW_TEST`: a nominal build shall not contain it, and test code that calls it shall not compile without the flag. | test (isolation check) | new |
| SW-FI-02 | With no injection active, a test build shall behave exactly as a nominal build: the same inputs give byte-identical downlinked telemetry. | test (back to back) | new, same idea as the lab 06 back-to-back test |

## Changes from the Session 13 baseline (for the pull request)

| Change | Reason (one line) |
|---|---|
| SW-FI-01, SW-FI-02 added | Session 13 section 5 makes isolation a rule, but no requirement says so, so nothing would ever test it. |
| SYS-06 lists the events | "Significant events" with no list cannot be tested. |
| SW-RT-01 defines rate monotonic, for periodic tasks only | Says what to check (shorter period, higher priority). The aperiodic safe-mode task has no period to rank. |
| SW-TM-01 names subtype 25 | Service 3 alone does not identify the HK report. Subtype 25 is what lab 03 emits. |
| SW-TM-02 "independently per APID" made explicit | Each APID keeps its own count (lab 04 / lab 05 counters), so a test must check two APIDs. |
| SW-TC-03 "failure report always" | Kept from lab 03: a rejected command must never fail silently, even when its ack flags ask for nothing. |
| SW-TC-04 "fully or partly" | Partial execution before rejection is the failure that `check_never_execute_unvalidated` guards. |
| SW-FD-02, SW-FD-03, SW-FD-05 carry the numbers (interval, 3 attempts, 2 cycles) and the exit command | A test can only assert a defined count. |
| SW-OB-01 adds "dropped and counted" | Unmapped APIDs must be visible, not silently lost. |
| SW-OB-02 adds "before any real-time telemetry" | The ordering rule lab 04 already implements, now required. |
| SW-OB-04 fixes the policy as overwrite oldest | SCN-05's pass condition already assumes it. |
| SW-OB-05 defines "priority" as drain order | Within one contact, real-time packets are not queued, so drain order is the only priority there is. |

## Change control

After the freeze, a requirement changes only through a pull request that, in the same change:
1. edits the row here (an ID is never reused; a deleted requirement keeps its row, marked
   "Deleted: reason");
2. adds or changes the tests and the scenario that verify it;
3. updates its row in `traceability.md`;
4. updates the report section that describes it.

`tools/check_traceability.sh` fails if 1 and 3 disagree (see `docs/12-capstone-baseline.md`).
