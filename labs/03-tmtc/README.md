# Lab 03 - TM/TC: HK as a CCSDS/PUS TM Packet

## Files

- `ccsds.h` / `ccsds.c` - CCSDS Space Packet primary header (6 octets, big endian): pack and
  parse, explicit shifts/masks (no struct bit fields, per Session 7's own warning).
- `pus.h` / `pus.c` - simplified PUS TM secondary header (service/subtype/counter) and the HK
  payload serializer/deserializer (big endian, explicit byte packing so the wire format does
  not depend on host endianness).
- `main.c` - builds one full TM packet per HK cycle (primary header + PUS Service 3 secondary
  header + HK payload), prints it as hex, and round-trip verifies both the primary header and
  the payload. Reuses `spacecraft_state` / `sample_state()` / `apply_limits()` from
  `labs/01-foundation/hk_telemetry.c` unchanged.

## Build and run

```sh
gcc -Wall -Wextra -std=c11 labs/03-tmtc/*.c -o tmtc
./tmtc
```

## Packet layout

```
[ CCSDS primary header : 6 B ][ PUS TM secondary header : 5 B ][ HK payload : 14 B ]
  version/type/APID/seq/len     pus_version/service/subtype/ctr  bus_v/batt/t_obc/t_batt/mode
```

- Primary header `data_length` = `(PUS secondary header + payload) - 1` = `(5 + 14) - 1` =
  **18**, per CCSDS's "octets minus one" convention - not the packet's total length.
- `APID = 0x064` is this lab's fixed housekeeping APID (`CCSDS_APID_HK` in `ccsds.h`).
- `service = 3` (Housekeeping), `subtype = 25` ("HK parameter report") - the subtype is the
  session's own worked example and is flagged in `pus.h` as **not yet checked against
  ECSS-E-ST-70-41 itself**; see `docs/05-tmtc.md`.
- Sequence count is a 14-bit field, incremented per packet with explicit `% 16384` wraparound,
  independently self-checked (`self_check_sequence_wrap()` in `main.c`) so the wrap is proven
  without waiting 16384 real cycles.

## Example packet, annotated (frame 0)

```
08 64 C0 00 00 12 | 20 03 19 00 00 | 41 01 99 9A 46 41 B0 00 00 41 90 00 00 01
-------primary------- --PUS hdr---   -------------------- payload --------------------
```

| Bytes | Field | Value |
|---|---|---|
| `08 64` | version(0)/type(0=TM)/sec_hdr_flag(1)/APID | APID = 0x064 |
| `C0 00` | seq_flags(3)/seq_count | seq_count = 0 |
| `00 12` | data_length | 18 (octets - 1) |
| `20` | pus_version | 0x20 |
| `03` | service | 3 (Housekeeping) |
| `19` | subtype | 25 (0x19) |
| `00 00` | counter | 0 |
| `41 01 99 9A` | bus_voltage (f32 BE) | 8.10 |
| `46` | battery_pct | 70 |
| `41 B0 00 00` | temp_obc (f32 BE) | 22.0 |
| `41 90 00 00` | temp_batt (f32 BE) | 18.0 |
| `01` | mode | 1 (MODE_NOMINAL) |
