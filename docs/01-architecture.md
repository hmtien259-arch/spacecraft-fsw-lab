# Session 3 - Flight Software Architecture, in My Own Words

## Spacecraft as a system

The OBC is the coordination hub. Every other subsystem (EPS, ADCS, TT&C, thermal,
propulsion, payload) reduces, from the software side, to the same three duties: **sample
state, act within limits, and report**. `labs/01-foundation/hk_telemetry.c` is a minimal,
single-subsystem instance of exactly that pattern: `sample_state()` samples, `apply_limits()`
acts (drives the mode to `SAFE` on a limit violation), and `emit_hk_frame()` reports.

## The five layers, bottom to top

1. **Boot and BSP** - the lowest layer: bootloader, board support package, memory scrubbing,
   low-level init. Nothing above this layer runs until it hands off a sane machine.
2. **RTOS kernel** - tasks, scheduling, timing, synchronization. This is what turns "a program
   that runs" into "a program with provable timing,".
3. **Drivers and HAL** - device drivers behind a hardware abstraction layer, so the same
   application logic can run unmodified on an emulator, a dev board, or the real OBC.
4. **Middleware and services** - OBDH, TM/TC, time, storage, parameter and event services.
   This is where the HK frame becomes a real CCSDS/PUS packet instead of a
   `printf` line.
5. **Application** - mode management, subsystem managers, FDIR, mission logic. This is where
   the SAFE-mode decision in `apply_limits()` eventually escalates into a full FDIR response
   (watchdogs, recovery) rather than just flagging a frame.

Cross-cutting all five layers: a single disciplined clock (**time**), continuous checks
feeding FDIR (**health monitoring**), and predictable worst-case timing (**determinism**) -
none of which is optional at any layer.

## Why this differs from ordinary embedded work

The delta that matters most to how I write code here: no field access, so correctness up
front beats "patch it later"; autonomy, so the software must stay safe for hours with no
operator; and determinism over throughput, so a late-but-correct result still counts as a
failure. That is why `apply_limits()` is a small, bounded, single-pass check rather than
anything with unbounded loops or dynamic allocation.