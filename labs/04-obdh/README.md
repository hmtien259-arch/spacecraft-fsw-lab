# Lab 04 - OBDH: Router, Packet Store and Store-and-Forward (Session 9)

Connects the Session 7 telemetry and Session 8 telecommand code into one onboard data handling
core. Every packet enters through `obdh_ingest()`; `obdh_route()` decides where it goes.

```
HK TM / Event TM / Telecommand -> obdh_ingest -> obdh_route(apid, type, in_contact)
    downlink callback | packet store(s) | Session 8 command dispatcher
```

## Files

- `store.h` / `store.c` - bounded ring-buffer packet store (16 slots x 64 bytes), FIFO drain,
  overwrite-oldest on overflow, cumulative `dropped` counter, per-packet onboard-time stamp.
- `obdh.h` / `obdh.c` - the router, two stores (housekeeping and a higher-priority event store),
  contact handling (`obdh_set_contact()` drains events then HK before anything real-time), the
  hand-off of TCs to `uplink_handle_tc()` with its reports re-entering the router, and OBDH
  housekeeping (`obdh_build_hk()`: fill levels and drop counters).
- `obdh_test.c` - assert-based self checks and `main()` (the required Step 4 scenario, store unit
  checks, overflow and housekeeping, event priority and stamps, robustness).

Reuses `../03-tmtc` unchanged apart from making its `main()` weak so both directories link.

## Build and run

```
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c labs/04-obdh/*.c -o obdh && ./obdh
```

## Behaviour summary

- TC (any APID) -> command dispatcher; its TM[1,x] / TM[17,2] go back through the router to downlink.
- HK and OBDH HK TM -> downlink in contact, HK store out of contact.
- Event TM (APID 102) -> downlink in contact, event store out of contact.
- Unmapped APID -> dropped and counted; malformed TM -> dropped and counted separately.
- On regaining contact: event store drains, then HK store, each in order, before real-time TM.
- Overflow policy: overwrite oldest; every loss is counted and reported in OBDH housekeeping.

APIDs are lab-assigned (HK 100, command app 101, events 102, OBDH HK 103). Design notes,
the overflow rationale and the PR evidence are in `docs/07-obdh.md`.
