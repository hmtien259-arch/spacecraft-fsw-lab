# Session 7 - Telemetry and Telecommand Notes

## Example packet, annotated

See `labs/03-tmtc/README.md` for a full byte-by-byte annotation of one captured HK TM packet
(frame 0, `08 64 C0 00 00 12 20 03 19 00 00 41 01 99 9A 46 41 B0 00 00 41 90 00 00 01`).

## Why the APID, not the payload, is what the ground uses to route a packet

The ground segment has to route a packet before it can interpret it, and interpreting the
payload requires already knowing which application produced it - the APID is the one field
guaranteed to be at a fixed offset (bits 5-15 of the first two octets) and a fixed meaning
across every mission that follows CCSDS, regardless of what that application's data looks
like. If routing depended on the payload instead, every ground station would need
mission-specific, payload-aware parsing just to decide where a packet should go, which is
exactly the kind of custom-code-per-vehicle problem CCSDS exists to avoid (Session 7 Section
2).

## Telecommand question carried into Session 8

Section 4 of Session 7 flags that PUS service/subtype numbers used here (Service 3 subtype
25) come from the session's worked example, not a verified read of ECSS-E-ST-70-41 - see the
warning in `labs/03-tmtc/pus.h`. Before building the Session 8 telecommand path (Service 17
connection test, Service 1 verification), the subtype values on the **command** side need the
same "verify against the standard" treatment, ideally before the uplink parser is written
rather than after - a wrong TC subtype fails closed (command rejected), which is safer than a
wrong TM subtype (a ground tool might silently misinterpret a report), but neither should ship
unverified.
