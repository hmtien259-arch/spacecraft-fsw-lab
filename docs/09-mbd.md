# Session 11 - Model-Based Development (MBD) Notes

Code: `labs/06-mbd/` (`fdir_model.txt` the model, `generate.py` the generator, `fdir_step_gen.c/.h`
and `traceability.md` generated, `mbd_test.c` verification). Build and run:

```
python3 labs/06-mbd/generate.py --check      # generated files still match the model
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c labs/04-obdh/*.c labs/05-fdir/*.c labs/06-mbd/*.c -o mbd && ./mbd
```

The binary runs the Session 9 and 10 checks first and then the Session 11 ones.

Terms used here: **MBD** = model-based development: the model, not the code, is the source of truth
and the code is generated from it. **MIL** = model in the loop (run the model itself), **SIL** =
software in the loop (run the generated code on the host), **PIL** = processor in the loop (run it
on the target CPU). This lab does SIL plus a back-to-back comparison; PIL needs the target.

## The workflow

```
requirements.md --> fdir_model.txt --generate.py--> fdir_step_gen.c/.h + traceability.md
                         ^                                   |
                         |                                   v
                   change goes here                  mbd_test.c: requirement tests,
                   (never in generated files)        back-to-back vs Session 10 fdir_step()
```

- **Model**: a text transition table (`STATE`, `VAR`, `INPUT`, `LET`, `COND`, `ACTION`, then one
  row per transition: id, state, condition, next state, actions, requirement IDs). Rows are tried
  top to bottom and the last row of each state is `ANY`, so the model is total: every state handles
  every input. Text was chosen over a diagram because it diffs, reviews and merges like code.
- **Generator**: stdlib Python, about 350 lines. It validates before it generates (unknown state,
  condition, action or requirement ID; a state without a closing `ANY`; unreachable states;
  duplicate ids; missing variables) and emits a statically allocated, deterministic
  `init` + `step` pair. The model SHA-256 is in the banner of every generated file.
- **Component shape**: `fdir_step_gen(ctx, in, out)` returns its effects (events, recovery
  request, enter-safe request) in `out`; it does not call OBDH. The hand-written glue turns effects
  into PUS Service 5 packets. A new state in the model therefore never touches hand-written enums.
- **Guard against hand edits**: `generate.py --check` regenerates in memory and compares.
  Hand-editing the output fails it.

## Verification evidence

| Check | What it shows |
|---|---|
| Requirement tests `req_001..009` | The generated component alone does what `requirements.md` says, without reference to Session 10 code |
| Back-to-back, exhaustive | Every input sequence up to 8 steps from {OK, WARN, FAULT_PWR, FAULT_WD, EXIT} at confirm_cycles 0..3: 1,953,120 sequences |
| Back-to-back, random runs | 4000 seeded runs of 80 steps, confirm_cycles 0..5 |
| Back-to-back, random contexts | 400,000 single steps and exit commands from arbitrary contexts: attempts and streak up to 255, any masks, invalid state codes |
| Compared per step | state, attempts, streak, previous severity, both masks, all counters, every PUS event decoded from the real downlink packet (id, subtype, payload), every recovery and safe-mode hook call and its order |
| Model coverage | all 13 transitions fired (the 9 of v1 and the 4 new ones) |

About 3.07 million steps are compared in under a second.

**Mutation checks** (each in a scratch copy, then the self checks were run):

| Injected fault | Caught by |
|---|---|
| escalation `>=` changed to `>` in the model | `req_005_escalation` |
| persistence filter off by one | `req_001_persistence` |
| WARN edge ignores previous severity | `req_008_warning_edge` |
| recovery rungs swapped | `req_003_graduated_recovery` |
| causes not cleared on recovery | `req_004_recovery` |
| ground exit leaves `attempts` set | `req_007` / back-to-back event payload |
| MONITORING never returns to NOMINAL | `req_008` / `req_009` |
| fault transition drops `set_causes` | back-to-back mismatch (event source mask) |
| unknown condition, missing `ANY` row, unknown action | generator refuses to generate |
| hand edit of generated C | `generate.py --check`, and also `req_005_escalation` here |
| same bug in the hand-written Session 10 `fdir.c` | Session 10 self checks |

One caution: an edit to generated code can pass the tests if the tests do not exercise it (a first
attempt hit a comment-level token and every test passed). `--check` is what makes hand edits
detectable, not the tests.

## Change the model, not the code: MONITORING

Request: a WARN should move NOMINAL into a monitoring state instead of staying in NOMINAL.

Hand-made changes (see the commit `feat: add MONITORING state ...`):

1. `fdir_model.txt`: one `STATE MONITORING = 3` line, four rows `T10..T13`, and `T3` now targets
   MONITORING.
2. `requirements.md`: FDIR-REQ-009.
3. `mbd_test.c` had been made version aware first (a separate commit), so the same file verifies v1
   and v2.

Then `generate.py --write`: 44 lines changed in `fdir_step_gen.c`, 10 in the header, the
traceability table regenerated, no C typed by hand. The new behaviour: a rise to WARN enters
MONITORING (EVT_WARN as before), WARN persists there without recovery or timeout, OK returns to
NOMINAL silently, a confirmed FAULT acts as in NOMINAL.

**Why back-to-back still compares against Session 10.** The change is a *refinement*: MONITORING is
a finer view of NOMINAL. It is entered only on a rise from OK and left only on OK, so inside it the
previous severity is never OK and the WARN-edge condition can never be true there. Therefore every
event, action and counter is identical to Session 10 and the generated state, projected
MONITORING -> NOMINAL, must equal the hand-written state after every step. The projection is in
`as_ref_state()`. The exhaustive search and the 400,000 contexts confirm it. The random-context
test keeps MONITORING contexts reachable (previous severity above OK); a context that breaks that
invariant is unreachable and is outside the argument.

## Which parts of the stack suit MBD, which are better hand-coded

| Part | Approach | Why |
|---|---|---|
| FDIR state machine, mode manager (Session 10, 6) | MBD | Finite states and discrete decisions; the interesting bugs are missed transitions and wrong precedence, which a total table, validation and coverage expose. Reviewers can read the table; requirements trace to rows |
| Limits and monitor tables (Session 3, 10) | Data tables, generated | They are data, owned by systems engineers; a generator keeps ranges, units and requirement IDs in one place |
| APID routing table, PUS packet and HK layouts (Sessions 7-9) | Generate from a table / ICD | Offsets and field widths are mechanical and the cause of many silent bugs; the packing code itself can stay small and hand-written |
| Control laws (attitude, thermal) | MBD (Simulink/Xcos style) | Dynamics and gains are validated in simulation before code exists; hand-coding loses the MIL evidence |
| RTOS tasks, queues, mutexes, priorities (Sessions 5-6) | Hand-coded | The correctness is in timing, priority inversion and memory ownership, which a state table does not describe; WCET depends on the exact code |
| Drivers, ISRs, register access, DMA | Hand-coded | Hardware quirks, ordering and volatile access; the model has no notion of them |
| CRC, bit packing, store-and-forward memory management | Hand-coded, tested | Small, performance and memory sensitive, fully specified by a standard, and exhaustively testable directly |
| Bootloader and anything that must be tiny or auditable by hand | Hand-coded | Generated code adds size and a tool in the trust chain |

Rule of thumb: model what is **logic over a finite set of states and conditions, or data a
non-programmer should own**. Hand-code what is **about time, memory, hardware or bits**. In both
cases the generator is a tool whose output is trusted, so it needs its own evidence: here that is
validation plus the back-to-back comparison against an independent implementation. On a real
programme the tool would need qualification (ECSS-E-ST-40C and ECSS-Q-ST-80C ask for tool
assessment; checking this against the standards text is still open).

## Why hand-editing generated code defeats MBD

The next regeneration silently overwrites the edit, so either the fix is lost or nobody dares to
regenerate. The model then no longer describes the code that flies, so reviews, requirement
traceability and every test written against the model stop meaning anything.

## Limits of this lab

- The model language is a lab design, not Stateflow or SCXML; it has no hierarchy, timers or
  concurrency, and `ACTION` bodies are C snippets.
- Back-to-back shows the generated code equals Session 10's code on the inputs tried. It cannot
  show the requirements are right; that is what the requirement tests and review are for.
- Exhaustive depth is 8 steps. Deeper behaviour is covered by the randomized runs, not by proof.
- No MIL run of an executable diagram and no PIL on target.

## Stretch goals

- **Model coverage**: every transition fired; the test fails if one does not.
- **Traceability**: `traceability.md` is generated and lists each transition with its requirement
  IDs; the generator refuses unknown IDs, and `requirements.md` names the test for each.
- Scilab Xcos / Simulink: not done (no such tool in this environment).

## Question for Session 12 (software V&V and CI)

If the generated code is regenerated on every build, what should the CI pipeline check to be sure
the committed generated files are still the output of the committed model, and which of the
back-to-back, requirement and coverage checks must gate a merge versus only report?
