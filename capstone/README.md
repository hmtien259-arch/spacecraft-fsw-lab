# Capstone - Mini Flight Software Stack

One FreeRTOS application (POSIX port) that integrates the lab components: it samples spacecraft
state, produces and stores telemetry, accepts and verifies telecommands, and manages faults
under one scheduler and one OBDH data flow.

**Status: scope baseline (Session 13).** Requirements, scenarios, acceptance criteria and boundaries
are frozen. No integration code exists yet. Session 14 starts it with SCN-02.

| File | What it is |
|---|---|
| `requirements.md` | the frozen SYS / SW / OBDH / fault-injection requirements (28, stable IDs) and the change-control rule |
| `scenarios.md` | the six end-to-end scenarios with detailed pass conditions, and the scenario script format |
| `traceability.md` | living requirement-to-implementation-to-test table (every ID, status `pending` / `verified`) |
| `tests/fault_inject.h` | the test-build-only fault injection interface (`-DFSW_TEST`) and product-side hook |
| `tests/test_fault_inject.c` | SW-FI-01 isolation check |
| `report/outline.md` | report structure with a sketch of each section |
| `report/proposal.md` | the Session 12 proposal this capstone started from |
| `src/` | the application (Session 14 onwards) |

## Gates

```
bash tools/run_tests.sh                     # labs + capstone build and tests (-Werror, gcc; CC=clang, SAN=1)
bash tools/check_traceability.sh            # lab and capstone traceability
bash tools/check_traceability.sh --strict   # acceptance: nothing pending
bash tools/capstone_gates.sh all            # capstone tests, coverage, cppcheck, report
bash tools/demo_gates.sh                    # proves each gate goes red on an injected fault
```

Build conventions, which gates block a merge, and what is simulated, tested, reviewed or left to
hardware: `docs/12-capstone-baseline.md`.
