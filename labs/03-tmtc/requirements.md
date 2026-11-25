# TM/TC requirements (Sessions 7-8)

These requirements were written down in Session 12, after the code and its self checks existed,
by reading what Sessions 7 and 8 built and what their checks assert. They are therefore a
description of intended behaviour that the tests already guard, not requirements that drove the
design. Writing them was the point of Session 12 Step 4: each one now names the test that protects
it, and `tools/check_traceability.py` fails CI if a requirement has no existing, executed test.

| ID | Requirement | Verified by |
|---|---|---|
| TMTC-REQ-001 | An HK telemetry packet shall carry a CCSDS primary header (version 0, type TM, secondary header present, APID HK, unsegmented) and a PUS-C TM secondary header; `data_length` shall be the data field octets minus one. | `self_check_primary_header` |
| TMTC-REQ-002 | The HK payload shall unpack to exactly the values that were packed. | `self_check_payload_roundtrip` |
| TMTC-REQ-003 | The telecommand CRC-16/CCITT-FALSE shall match the published check value, and a built TC shall parse back to the fields it was built from. | `check_crc_and_roundtrip` |
| TMTC-REQ-004 | The 14-bit TM sequence count shall increment per packet and wrap from 16383 to 0; a refused report build shall not consume a number. | `check_tm_sequence_wrap` |
| TMTC-REQ-005 | A valid TC[17,1] shall be executed and answered with TM[17,2]; the Service 1 acceptance, start and completion reports shall be sent exactly as the ack flags request. | `check_valid_command` |
| TMTC-REQ-006 | A command with an unknown service/subtype shall be refused with an acceptance-failure report and not executed. | `check_unknown_command` |
| TMTC-REQ-007 | A malformed packet (inconsistent length, truncated, too long, bad CRC, wrong APID, wrong type, wrong PUS version) shall be rejected before dispatch with the matching failure code; a packet too short to be identified shall be dropped and counted. | `check_malformed` |
| TMTC-REQ-008 | No truncated, extended or bit-corrupted variant of a valid command shall ever reach a handler, and the unmodified command shall still work afterwards. | `check_never_execute_unvalidated` |
