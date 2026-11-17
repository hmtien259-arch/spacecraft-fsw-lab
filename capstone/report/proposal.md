# Capstone proposal (draft for Session 13)

**Mini flight software stack with an end-to-end fault-injection verification campaign.** I propose
to integrate the Session 3-12 pieces into one host-simulated stack for a small satellite: FreeRTOS
tasks (sampler, telemetry, uplink, FDIR) feeding the CCSDS/PUS TM/TC layer and the OBDH router
with store-and-forward, with the FDIR state machine taken from the generated model, wired
into the real tasks (still open from Session 10). The new work is a telecommand-driven fault
injector (force a monitored value out of range, silence a task heartbeat for N cycles) so every
FDIR path runs through the real dispatcher, router, event store and downlink on a simulated pass.
Deliverables: a requirements set that extends the existing HK, TMTC and FDIR IDs and adds OBDH
requirements, a test and verification plan with the system-level scenarios the CI pipeline runs on
every change, the requirement-to-test matrix and coverage evidence, and a short technical report
that states what is verified, what is only validated by review, and what remains open (timing,
target hardware).
