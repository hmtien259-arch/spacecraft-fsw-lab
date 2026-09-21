# Lab 02 - RTOS: HK Pipeline on FreeRTOS (POSIX port)

## Dependencies

The FreeRTOS kernel (with its POSIX simulator port) is **not vendored** into this repository -
it is fetched on demand into `labs/02-rtos/FreeRTOS/FreeRTOS-Kernel/` (gitignored) by:

```sh
tools/scripts/fetch_freertos.sh
```

`make` in this directory runs that fetch automatically if the kernel is missing.

## Build and run

```sh
cd labs/02-rtos
make pipeline          # Session 5 - two task sampler/telemetry pipeline
./build/hk_freertos

make ipc                # Session 6 - four task system with mutex + semaphore
./build/hk_freertos_ipc
```

Both reuse `spacecraft_state` / `sample_state()` / `apply_limits()` / `emit_hk_frame()` from
`labs/01-foundation/hk_telemetry.c` unmodified (compiled in with `HK_UNIT_TEST` defined, the
same trick `test_hk.c` uses, so that file's own `main()` stays out of the way).

## Session 5 - two task pipeline (`hk_freertos.c`)

- `vSamplerTask` (priority `tskIDLE_PRIORITY + 2`) samples at a fixed 5 Hz
  (`vTaskDelayUntil`), applies the SAFE-mode limit check, and sends the state plus its frame
  index and RTOS tick timestamp into a 4-deep queue.
- `vTelemetryTask` (priority `tskIDLE_PRIORITY + 1`) blocks on the queue and emits the HK
  frame as soon as a sample arrives.
- **Rate monotonic priority assignment**: the sampler is the only periodic deadline in this
  system, so it holds the higher priority; the telemetry task is purely event driven and
  holds the lower one.
- The pipeline runs a bounded 10 cycles (matching Sessions 3-4), then the telemetry task calls
  `vTaskEndScheduler()` so the demo exits cleanly instead of running forever.

## Session 6 - four task system (`hk_freertos_ipc.c`)

Adds two tasks on top of the Session 5 pipeline:

- `vReporterTask` (priority `tskIDLE_PRIORITY + 1`, 1 Hz): takes `xStatsMutex`, reads the
  shared `hk_stats` block (`nominal_frames`, `safe_frames`, `min_voltage`, `max_voltage`),
  prints a summary line, releases the mutex.
- `vSafeModeTask` (priority `tskIDLE_PRIORITY + 3`): blocked on `xSafeModeSem` (a binary
  semaphore) until `vSamplerTask` signals a SAFE-mode entry, then prints a response line -
  the placeholder for a real FDIR action later in the program.

`vSamplerTask` gains a fifth job, `update_stats()`, which takes `xStatsMutex` before touching
`g_stats` and releases it immediately after - the same mutex the reporter uses, so the two
can never observe or corrupt a half-updated struct.

Priorities (rate monotonic for the two periodic tasks, safety-escalated for the aperiodic
handler): `sampler(4) > safemode(3) > telemetry(2) > reporter(1)`. See
`docs/04-rtos-ipc.md` for the mutex-vs-semaphore rationale and the race condition it closes.
