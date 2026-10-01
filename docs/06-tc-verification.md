# Session 8 - Telecommand Path and Command Verification Notes

## Why validate an uplinked command before executing it

The uplink is the only place where input from outside the spacecraft is turned directly into
action, and a command acts on the vehicle at once: a corrupted or malformed packet that is
executed anyway can switch the wrong unit, load a wrong parameter, or leave a half-finished
action behind, and on orbit there is no one to reboot it. Validation (length, CRC, header,
known command, correct arguments) is therefore a safety requirement: it bounds the damage a
bit error, a ground mistake or a hostile packet can do to "the command is rejected and
reported", and the Service 1 failure report is what tells the operator it was rejected instead
of leaving a silent gap. `labs/03-tmtc/uplink_test.c` proves this by sweeping every truncation
and every single-bit flip of a valid command and asserting that none of them reaches a handler.

## Evidence for the PR: accepted and rejected commands

All values hex. TC APID = 0x065 (`CCSDS_APID_CMD`), TM sent from the same APID.

**Accepted - TC[17,1], ack flags = accept | complete (0x9), sequence count 5**

```
TC     : 18 65 C0 05 00 06 | 29 11 01 00 01 | 65 25
         primary (TC)        PUS TC hdr        CRC-16
TM[1,1]: 08 65 C0 00 00 08 | 20 01 01 00 00 | 18 65 C0 05
TM[17,2]: 08 65 C0 01 00 04 | 20 11 02 00 01
TM[1,7]: 08 65 C0 02 00 08 | 20 01 07 00 02 | 18 65 C0 05
```

- `29` = PUS version 2 | ack flags 0x9; `11` = service 17; `01` = subtype 1.
- The verification reports end with `18 65 C0 05`: the packet id and sequence control of the
  command they refer to (TC packet id `18 65`, sequence `C0 05` = count 5), which is how the
  ground matches report to command.
- Order on the wire: acceptance success, the TM[17,2] test report, completion success.

**Rejected - unknown TC[17,99], sequence count 9**

```
TC     : 18 65 C0 09 00 06 | 29 11 63 00 01 | 81 99
TM[1,2]: 08 65 C0 00 00 09 | 20 01 02 00 00 | 18 65 C0 09 05
```

- `63` = subtype 99, no handler exists. The report ends with the command's identity
  (`18 65 C0 09`) and failure code `05` (`VFAIL_UNKNOWN_CMD`). Nothing was executed
  (`executed == 0` in the self check) and no TM[17,2] was produced.

**Rejected - length-inconsistent packet (data_length says 7, packet carries 6)**

```
TC     : 18 65 C0 14 00 07 | 29 11 01 00 01 | 5A E2   (CRC recomputed to match)
TM[1,2]: 08 65 C0 00 00 09 | 20 01 02 00 00 | 18 65 C0 14 01
```

- The CRC is deliberately valid so that only the length-consistency check can catch it; the
  failure code is `01` (`VFAIL_ILLEGAL_LEN`).

## Design decisions worth reviewing

- **Check order:** size sanity -> length consistency -> CRC -> primary-header fields -> PUS
  header -> known `(service, subtype)` -> argument length. Only the length is read before the CRC
  (it is needed to find the CRC); no other field is believed until the CRC passes.
- **Failure reports are never optional.** Success reports follow the ack flags (stretch goal),
  but a rejection always yields TM[1,2], because a rejected command must not be silent and, on a
  corrupt packet, the ack flags themselves cannot be trusted. A packet too short to carry a
  packet id and sequence control (< 4 bytes) cannot be named in a report and is only counted
  (`dropped`).
- **The failure-code table is this lab's own** (`verify.h`, `VFAIL_*`); it is not an
  ECSS-assigned list.
- **The CRC** is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), checked against the published
  value 0x29B1 for the string "123456789".
- **Progress stage:** TC[17,1] has no intermediate steps, so a progress ack flag on it
  correctly produces no TM[1,5]. The start stage (TM[1,3]) is emitted when requested.

## Subtype numbers still to be verified against the standard

As in Session 7 (see `docs/05-tmtc.md`), the service and subtype numbers used here - Service 1
subtypes 1-8, TC[17,1], TM[17,2] - come from the session text and have **not** been read from
ECSS-E-ST-70-41 itself in this repo. Public implementations of the standard use TC[17,1] /
TM[17,2] for the connection test, which agrees with the session, but the standard is the
authority. Two further points to settle there before real use: Service 1 was reworked in
ECSS-E-ST-70-41C Rev.1 (the verification approach and which reports exist change, per ECSS's
published change summary), and the exact secondary-header field widths (source id width,
ack-flag nibble position) are simplified in this lab.

## Question carried into Session 9 (OBDH)

When the OBDH layer routes packets by APID and stores telemetry for the next ground pass,
should Service 1 verification reports go through the same store-and-forward queue as housekeeping
data - risking that an acceptance failure for a time-critical command reaches the ground only
at the next pass behind a backlog of HK - or on a priority path that bypasses the store?
