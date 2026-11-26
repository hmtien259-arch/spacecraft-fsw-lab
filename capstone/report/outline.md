# Capstone technical report - outline

**Mini Flight Software Stack: an integrated, fault-injection-verified FreeRTOS flight software
stack.** Written alongside the code, not at the end: each Session 14+ pull request that adds a
scenario also adds its evidence to section 6 and its row to the traceability appendix. The
report will be `capstone/report/report.md`. Section titles below are checked by
`tools/capstone_gates.sh report`.

## 1. Introduction and mission context

A small LEO satellite needs onboard software that keeps it safe and observable between ground
passes, which may be hours apart. This section states that need, the question the capstone
answers (can the seven lab components, integrated under one scheduler, meet a frozen requirement
set with evidence?), and how the rest of the report is organised.

## 2. System overview and architecture

The four layers (foundation state model, FreeRTOS tasks, TM/TC and OBDH services, mode and FDIR
application) with one block diagram and one data-flow diagram from sensor sample to downlinked
packet. For each block it names the lab it comes from and what changed when it was integrated.
It also gives the task table: period, priority and what each task produces and consumes.

## 3. Requirements (system, software, OBDH), with traceability reference

A summary of the 28 frozen requirements in `capstone/requirements.md` by group (SYS, RT, TM, TC,
FD, OB, FI), the changes made at the Session 13 freeze with their reasons, and any change made
afterwards through change control. It refers to Appendix A for the full matrix rather than
repeating it.

## 4. Design (tasks, TM/TC, OBDH, FDIR, fault injection)

One subsection per area. Each records the design decisions and the alternatives rejected:
rate-monotonic priorities and queue sizes, per-APID sequence counters, router table and store
policy (overwrite oldest, events drain first), the generated FDIR step and its model, and the
fault-injection hooks that compile to nothing in a nominal build. It also covers the three
new pieces the labs did not have: time stamping at production, store and time telecommands.

## 5. Verification and validation approach

The test levels (unit, integration, back-to-back, scenario) and what each is trusted to show.
Also the CI gates and why each one blocks a merge, and the coverage targets and how gaps were
judged. It separates verification (tests against requirements) from validation (review: are the
thresholds and the safe configuration right?) and lists the review items, with who reviewed them.

## 6. Scenario results and evidence (test and run logs)

For each of SCN-01..06: the script, the decoded downlink log extract that shows each pass
condition, and the CI run that produced it. Failures found while bringing a scenario up are
reported here too, with the fix, because they are the most useful evidence the campaign produces.

## 7. Limitations and future work (the hardware boundary)

What is simulated (OBC and RTOS on the POSIX port, sensors, RF link, host clock, faults by
injection), and so what is not shown: a flight RTOS, a hardware watchdog, a real link and ground
station, radiation effects, worst-case timing on target (PIL/HIL), real attitude and power
control. Each limit comes with the evidence that would close it. Review-only items: requirement
completeness, safe-mode appropriateness, coding standard, event design.

## 8. Conclusion

Whether the acceptance criteria of Session 13 section 7 were met, item by item, with the numbers
(requirements verified, scenarios passing, coverage, CI status). It ends with what the author
would do differently and what the stack is now ready for.

## Appendices

- **A. Full traceability matrix**: `capstone/traceability.md` at the final commit, every row
  `verified`, plus the strict check's output.
- **B. Build and run instructions**: the exact commands for the nominal application, the test
  build, every gate (`tools/run_tests.sh`, `tools/capstone_gates.sh all`,
  `tools/check_traceability.sh --strict`), and the FreeRTOS kernel fetch.
- **C. Oral defense**: a short slide deck accompanies the report. It covers the architecture
  diagram, one scenario end to end (SCN-03), the coverage and traceability numbers, and the
  hardware boundary.
