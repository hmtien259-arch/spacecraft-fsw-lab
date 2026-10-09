# Session 9 - Onboard Data Handling (OBDH) Notes

Code: `labs/04-obdh/` (`obdh.c` router and TM/TC integration, `store.c` packet store,
`obdh_test.c` self checks). Build and run:

```
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c labs/04-obdh/*.c -o obdh && ./obdh
```

(`labs/03-tmtc/main.c` defines a weak `main()` so the two directories link into one binary;
`labs/03-tmtc` still builds and runs on its own.)

## Routing table

| Packet | APID / type | Destination |
|---|---|---|
| Telecommand | any APID, type 1 | Command dispatcher (Session 8 `uplink_handle_tc`) |
| Housekeeping TM | 0x064, type 0 | in contact: downlink; out of contact: HK store |
| OBDH housekeeping TM | 0x067, type 0 | same as HK |
| Event TM | 0x066, type 0 | in contact: downlink; out of contact: event store (higher priority) |
| Command responses (TM[1,x], TM[17,2]) | 0x065, type 0 | downlink (the ground just spoke, so it is in contact) |
| Anything else | unmapped APID | drop and count (`n_drop_unknown`) |

Telemetry whose `data_length` disagrees with the bytes received, or that is too short to hold
a header, is dropped and counted separately (`n_drop_malformed`). TCs are not length-checked in
the router: the Session 8 path validates them and answers with TM[1,2], and a silent drop at
the router would defeat that.

Two deliberate differences from the Session 9 sketch:

- **Event APID is 102 (0x066), not 101.** The session table gives 101 as an example, but 101
  (0x065) is already `CCSDS_APID_CMD` from Session 8, the address of every TC and the source of
  every verification report. Reusing it would make events and command responses
  indistinguishable by APID, so events moved to 102.
- **Out of contact, events are stored** in a second store instead of being "downlinked" into
  nothing (this is the priority-store stretch goal). In contact they still go straight down, as
  in the sketch.

## Overflow policy: overwrite oldest

Each store is a 16-slot ring buffer. When it is full, the oldest packet is discarded to make
room for the new one, and `dropped` is incremented.

I chose overwrite-oldest over stop-storing for these reasons:

- Housekeeping is a time series, and what the ground needs most after a long blackout is the
  state of the spacecraft leading up to the pass. Stop-storing would preserve the oldest data and
  throw away the newest, which is the wrong end to lose.
- The loss is bounded and visible: the store never grows, never blocks the router, and the
  dropped count says exactly how many packets are missing, so the ground can see a gap.
- The two stores are separate, so a housekeeping flood can never evict stored events (checked in
  the self test: 40 HK packets into a full HK store leave a stored event untouched).

Trade-off, stated honestly: overwrite-oldest is right for routine HK but not obviously right for
events. If a first fault event is the oldest entry in a full event store, it is the one
discarded, and the first failure is often the most informative. A real mission would size the
event store for the worst blackout, or protect first-occurrence events; this lab keeps the same
policy for both to stay minimal.

Drain order across stores is events first, then HK, each FIFO. That means an event can reach the
ground before an older HK packet; the sequence counts and storage stamps let the ground put them
back in order.

## Why the dropped counter belongs in telemetry

A packet store that loses data silently makes the ground believe the telemetry it receives is
complete: an operator would read a gap in housekeeping as "nothing happened" instead of "we lost
data", and could not tell a quiet spacecraft from an overflowing one. Reporting the dropped
counter (and the fill level) in housekeeping turns data loss into an observable quantity, so the
ground can resize stores or change rates, and so FDIR can react to it. In this lab
`obdh_build_hk()` reports `hk_fill`, `ev_fill`, `hk_dropped`, `ev_dropped` and the unknown-APID
drop count; counters are cumulative and are not cleared by draining.

## Onboard time

The OBDH owns one clock (`obdh_ctx.onboard_time`, advanced by `obdh_tick()`). A packet is
stamped with it at the moment it is stored; stamps are non-decreasing in drain order, which the
self check verifies. Limits of this lab: the stamp is storage metadata only, it is not yet
carried inside the packet in a CCSDS time format (CUC or CDS), and the producers (HK, events) do
not stamp their own output at the point of production, which is where the session says the stamp
belongs. That is the natural next step.

## Evidence for the PR

Scenario from `obdh_test.c` Case A: five HK packets while out of contact (sequence 0..4), a
TC[17,1] with acceptance and completion requested, an unknown-APID packet, a ground pass, then
one real-time HK packet (sequence 5). What the ground receives, in order, all hex:

```
dl[0] TM[1,1]   08 65 C0 00 00 08 20 01 01 00 00 18 65 C0 05
dl[1] TM[17,2]  08 65 C0 01 00 04 20 11 02 00 01
dl[2] TM[1,7]   08 65 C0 02 00 08 20 01 07 00 02 18 65 C0 05
dl[3] HK seq 0  08 64 C0 00 00 12 20 03 19 00 00 41 E0 00 00 50 41 C8 00 00 41 A0 00 00 01
dl[4] HK seq 1  08 64 C0 01 00 12 20 03 19 00 01 41 E0 00 00 50 41 C8 00 00 41 A0 00 00 01
dl[5] HK seq 2  08 64 C0 02 00 12 20 03 19 00 02 ...
dl[6] HK seq 3  08 64 C0 03 00 12 20 03 19 00 03 ...
dl[7] HK seq 4  08 64 C0 04 00 12 20 03 19 00 04 ...
dl[8] HK seq 5  08 64 C0 05 00 12 20 03 19 00 05 ...   (real time, after the drain)
```

- The command responses (dl[0..2]) go down immediately, before the pass: the command path is
  real time and does not wait behind stored telemetry.
- HK 0..4 appear only after contact is regained, in order, and HK 5 (real time) comes after them.
- The unknown-APID packet produces nothing on the downlink; `n_drop_unknown == 1`.

Overflow case (Case C): 20 HK packets into a 16-slot store while out of contact. The OBDH HK
report, hex `08 67 C0 00 00 12 20 03 19 00 00 | 10 00 | 00 00 00 04 | 00 00 00 00 | 00 00 00 00`,
says fill 16, event fill 0, 4 HK dropped, 0 events dropped, 0 unknown. On contact the 16 newest
(sequence 4..19) drain in order.

## Question carried into Session 10 (FDIR)

FDIR will watch this data flow, and the dropped counter is itself reported through the same flow
it monitors. Should a rising dropped count or a store that stays near full be a fault that FDIR
acts on (for example reducing the HK rate, or entering safe mode), or only telemetry for the
ground to judge, and who owns the threshold when the report of the problem can itself be stored,
overwritten or delayed behind the very backlog it describes?

## Not verified / open points

- PUS Service 15 (on-board storage and retrieval) is only echoed conceptually here; the store is
  not controlled by TCs and there is no Service 15 subtype in this code.
- The OBDH housekeeping reuses Service 3 subtype 25 from Session 7, which is itself unverified
  against ECSS-E-ST-70-41. The event stand-in (Service 5, subtype 1) used by the self checks is
  likewise not checked against the standard, though the router never looks at it.
