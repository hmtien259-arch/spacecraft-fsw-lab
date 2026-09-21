# Session 5 - RTOS Notes

## Why WCET, not average time, drives schedulability

A schedulability proof has to hold for every possible run, not the typical one, because the
one run that matters in flight is whichever one actually happens on orbit - and it will not
volunteer to be the average case. If a task's execution time varies and the scheduler is only
checked against its average, the first time it hits its actual worst case (interrupt storm,
cache miss pattern, longest branch through a limit check) is the first time a deadline is
silently missed - and by then it is running on hardware nobody can walk up to. Designing to
WCET, with bounded loops and static allocation (as in `apply_limits()` and `sample_state()`
in Session 3), is what makes the proof possible before launch instead of a postmortem after
one.

## Priority inversion mitigation

FreeRTOS mutexes (`xSemaphoreCreateMutex`, used for the Session 6 stats block) implement
**priority inheritance**: a task blocked waiting for a mutex temporarily lends its priority to
whichever (possibly lower priority) task is holding it, so a medium priority task cannot keep
the low priority holder off the CPU and starve the high priority waiter indefinitely - the
Mars Pathfinder failure mode from Session 5. In a larger design this matters anywhere a
high-priority task (e.g. an ADCS control loop) can share a resource (a bus, a shared data
structure) with a low-priority task (e.g. a housekeeping or logging task) while a
medium-priority task also exists that could otherwise run unbounded in between.
