# Session 10 - Fault Detection, Isolation and Recovery (FDIR) Notes

Code: `labs/05-fdir/` (`fdir.c` monitors / watchdog / state machine / events, `fdir_test.c`
self checks). Build and run:

```
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c labs/04-obdh/*.c labs/05-fdir/*.c -o fdir && ./fdir
```

The binary runs the Session 9 checks first and then the Session 10 ones (see "Build notes").

## How the three stages map to the code

| Stage | Where | What it does |
|---|---|---|
| Detection | `monitor_power / thermal / battery`, `watchdog_check` | Each returns OK, WARN or FAULT; the worst severity wins. |
| Isolation | `fdir_evaluate` source mask, `task_mask` | Names the monitor(s) at the worst severity, and for a stall, which task. Carried in every event. |
| Recovery | `fdir_step`, `recover` / `enter_safe` hooks | Local recovery ladder, then safe mode only if it fails. |

Isolation here means *identifying* the source; actually containing it (power-cycling one unit,
cutting one load) is the job of the `recover` and `enter_safe` hooks, which are test doubles in
this lab.

## Monitors and thresholds

Strict comparisons, the same convention as Session 3's `apply_limits()` (a bus voltage of exactly
6.0 V is WARN, not FAULT). All values are **lab values**, not derived from a real power or
thermal budget.

| Monitor | WARN | FAULT |
|---|---|---|
| Power (bus voltage) | < 7.0 V | < 6.0 V (`V_FAULT = BUS_VOLTAGE_MIN`, the old SAFE limit) |
| Thermal, OBC | > 60 C | > 75 C |
| Thermal, battery | > 40 C or < 0 C | > 50 C or < -10 C |
| Battery state of charge | < 30 % | < 15 % |
| Watchdog | - | any monitored task did not advance its heartbeat |

Not implemented: the consistency checks and error-counter checks listed in Session 10 Section 3
(only limit checks and liveness checks exist).

## Watchdog design

Each monitored task bumps its own counter once per cycle; `watchdog_check()` compares every
counter with the snapshot from the previous check. Three things differ from the session's sketch:

- **State lives in a struct, not in `static` locals**, so tests can create fresh instances and the
  result does not depend on test order.
- **Start-up:** with `last[] = 0` and tasks that have not run yet, the sketch's first check reports
  a fault for every task. `wd_init()` snapshots the counters instead, and the check interval must
  be longer than the slowest monitored task's period.
- **Only periodic tasks are monitored.** In the Session 6 system `vSafeModeTask` blocks on a
  semaphore and runs only on SAFE entry, so "did the heartbeat advance" would flag a healthy task
  forever. The monitored set is sampler, telemetry and reporter. An aperiodic task can still be
  covered by blocking with a timeout and kicking on the timeout.

Known limit: the telemetry task is fed by the sampler's queue, so a dead sampler starves it and
both are reported stalled; the event carries the stalled-task mask, not a root-cause ranking.
On a real target this software check is the inner layer: a hardware watchdog would be kicked only
when every monitored task has checked in, as the last line of defence if FDIR itself hangs.

## State machine and escalation policy

```
 NOMINAL --fault confirmed--> RECOVERING --attempts exhausted and still faulty--> SAFE
    ^                              |                                                |
    +------- health OK ------------+          ground TC[130,1] ---------------------+
```

- **Confirmation (stretch goal):** a FAULT must be seen on `FDIR_CONFIRM_CYCLES = 2` consecutive
  cycles. A single noisy reading is counted in `n_filtered` (visible in FDIR housekeeping) but
  produces no event, because a noisy sensor would otherwise flood the event store.
- **Recovery ladder:** attempt 1 retry, attempt 2 reset the device, attempt 3 switch to the
  redundant unit (`recovery_action_t`). Each attempt is one cycle, and each is reported.
- **Escalation:** after `FDIR_MAX_ATTEMPTS = 3` attempts, if the next cycle is still not OK,
  safe mode. If the fault clears at any point, including on that last cycle, FDIR returns to
  NOMINAL with a recovery event.
- **WARN is not recovery:** inside RECOVERING a WARN reading still counts as "not recovered".

### Why 3 attempts

- There are three distinct rungs on the ladder (retry, reset, redundant unit). A fourth attempt
  would repeat a rung that already failed, and stopping at two would give up on the mission
  without ever trying the redundant unit.
- The upper bound is time: every attempt is a cycle during which a real fault keeps progressing
  (a draining battery does not wait), so the total must stay below the time the fault needs to
  become critical.
- Time to safe mode is `confirm + MAX_ATTEMPTS` cycles from the first bad sample:
  2 + 3 = 5 cycles, which is 1 s at the lab's 200 ms cycle (Timeline 2 below). On a real mission
  the cycle period would follow how long each recovery action takes to show an effect, and both
  numbers would be derived from the fault's time-to-criticality, which this lab does not model.

## Deviations from the session's sketch

1. **`fdir_step` takes a context** (`fdir_step(fdir_ctx *, health, sources)`), not
   `(state, worst, &attempts)`: it has to emit events and run recovery hooks.
2. **Off-by-one fixed.** The sketch tests `++attempts >= MAX_ATTEMPTS` before acting, so with
   MAX = 3 it performs only 2 recovery actions, and the third cycle escalates without trying.
   Here `attempts` counts actions performed, and escalation needs all `MAX` actions made *and*
   seen to fail. A mutation test with the sketch's condition fails the state-machine check.
3. **More events than the sketch's three,** because the design principle is "report every
   detection and every action": WARN edge, one per recovery attempt, and the ground's safe exit.
4. **`apply_limits()` is superseded** in labs that use FDIR. It latches SAFE on a single reading
   and never leaves, which cannot coexist with graduated recovery. `fdir_cycle()` sets
   `s->mode` from the FDIR state instead. The Session 3 function is untouched and Timeline 1
   runs it next to FDIR for comparison.
5. **`spacecraft_state` moved to `labs/01-foundation/hk_state.h`** (a behaviour-preserving
   refactor, committed separately): it was only defined inside `hk_telemetry.c`, whose
   non-static functions cannot be included twice.

## Events

Every event is a PUS Service 5 report built by `emit_event()` and handed to `obdh_ingest()`, the
same router every other packet uses: in contact it is downlinked; out of contact it goes into the
OBDH event store and is forwarded before housekeeping on the next pass.

| Event | id | Severity | TM subtype | Raised |
|---|---|---|---|---|
| `EVT_WARN` | 0x1001 | low | [5,2] | OK to WARN edge only |
| `EVT_FAULT` | 0x1002 | medium | [5,3] | fault confirmed, recovery starts |
| `EVT_RECOVERY_ACTION` | 0x1003 | informative | [5,1] | each local recovery attempt |
| `EVT_RECOVERED` | 0x1004 | low | [5,2] | health OK during recovery |
| `EVT_SAFE_ENTER` | 0x1005 | high | [5,4] | attempts exhausted |
| `EVT_SAFE_EXIT` | 0x1006 | informative | [5,1] | ground exit command executed |

Payload (7 bytes, big endian): `event_id u16 | fdir_state u8 | attempts u8 | worst_health u8 |
source_mask u8 | task_mask u8`. The state is the one in effect *after* the transition.

One escalation as downlinked (APID 0x066), from `./fdir`:

```
FAULT           TM[5,3]: 08 66 C0 00 00 0B | 20 05 03 00 00 | 10 02 01 00 02 01 00
RECOVERY_ACTION TM[5,1]: 08 66 C0 01 00 0B | 20 05 01 00 01 | 10 03 01 01 02 01 00
RECOVERY_ACTION TM[5,1]: 08 66 C0 02 00 0B | 20 05 01 00 02 | 10 03 01 02 02 01 00
RECOVERY_ACTION TM[5,1]: 08 66 C0 03 00 0B | 20 05 01 00 03 | 10 03 01 03 02 01 00
SAFE_ENTER      TM[5,4]: 08 66 C0 04 00 0B | 20 05 04 00 04 | 10 05 02 03 02 01 00
```

Reading the first line: event `10 02` (FAULT), state `01` (RECOVERING), attempts `00`, worst `02`
(FAULT), source `01` (power), task mask `00`. The last line: `10 05` SAFE_ENTER, state `02` (SAFE),
attempts `03`.

### Why reporting every FDIR action matters as much as the recovery itself

A recovery nobody can see is a spacecraft the ground can no longer trust: if FDIR quietly
retried, reset a unit and switched to the redundant one, the next pass would find a vehicle that
is no longer in the configuration the operators believe, with no way to know why or whether the
fault will come back. The event trail is what lets the ground reconstruct the sequence after the
fact (what failed, what was tried, what worked), decide whether the redundancy has been used up,
and tell a one-off glitch from a degrading unit. Silent recovery therefore hides the one piece of
evidence that decides what happens next.

## Ground exit from safe mode (stretch goal)

`TC[130,1]` goes through the Session 8 dispatcher to a hook that calls `fdir_exit_safe()`. Service
130 is in the mission-specific range, not an ECSS service. If FDIR is not in SAFE the command is
refused visibly (TM[1,8], failure code 7), not silently ignored. Exit does not mask a fault that is
still present: it is re-detected through the normal path and a new episode starts (checked in the
self test). The exit event is raised while the command executes, so on the downlink it precedes
the acceptance report, which the Session 8 path delivers after the handler returns.

## FDIR housekeeping (stretch goal)

`fdir_build_hk()` builds a Service 3 report on a new APID 0x068 (routed like any HK):
`state | attempts | last_worst | source_mask | n_faults u16 | n_safe_entries u16 | n_filtered u16`.
In SAFE after one escalation:

```
08 68 C0 00 00 0E | 20 03 19 00 00 | 02 03 02 01 00 01 00 01 00 01
```

## Timelines (from `./fdir`)

Timeline 1 is the real Session 3 HK profile (two consecutive low samples at cycles 4 and 5)
through FDIR with the default confirmation:

```
cyc=04 bus=5.80V raw=FAULT state=NOMINAL                      <- first reading, filtered
cyc=05 bus=5.70V raw=FAULT state=RECOVERING [FAULT TM[5,3]]    <- confirmed
cyc=06 bus=7.90V raw=OK    state=NOMINAL    [RECOVERED TM[5,2]]
```

Session 3's `apply_limits()` alone would be in SAFE from cycle 4 and stay there; FDIR reports one
fault and one recovery and never enters safe mode.

Timeline 2, persistent undervoltage from cycle 2:

```
cyc=02 bus=5.00V raw=FAULT state=NOMINAL                       <- filtered
cyc=03 bus=5.00V raw=FAULT state=RECOVERING attempts=0 [FAULT TM[5,3]]
cyc=04 ... attempts=1 [RECOVERY_ACTION TM[5,1]]    (retry)
cyc=05 ... attempts=2 [RECOVERY_ACTION TM[5,1]]    (reset)
cyc=06 ... attempts=3 [RECOVERY_ACTION TM[5,1]]    (redundant unit)
cyc=07 ... state=SAFE       mode=SAFE [SAFE_ENTER TM[5,4]]
```

## Build notes

- `labs/03-tmtc/main.c` still has a weak `main()`. `labs/04-obdh/obdh_test.c` owns `main()` and,
  when the FDIR sources are linked in, calls `fdir_selfcheck()` through a weak reference. Each
  build is then a regression of everything before it. (A first attempt made the Session 9
  `main()` weak too; with two weak definitions the linker silently picked the first one on the
  command line, so the Session 9 build ran the Session 8 checks.)
- `fdir_test.c` uses `sample_state()` and `apply_limits()` from `labs/03-tmtc` unchanged, which
  is why the Session 10 command line includes `labs/03-tmtc/*.c`.
- Test strength: eight deliberate bugs (the sketch's off-by-one, no persistence filter, SAFE not
  absorbing, events bypassing OBDH, a watchdog that never flags a stall, an ignored exit-safe
  hook, a thermal boundary changed from `>` to `>=`, a dropped stalled-task mask) were each injected in a scratch copy and each
  made the self checks fail.

## Not verified / open points

- **Service 5 subtypes** (1 to 4 by severity) and the **mission-specific service range** for
  Service 130 are lab values; confirm both against ECSS-E-ST-70-41 before real use, the same
  posture as Sessions 7 to 9.
- Thresholds and the cycle period are lab values, not derived from a power or thermal budget.
- The lab runs FDIR from a host loop; it is not wired into the FreeRTOS tasks of `labs/02-rtos`
  (that lab needs the kernel fetched). Mapping: the old `vSafeModeTask` role becomes the
  `enter_safe` hook, and the heartbeat bump becomes one `wd_kick()` per task cycle.
- Onboard time (Session 9 review point): events carry no timestamp yet; producer-side time in
  CUC or CDS is still the next architectural change.

## Capstone idea

Put a **fault injector on the uplink**: a ground telecommand that forces a monitored value out of
range or silences a task's heartbeat for N cycles. FDIR's recovery paths could then be exercised
end to end through the real TC dispatcher, the OBDH router and the downlinked events, on the host
build and on a target, instead of only from unit tests. It also gives the capstone's test report a
repeatable set of fault scenarios to reference.

## Question carried into Session 11

Once part of this logic is generated from a model (Model Based Development), where should the
line sit between the generated state machine and the hand-written parts that touch hardware and
the OBDH router, so that the recovery paths stay testable and traceable the way they are here?
