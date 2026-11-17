# Lab 06 - Model-Based FDIR: Table, Generator, Back-to-Back (Session 11)

The Session 10 FDIR state machine as a text transition table, a generator that emits C from it, and
a test that proves the generated code behaves like the hand-written one. MBD = model-based
development: change the model, regenerate, never edit the output.

```
requirements.md -> fdir_model.txt --generate.py--> fdir_step_gen.c/.h, traceability.md
                                                      |
                              mbd_test.c: requirement tests + back-to-back vs labs/05-fdir
```

## Files

- `fdir_model.txt` - the model: states, variables, inputs, conditions, actions, transitions with
  requirement IDs. The single source of truth.
- `requirements.md` - FDIR-REQ-001..009 and the test that verifies each.
- `generate.py` - validates the model and writes the generated files. `--check` fails if they no
  longer match the model, `--trace` prints the traceability table, no option prints the C.
- `fdir_step_gen.c/.h`, `traceability.md` - **generated, do not edit**.
- `mbd_test.c` - requirement tests, back-to-back (exhaustive, random runs, random contexts) and
  model coverage. No `main()`: `labs/04-obdh/obdh_test.c` calls `mbd_selfcheck()`.

## Workflow

```
$EDITOR labs/06-mbd/fdir_model.txt
python3 labs/06-mbd/generate.py --write
python3 labs/06-mbd/generate.py --check
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c labs/04-obdh/*.c labs/05-fdir/*.c labs/06-mbd/*.c -o mbd && ./mbd
```

Design notes, evidence, mutation checks, the MONITORING change and the MBD-suitability note are in
`docs/09-mbd.md`.
