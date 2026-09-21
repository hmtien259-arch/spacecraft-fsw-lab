# Lab 01 - Foundation: Housekeeping Telemetry

## Frame format (Session 3 stretch goal)

One line per cycle, fixed field order, human-readable (not yet a wire format - that comes in
`labs/03-tmtc` once the CCSDS/PUS envelope is introduced):

```
HK frame=<counter> t=<sim_ms>_ms bus=<V>V batt=<pct>% t_obc=<C>C t_batt=<C>C mode=<mode>[ *SAFE*]
```

- `frame`: zero-based, increments once per cycle, does not repeat within a run (HK-REQ-004).
- `t`: simulated monotonic timestamp in milliseconds, advances by the fixed 200 ms cycle
  interval.
- `bus`, `batt`, `t_obc`, `t_batt`: the sampled `spacecraft_state` fields.
- `mode`: `BOOT`, `NOMINAL`, or `SAFE`.
- ` *SAFE*` suffix: present only when the frame was produced while in SAFE mode, so a log
  can be grepped for safety events without parsing every field.

## Build and run

```sh
gcc -Wall -Wextra -std=c11 labs/01-foundation/hk_telemetry.c -o hk
./hk
```

## Requirements, tests, and traceability (Session 4)

See `requirements.md`, `test_hk.c`, and `traceability.md` in this directory.

```sh
gcc -Wall -Wextra -std=c11 labs/01-foundation/test_hk.c -o test_hk
./test_hk
```

`test_hk.c` starts with `#define HK_UNIT_TEST` and then `#include "hk_telemetry.c"`, which
suppresses that file's own `main()` (guarded by `#ifndef HK_UNIT_TEST`) so the two share
exactly one implementation of `sample_state()`, `apply_limits()`, and `emit_hk_frame()`
without a duplicate-symbol conflict, and without needing a `-D` build flag.
