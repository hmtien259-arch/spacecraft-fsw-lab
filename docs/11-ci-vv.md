# Session 12 - Software Verification, Validation and CI Notes

Code: `tools/` (`run_tests.sh`, `coverage.sh`, `static_analysis.sh`, `check_traceability.py`,
`demo_gates.sh`) and `.github/workflows/ci.yml`. One command runs everything locally:

```
bash tools/run_tests.sh
```

Terms: **CI** = continuous integration (a pipeline that builds and tests every change
automatically). **V&V** = verification and validation. **IVV** = independent V&V (a team separate
from the developers). **ISVV** = independent *software* V&V (the same idea for software, common on
ESA missions). **ASan / UBSan** = AddressSanitizer / UndefinedBehaviorSanitizer (compiler
instrumentation that aborts on memory errors / undefined behaviour). **gcov** = gcc's coverage tool.

## Verification versus validation

Verification asks "did we build the software right?" (it meets its requirements and design).
Validation asks "did we build the right software?" (it meets the mission need). Everything in this
pipeline is verification. Nothing in CI validates: a perfectly verified FDIR with the wrong
thresholds still fails the mission, and only analysis, simulation with mission experts and
review can say so.

## Why independence finds defects an author cannot

An author tests what they believe the code does, so a wrong belief is built into both the code and
the test and the two agree. A different person (or team, in IVV/ISVV) starts from the requirement
instead of from the code, so they test the belief itself. This session produced one small
example: the Session 7 "sequence wraps 16383 to 0" check only looped over a local variable and
never touched the code that owns the counter, and it passed without ever exercising it until someone re-read what
it actually exercised.

## Where the existing tests sit

| Level | What it is | Tests in this repository |
|---|---|---|
| Unit | one function or component alone | `test_hk.c` (all five), `check_monitors`, `check_watchdog`, `check_persistence`, `check_defensive_paths`, `check_crc_and_roundtrip`, `check_tm_sequence_wrap`, `check_router`, `check_store`, `req_001..009` (generated FDIR alone) |
| Integration | components working together | `check_valid_command`, `check_unknown_command`, `check_malformed`, `check_never_execute_unvalidated` (TC through parser, dispatcher, verification reports), `check_watchdog_integration`, `check_events_via_obdh`, `check_ground_exit`, `check_fdir_hk`, the back-to-back comparison (generated against hand-written FDIR including real PUS packets) |
| System | the whole stack against a requirement | `check_required_scenario` (out-of-contact HK stored, drained in order on a pass), `scenario_session3_profile`, `scenario_persistent_fault`, the two FreeRTOS programs of lab 02 run to completion |

The shape is closer to a pyramid than an hourglass at the bottom, but the top is thin:

- No scenario drives a telecommand through FDIR to an event, the event store and a drain in one
  run, because FDIR is not yet wired into the FreeRTOS tasks (open since Session 10). The fault
  injector idea in the Session 10 PR is the system test that would close this.
- The lab 02 programs only prove they run ten frames and exit 0; they do not assert behaviour.
- Nothing measures timing (WCET) in CI, and nothing runs on target.

## What the pipeline does and what blocks a merge

| Job | What it checks | Blocks merge? | Why |
|---|---|---|---|
| Model-to-code and traceability | `generate.py --check`; `check_traceability.py` | Yes | If the committed C is not the model's output, or a requirement has no executed test, every other result is about something else. Both take seconds |
| Build and test, gcc and clang | all labs with `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion`; every self check; output markers proving each check group ran | Yes | A failing check or a warning is a defect. Two compilers because clang caught a sign-conversion in the CRC that gcc accepted |
| Sanitizers | the same checks under ASan and UBSan, aborting on the first report | Yes | A memory error can change no result and pass every assert; `tools/demo_gates.sh` shows exactly that |
| Static analysis | clang-tidy: analyzer, `bugprone-*`, `cert-*`; any finding fails | Yes | The tree is clean, so any finding is new. Two checks are off (Annex K `memcpy_s`, parameter swapping) and three are relaxed for test code, each reason written in the script |
| FDIR coverage gates | line and branch floors for the hand-written and the generated FDIR | Yes, the floors only | The floors sit just under today's numbers and catch a test being removed. They are not a target |
| Coverage summary and gcov files | the percentages, the missed lines and branches | No (informational) | A percentage does not say whether a gap matters; a person reads the list |

The workflow does not decide what blocks; the repository setting does. After the first push, add a
branch protection (ruleset) on `main` that requires the five job names above to pass and a
pull request review before merge. Without that rule a red run is only a warning.

Deliberately not in the pipeline: long soak or random runs beyond the 3 million back-to-back steps
(they fit in a second here, so they run on every change), tests on target hardware, and any check
that needs the network at test time, except the FreeRTOS kernel download, which is pinned to tag
`V11.3.0` so an upstream change cannot turn the pipeline red.

## Evidence that each gate can fail

`bash tools/demo_gates.sh` copies the repository, injects one fault, runs the gate and reports
RED (caught) or GREEN (missed). Output of the committed version:

```
RED    invert the SAFE mode condition (HK-REQ-003)                    01-foundation-test: labs/01-foundation/test_hk.c:40: test_safe_mode_on_low_voltage: Assertion `s.mode == MODE_
RED    FDIR escalation off by one (FDIR-REQ-005)                      05-fdir: labs/05-fdir/fdir_test.c:278: check_state_machine: Assertion `hn_cycle(&h, 5.0f) == FDIR_SAFE' failed
RED    compiler warning (unused variable) under -Werror               labs/04-obdh/store.c:2:12: error: 'unused_demo' defined but not used [-Werror=unused-variable]
RED    generated C edited by hand                                     generate.py: --check FAILED: labs/06-mbd/fdir_step_gen.c differ from the model's output.
RED    model edited, generated files not regenerated                  generate.py: --check FAILED: labs/06-mbd/fdir_step_gen.c, labs/06-mbd/fdir_step_gen.h, labs/06-mbd/traceabilit
RED    requirement added with no test                                 check_traceability.py: FAILED
RED    test renamed, requirement still names the old one              check_traceability.py: FAILED
RED    test defined but no longer called                              check_traceability.py: FAILED
RED    FDIR checks silently not linked into the binary                FAIL: 05-fdir: expected 'all Session 10 FDIR self checks passed' in output (checks did not run?)
RED    FDIR coverage drops (a test no longer called)                  COVERAGE GATE FAILED for labs/05-fdir/fdir.c: line coverage 97.0% < 99%; branch coverage 88.3% < 95%
RED    static analysis finding (null dereference)                     labs/03-tmtc/verify.c:3:54: error: Dereference of null pointer (loaded from variable 'p') [clang-analyzer-core
GREEN  memory error that changes no result: plain build               (not caught)
RED    the same memory error: sanitizer build                         FAIL: 03-tmtc: self checks returned non-zero
demo_gates: every gate went red on its fault (the plain build missing the memory error is the expected GREEN)
```

The one GREEN is intended: a stack read past a struct changes no result, so the plain build passes
and only the sanitizer job catches it. A gate that has never been seen failing proves nothing.

## Coverage and what the missed branches mean

`bash tools/coverage.sh` (gcc with `--coverage`, read with `gcov -b`), each component measured by
the tests that belong to it:

| Component | Lines | Branches taken |
|---|---|---|
| `labs/05-fdir/fdir.c` (Session 10 tests only) | 100.0% (165/165) | 96.8% (91/94) |
| `labs/06-mbd/fdir_step_gen.c` (Session 11 tests) | 99.4% (176/177) | 92.6% (50/54) |

The first measurement of `fdir.c` was 97.6% lines and 88.3% branches. Reading the report, not the
number, gave these gaps and decisions:

| Gap found | Decision |
|---|---|
| FDIR run without hooks (`recover` / `enter_safe` NULL) never tested | missing test, added in `check_defensive_paths` |
| `fdir_event_name` for WARN and SAFE_EXIT never printed | missing test, added |
| corrupted state value and invalid severity fallbacks | missing test, added |
| failure paths in `emit_event`: `obdh_pack_tm` returning 0, `obdh_ingest` returning DROP, in `emit_event` | not reachable with a valid packet size and the APID routing of this lab; left uncovered on purpose, counted by `n_event_lost` |
| streak saturation false branch (`fault_streak == 255` without acting) | not reachable: the streak resets when it reaches `confirm_cycles`, at most 255; the back-to-back random-context test reaches it anyway by starting from an arbitrary context |
| generated code: the effect-overflow guard (`n_effects` already full) | not reachable by construction: the generator computes the maximum effects per step from the model |

Coverage never proves correctness. A line executed by a test that asserts nothing about it counts
as covered. That is why the requirement tests and the back-to-back comparison, not the percentage,
are the evidence for FDIR.

## Requirements and tests

`docs/10-requirements-test-matrix.md` is generated by `tools/check_traceability.py`. It reads every
requirement table (HK-REQ, TMTC-REQ, FDIR-REQ), takes the test names written in each row and
requires that each is defined in a test source **and called**. A requirement with no such test
fails CI; so does a row naming a test that does not exist, and so does a stale matrix. Today:
23 requirements, all with at least one executed test.

Honest limits: the check proves a named test exists and runs, not that it is a good test for the
requirement. The TMTC-REQ requirements were written in this session by reading what Sessions 7 and
8 built, so they describe the code rather than drive it (said in `labs/03-tmtc/requirements.md`).
OBDH (Session 9) has no requirement IDs yet and so is not protected by this check.

## Problems this session's checks found

- The Session 7 sequence-wrap check tested a local loop. A real one now exercises the report
  builder that owns the counter (`check_tm_sequence_wrap`).
- The TM packet checks live in a weak `main()`, so the Session 9-11 binaries do not run them.
  The lab 03 build does, and the runner requires its output line.
- The checks use `assert()`; built with `-DNDEBUG` every one would pass vacuously. Each test file
  now stops compilation if `NDEBUG` is defined.
- Three checks had side effects inside `assert()` (`assert(++n < 32u)`), which disappear under
  `NDEBUG`. Rewritten.
- clang found an `int`/`unsigned` conversion in the CRC-16 (gcc, default flags, did not). Fixed
  with unsigned arithmetic; the published check value `0x29B1` still passes.
- A test compared two structs with `memcmp`, which also compares padding. The generator now emits
  `fdir_gen_ctx_same()` comparing members, so it follows the model when variables are added.
- Two duplicated `switch` branches and a missing `default` (clang-tidy), and the FreeRTOS clone
  was unpinned.

## Questions a reviewer should attack

These tests, scripts and requirements were written by the same author as the code, with an AI
assistant, so none of this is independent verification. Good first targets: whether the TMTC
requirement wording matches what the standard asks for, whether the coverage floors are the right
numbers, whether `cert-*` and `bugprone-*` is the right check set, and what a system-level test
for "FDIR sees a fault and ground gets the event after the pass" should assert.
