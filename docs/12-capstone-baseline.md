# Session 13 - Capstone Kickoff and Scope Freeze Notes

Baseline: `capstone/` (`requirements.md`, `scenarios.md`, `traceability.md`, `tests/fault_inject.h`,
`report/outline.md`) and the capstone gates in `tools/` and `.github/workflows/ci.yml`. No
integration code is written in this session: it fixes the target that Sessions 14 onwards build
and verify against.

Terms: **SCN** = end-to-end scenario. **FI** = fault injection. **PIL / HIL** =
processor-in-the-loop / hardware-in-the-loop (flight software on the target processor / with real
hardware around it). **Scope freeze** = from this point, a change to scope goes through change
control rather than being made silently.

## Architecture

One FreeRTOS application (POSIX port), four layers, each from the labs:

```
 Application   mode management, FDIR state machine (generated step, labs 05, 06)
 Services      TM/TC encode/decode + Service 1 verification (lab 03); OBDH router, packet stores,
               onboard time (lab 04); fault-injection hooks (test build only, new)
 Tasks         sampler, FDIR/monitor, telemetry, command, reporter under FreeRTOS (labs 02, 05)
 Foundation    spacecraft state model and housekeeping (lab 01)

 sensors (simulated) -> sampler --queue--> FDIR/monitor --events--> obdh_ingest -> downlink | stores
                             \--queue--> telemetry (HK TM) ----------^
 uplink buffer -> command task -> tc_parse/dispatch -> TM[1,x], TM[17,2] -> obdh_ingest
```

Proposed task set, to be fixed in Session 14 (SW-RT-01 orders the periodic tasks by period). Lab 02
had sampler 200 ms and reporter 1 s; the new tasks are slotted between them:

| Task | Period | Priority (high to low) | Note |
|---|---|---|---|
| sampler | 200 ms | 1 | lab 02, kicks the watchdog |
| FDIR / monitor | 200 ms after the sample, or event-driven | 2 | runs the generated FDIR step |
| command | aperiodic, blocks on the uplink queue | placed by urgency | like lab 02's safe-mode task, aperiodic tasks are listed with a reason |
| telemetry | 1 s HK cycle | 3 | builds HK TM, drives `obdh_tick()` |
| reporter | 1 s | 4 (lowest) | lab 02, console / OBDH HK |

## What was frozen and what changed from the brief

- **28 requirements**: SYS-01..06, SW-RT/TM/TC/FD (13), SW-OB-01..07, and two added:
  **SW-FI-01/02**. The brief makes fault-injection isolation a rule (section 5) but no requirement
  says so, and a rule with no requirement is never tested. The other refinements add the numbers
  and lists that make a requirement testable (3 attempts, 2 cycles, 16 slots, which events). Each
  is listed with a one-line reason at the end of `capstone/requirements.md`. That list is the PR
  note the between-session task asks for.
- **Scenarios**: the brief's six, each with a detailed pass condition the harness can assert from
  the downlink alone. SCN-01's out-of-contact gap is sized below the store capacity, so SCN-01
  tests ordering and SCN-05 tests loss, and a failure points at one of them.
- **Three OBDH requirements need new code, not only integration**: SW-OB-03 (lab 04 stamps a
  packet when it is *stored*, not when it is *produced*), SW-OB-06 (no store clear/report TC) and
  SW-OB-07 (no time-set TC). They are marked "gap" in `requirements.md` so the plan does not
  assume they exist.
- **Fault injection API absent, not a no-op, in a nominal build.** The brief's header comment says
  "no-op in nominal build". A no-op would let a scenario run in the wrong build, inject nothing and
  still produce a log that looks like evidence. The nominal build instead has no API at all, and
  calling `fault_inject()` there fails to compile. Product code reaches the injector only through
  `FI_ACTIVE(id, &param)`, which is the constant 0 without `-DFSW_TEST`, so the compiler removes
  the hook. The gate checks both directions.

## The capstone CI gates

| Gate | Where | Blocks merge? | Today |
|---|---|---|---|
| Build `labs/` and `capstone/` with `-Werror`, gcc and clang | `tools/run_tests.sh` (jobs *Build and test*, *Sanitizers*) | Yes | header isolation check only; product code from Session 14 |
| Every unit, integration and scenario test passes | same | Yes | each `capstone/tests/test_*.c` must exit 0 **and** print "passed" |
| Fault-injection isolation (SW-FI-01) | `capstone_gates.sh test` | Yes | nominal: API absent, hook constant; misuse fails to compile |
| Coverage: overall lines >= 80 %, FDIR branches >= 90 % | `capstone_gates.sh coverage` (job *Capstone coverage gates*) | Yes | **not armed**: no product code (printed on every run) |
| Static analysis, cppcheck, any finding fails | `capstone_gates.sh cppcheck` (job *Capstone static analysis*) | Yes | clean (nominal and FSW_TEST configs) |
| Traceability: requirements, scenarios, traceability and tests agree | `check_capstone_traceability.py` (job *Model-to-code and traceability*) | Yes | 28 requirements, 1 verified, 27 pending |
| Report present and complete | `capstone_gates.sh report` (same job) | Yes | outline with every section |
| Acceptance: nothing pending, coverage armed | `--strict`, `REQUIRE_ARMED=1` (job *Capstone acceptance readiness*) | **No, informational** until the capstone is declared complete, then required | red, as expected |

Branch protection: add *Capstone coverage gates* and *Capstone static analysis (cppcheck)* to the
required checks on `main`. Do **not** require *Capstone acceptance readiness* until acceptance.

**Why the gates are allowed to pass while there is nothing to measure.** "Fail if any requirement
has no linked test" from day one would turn `main` red until the last session, and a gate that is
always red gets ignored. So the baseline splits the rule:

- What can be checked now blocks now: every ID has exactly one traceability row; scenarios,
  requirements and traceability agree; a named test exists and is called; `verified` needs a test.
- What only completion can satisfy runs as the informational acceptance job. That job states what
  is still missing on every run.

The risk is that a NOT ARMED gate is forgotten. The acceptance job is the guard against that:
it cannot go green while anything is pending or unarmed.

**cppcheck and clang-tidy.** The labs keep clang-tidy (Session 12). The capstone adds cppcheck
because the brief asks for it, and the two tools find different things. Verified locally with
cppcheck 2.19. CI's Ubuntu 24.04 package is older (2.13), so the first CI run is the real
baseline for "any finding fails".

## The brief's traceability sketch cannot fail

The brief sketches `tools/check_traceability.sh` as a grep loop. Run as written on this baseline,
with GNU grep:

1. The regex `(SYS|SW-[A-Z]+|)-[0-9]+` has an empty alternative, so besides real IDs it extracts
   fragments: `-09` and `-24` from a date, `-02` from `SCN-02`.
2. `grep -rqs "-09" ...` parses `-09` as an option (9 lines of context) and the next word as the
   pattern, so it matches almost anything.
3. Even with a correct regex, it searches `traceability.md`, which deliverable 3 requires to
   list every ID. Every ID is always found.

`tools/demo_gates.sh` runs the sketch on today's tree, where 27 requirements have no test, and it
passes (the expected GREEN). `tools/check_traceability.sh` keeps the brief's name but runs the
Session 12 Python check for the labs and `tools/check_capstone_traceability.py` for the capstone.
The capstone check reads `traceability.md` by column and requires a named test that exists and is
called. It is the same lesson as Session 12's sequence-wrap check: **a gate has to be seen failing
before it is evidence**. `bash tools/demo_gates.sh` now shows each capstone gate going red:

```
RED    capstone requirement added, no traceability row                check_capstone_traceability.py: FAILED
RED    capstone row marked verified with no test                      check_capstone_traceability.py: FAILED
RED    capstone test named in traceability, never called              check_capstone_traceability.py: FAILED
RED    scenario coverage disagrees with traceability                  check_capstone_traceability.py: FAILED
RED    fault injector visible in the nominal build (SW-FI-01)         capstone/tests/test_fault_inject.c:45:2: error: #error "nominal build: the injection API must not be visible"
RED    report outline loses a required section                        FAIL: capstone: capstone/report/outline.md: no section heading containing 'Limitations'
RED    acceptance: a requirement still pending (--strict)             check_capstone_traceability.py: FAILED (strict)
RED    acceptance: coverage gate not armed                            FAIL: capstone: coverage gate not armed, but REQUIRE_ARMED=1 (acceptance run)
GREEN  brief's grep sketch, 27 requirements untested                  (not caught)
```

The coverage gate was also exercised before any product code exists, in a scratch copy with a
dummy `fdir_demo.c`: 75 % lines and branches fails, 100 % passes, and a product file that no test
links counts as fully missed.

## Scope boundaries

The deliverable claims what it demonstrates and no more:

| Category | Covers | Evidence in this repo |
|---|---|---|
| Simulated | OBC and RTOS (FreeRTOS POSIX port), sensor values (generated profile, lab 01), RF link (in-process buffers), onboard time (host clock / tick count), faults (injection) | none of these is evidence about real hardware |
| Verified by test | TM/TC encoding and decoding, OBDH routing and store-and-forward, FDIR logic, command verification, sequence wrap: unit, integration, scenario and back-to-back tests with coverage | `capstone/tests/`, CI |
| Validated by review | requirement completeness and consistency, the safe-mode configuration, the thresholds, coding standard, event and report design | pull request review, recorded in report section 5 |
| Requires target hardware (out of scope, future work) | flight RTOS (RTEMS, VxWorks) on a real OBC, hardware watchdog, real RF link and ground station, radiation and upsets, worst-case timing on target (PIL/HIL), real attitude and power control | report section 7 |

## Between-session tasks

- **Requirements added or changed, each with a one-line reason**: the table at the end of
  `capstone/requirements.md`, copied into the PR description.
- **Two or three sentences per report section**: `capstone/report/outline.md`.
- **First scenario for Session 14: SCN-02**, the command round trip. Components it touches: the
  uplink buffer; the command task with `tc_parse()`, `dispatch()` and `uplink_handle_tc()`;
  Service 1 reports (`verify.c`); the Service 17 handler; `obdh_ingest()`, with the reports
  re-entering it; the telemetry task and downlink callback; and the new scenario harness. It needs
  no FDIR and no injector, so the application, harness and downlink log come up on the simplest
  path first (`capstone/scenarios.md`).

## Questions a reviewer should attack

The requirements, scenarios and gates were written by the same author who will write the code,
with an AI assistant, so the freeze has had no independent review until the PR. Good first
targets:
- Are the six scenarios enough to show SYS-05? None injects two faults at once.
- Should SW-OB-05 mean more than drain order? Real-time packets in contact are never queued
  today, so there is nothing to prioritise.
- Is 80 % overall line coverage the right floor for code that is mostly integration glue?
- Does the informational acceptance job make the "every requirement has a test" rule toothless
  until the last session?
