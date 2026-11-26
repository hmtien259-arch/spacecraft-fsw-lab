# Capstone end-to-end scenarios (baseline, frozen in Session 13)

These scenarios are the system-level validation of the integrated stack. Each one is a scripted run
of the whole application, made in a `-DFSW_TEST` build, driven by a scenario script and judged by
a pass condition the harness checks automatically. A scenario passes only if every one of its
pass conditions holds. A run that does not reach the end of its script fails.

Each scenario's pass conditions are written so they can be checked on the **downlink** (the packets
the ground would receive) and on housekeeping counters. They do not depend on internal variables,
because a ground operator can only see the downlink.

## Scenario script format

A scenario is a plain text file, `capstone/tests/scenarios/SCN-xx.scn`, read by the test harness
(written in Session 14 onwards). One command per line, in time order; `#` starts a comment.

```
# <cycle>  <command>       <arguments>
  0        contact         off|on          # ground contact state
  5        inject          FI_LOW_VOLTAGE 5500   # fault_inject(id, param), see tests/fault_inject.h
  9        clear           FI_LOW_VOLTAGE        # fault_clear(id)
  12       uplink          TC 17 1 ack=0x9       # build a valid TC and hand it to the uplink
  12       uplink_raw      <hex bytes>           # hand arbitrary bytes to the uplink
  30       end                                   # last cycle; the run must reach it
```

`<cycle>` is the housekeeping cycle number (onboard time in cycles since boot), so a run does not
depend on host timing. The harness logs every downlinked packet in decoded form, and that log is
the evidence attached to the report (section 6).

## Scenarios

| ID | Scenario | Pass condition | Requirements |
|---|---|---|---|
| SCN-01 | Nominal orbit: produce HK, lose contact, regain contact. | HK stored while out of contact, drained in order on the pass. | SYS-01, SYS-03, SYS-04, SW-TM-01, SW-OB-02 |
| SCN-02 | Command round trip: uplink a connection test. | TC[17,1] yields TM[17,2] and a success verification. | SYS-02, SW-TC-02, SW-TC-03 |
| SCN-03 | Fault to safe mode: inject persistent low voltage. | Detect, attempt recovery, escalate to SAFE, emit events; ground command recovers. | SYS-01, SYS-05, SYS-06, SW-FD-03, SW-FD-04, SW-OB-05 |
| SCN-04 | Watchdog: stall a task heartbeat. | Watchdog raises a fault and FDIR responds. | SYS-05, SYS-06, SW-FD-02 |
| SCN-05 | Store overflow: exceed store capacity out of contact. | Oldest overwrite applied and dropped count visible in HK. | SYS-04, SYS-06, SW-OB-04 |
| SCN-06 | Rejected command: uplink a malformed telecommand. | TM[1,2] failure report and no execution. | SYS-02, SW-TC-04 |

The detailed pass conditions below refine the one-line ones above. They are what the harness
asserts.

### SCN-01 - Nominal orbit

Script: in contact for cycles 0-4, `contact off` at cycle 5, `contact on` at cycle 15, end at 20.

Pass when:
1. One HK packet (Service 3, subtype 25, housekeeping APID) is produced per cycle, 21 in total
   (cycles 0-20), with no telecommand uplinked during the run.
2. The 10 HK packets from cycles 5-14 (fewer than STORE_CAP = 16, so nothing is lost; loss is
   SCN-05) are not downlinked during cycles 5-14. On the pass all 10 drain in FIFO order
   **before** the cycle 15 real-time HK packet.
3. Every HK packet reaches the ground exactly once. The sequence counts on the downlink are
   0..20 with no gap or duplicate, and the production time stamps (SW-OB-03) are strictly
   increasing.
4. No FDIR event is downlinked (no fault injected), and `hk_dropped` in OBDH HK stays 0.

### SCN-02 - Command round trip

Script: in contact; at cycle 3 `uplink TC 17 1 ack=0x9` (acceptance and completion requested).

Pass when the downlink after cycle 3 contains, in this order: TM[1,1] acceptance success,
TM[17,2], TM[1,7] completion success. The verification reports carry the TC's packet ID and
sequence control. The command app's `executed` and `conn_tests` counters each rose by exactly one.

### SCN-03 - Fault to safe mode and ground recovery

Script: out of contact from cycle 0; at cycle 5 `inject FI_LOW_VOLTAGE 5500` (5.5 V, below
V_FAULT = 6.0 V), never cleared until cycle 20; `contact on` at cycle 15; at cycle 16
`uplink TC 130 1 ack=0x9`; at cycle 20 `clear FI_LOW_VOLTAGE`; end at 26.

Pass when:
1. The fault is confirmed only on the 2nd consecutive faulty cycle (SW-FD-05). No action is taken on
   the first.
2. Exactly three recovery actions follow, in the order retry, device reset, switch to redundant
   unit, one per cycle, each reported as an event.
3. SAFE is entered after the third unsuccessful attempt. The vehicle mode in HK reads SAFE, and an
   EVT_SAFE_ENTER high-severity event is emitted.
4. On `contact on` (cycle 15) every stored event is downlinked before any stored HK
   (SW-OB-05). The events are in the order they were raised.
5. TC[130,1] at cycle 16 is accepted and executed. EVT_SAFE_EXIT is downlinked and the mode
   returns to NOMINAL.
6. Because the fault persists until cycle 20, it is detected again after the exit (the ground
   command does not mask a real fault). After the injection is cleared, the stack returns to
   NOMINAL with EVT_RECOVERED.

### SCN-04 - Watchdog detects a stalled task

Script: in contact; at cycle 5 `inject FI_TASK_STALL 0` (suppress the sampler task's heartbeat);
`clear` at cycle 12; end at 20.

Pass when the first watchdog check after the stall reports a FAULT with the watchdog source bit and
the task mask naming the sampler task. FDIR confirms the fault (after the persistence filter)
and emits EVT_FAULT naming the watchdog source, then takes recovery actions as in SCN-03. After the
heartbeat resumes, FDIR returns to NOMINAL with EVT_RECOVERED (or, if SAFE was reached first, stays
in SAFE until commanded).

### SCN-05 - Store overflow out of contact

Script: out of contact from cycle 0; at cycle 2 `inject FI_STORE_FULL 0` (fill the HK store);
continue to produce HK until cycle 12; `contact on` at cycle 13; end at 15.

Pass when:
1. Every HK packet produced while the store is full overwrites the oldest one. The store never
   holds more than STORE_CAP = 16 packets and never refuses the newest packet.
2. The OBDH housekeeping report (APID_OBDH_HK) shows `hk_dropped` equal to the number of
   overwritten packets, and the count is cumulative (it does not reset on drain).
3. On the pass, the drained packets are the 16 newest, in FIFO order.

### SCN-06 - Rejected command

Script: in contact; at cycle 3 `inject FI_BAD_TC 1` (a TC[17,1] with a corrupted CRC); at cycle 4
`uplink TC 99 99 ack=0x9` (an unknown service/subtype); at cycle 5 `uplink TC 17 1 ack=0x9`
(a valid command after the bad ones).

Pass when:
1. Each of the two bad commands yields exactly one TM[1,2] acceptance failure with the matching
   failure code (bad CRC, unknown command), and no TM[17,2] or TM[1,7].
2. The `executed` counter does not change for the bad commands, and `rejected` rises by two.
3. The valid command at cycle 5 then succeeds as in SCN-02 (a rejection leaves the command path
   working).

## First scenario to implement (Session 14)

SCN-02 is first. It is the thinnest slice through the whole stack. Components it touches:
- uplink buffer (simulated RF link, in-process);
- telecommand task: `tc_parse()` / `dispatch()` / `uplink_handle_tc()` (lab 03);
- PUS Service 1 verification reports, `verify.c` (lab 03);
- Service 17 handler (lab 03);
- OBDH router `obdh_ingest()` with the TM reports re-entering it (lab 04);
- telemetry task and downlink callback (lab 02 task structure, lab 04 downlink);
- the scenario harness and script reader (new, `capstone/tests/`).

It needs no FDIR and no fault injection, so Session 14 can bring up the FreeRTOS application, the
harness and the downlink log on the simplest path first.
