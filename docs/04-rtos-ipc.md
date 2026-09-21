# Session 6 - IPC and Shared Resource Protection Notes

## Why a mutex, not a binary semaphore, for the stats block

`g_stats` in `labs/02-rtos/hk_freertos_ipc.c` is written by `vSamplerTask` and read by
`vReporterTask` - two tasks, one shared structure, so it needs mutual exclusion, not
signaling. A mutex is the right tool because it has **ownership** and carries **priority
inheritance**: if the (lower priority) reporter task were holding the lock when the (higher
priority) sampler needs it, the reporter's priority is temporarily raised to the sampler's so
it cannot be starved out by some unrelated medium-priority task, which is exactly the failure
mode that hit Mars Pathfinder (Session 5). A binary semaphore has no ownership concept and
cannot donate priority, so using one for this would silently reintroduce priority inversion
into the design.

## A race condition this design prevents

Without the mutex, `vSamplerTask` incrementing `g_stats.safe_frames` (a read-modify-write) and
`vReporterTask` reading the four `g_stats` fields to print them could interleave: the reporter
could read `min_voltage` from before a sampler update and `max_voltage` from after it,
printing a range that never actually existed as a single, consistent snapshot - or, on a
platform where the increment is not atomic, the counter could lose an update entirely if both
tasks touched it in the same window. Every access to `g_stats` goes through
`xSemaphoreTake(xStatsMutex, ...)` / `xSemaphoreGive(xStatsMutex)` specifically to rule that
out: only one task is ever inside the critical section, so a reporter print always reflects
one consistent point in time.

## Question carried into next session

How should the safe-mode signal (`xSafeModeSem` here) map onto a real downlink event once HK
frames become CCSDS/PUS packets - does entering SAFE mode become its own PUS Service 5 event
report alongside the next Service 3 HK report, or does it wait to be picked up passively by
the following housekeeping cycle? (Session 7's own preview answers this: Service 5 is exactly
for onboard events, so a dedicated event report is the better fit - see
`docs/05-tmtc.md`.)
