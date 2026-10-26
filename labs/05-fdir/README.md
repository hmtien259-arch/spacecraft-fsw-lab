# Lab 05 - FDIR: Monitors, Watchdog and Graduated Recovery (Session 10)

A fault manager over the Session 3-9 stack: it reuses `spacecraft_state`, sends every event
through the OBDH router, and is commanded out of safe mode through the Session 8 dispatcher.

```
monitors (power/thermal/battery) + watchdog -> worst severity + source mask
   NOMINAL -> RECOVERING (retry, reset, redundant unit) -> SAFE   (ground TC[130,1] -> NOMINAL)
   every transition / action -> PUS Service 5 event -> obdh_ingest() -> downlink or event store
```

## Files

- `fdir.h` / `fdir.c` - severity monitors, heartbeat watchdog, the FDIR state machine
  (`fdir_step`, `fdir_cycle`), event emission, safe-exit hook and FDIR housekeeping.
- `fdir_test.c` - assert-based self checks (monitors, watchdog, state machine, persistence,
  watchdog integration, events via OBDH in and out of contact, ground exit, FDIR housekeeping, two
  printed timelines). It has no `main()`: `labs/04-obdh/obdh_test.c` runs the Session 9 checks and
  then calls `fdir_selfcheck()`.

Also touched for this lab: `labs/01-foundation/hk_state.h` (state type split out of
`hk_telemetry.c`, no behaviour change), `labs/03-tmtc/uplink.{h,c}` (TC[130,1] and its hook),
`labs/04-obdh/obdh.{h,c}` (APID 0x068 routed like HK).

## Build and run

```
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c labs/04-obdh/*.c labs/05-fdir/*.c -o fdir && ./fdir
```

## Policy at a glance

| Parameter | Value |
|---|---|
| Confirmation before acting | 2 consecutive FAULT cycles (`FDIR_CONFIRM_CYCLES`) |
| Local recovery attempts before safe mode | 3 (`FDIR_MAX_ATTEMPTS`): retry, reset, redundant unit |
| Time to safe mode | confirm + attempts = 5 cycles from the first bad sample |

Design notes, the rationale for the numbers, deviations from the session sketch, event table and
PR evidence are in `docs/08-fdir.md`. Thresholds, event subtypes and the Service 130 number are lab
values to confirm against ECSS-E-ST-70-41.
