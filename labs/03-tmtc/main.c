/* labs/03-tmtc/main.c
 *
 * Session 7 practical task - HK as a CCSDS TM Packet.
 *
 * Wraps the Session 3/4 HK frame in a real CCSDS Space Packet with a PUS
 * Service 3 (Housekeeping) secondary header: same data, standards
 * compliant envelope, per Session 7 Section 5.
 *
 * Reuses spacecraft_state / sample_state() / apply_limits() from
 * labs/01-foundation/hk_telemetry.c unchanged (the same HK_UNIT_TEST
 * trick used in labs/01-foundation/test_hk.c and labs/02-rtos), so the
 * telemetry task's only real change across Sessions 3 -> 5 -> 7 is *how*
 * it emits a frame, never what it samples.
 */
#define HK_UNIT_TEST
#include "../01-foundation/hk_telemetry.c"

#include "ccsds.h"
#include "pus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TM_PACKET_LEN (CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN + PUS_HK_PAYLOAD_LEN)

static void print_hex(const char *label, const uint8_t *buf, size_t len) {
    printf("%s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) {
        printf("%02X%s", buf[i], (i + 1 < len) ? " " : "");
    }
    printf("\n");
}

/* Builds one full TM packet (primary header + PUS secondary header + HK
 * payload) into `buf`, which must have room for TM_PACKET_LEN bytes.
 * Returns the total packet length. */
static size_t build_hk_tm_packet(uint8_t *buf, uint16_t seq_count,
                                  const spacecraft_state *s) {
    uint8_t *p = buf;

    pus_tm_hdr pus_hdr = {
        .pus_version = PUS_TM_VERSION,
        .service     = PUS_SERVICE_HOUSEKEEPING,
        .subtype     = PUS_HK_SUBTYPE,
        .counter     = seq_count,
    };
    uint8_t payload[PUS_HK_PAYLOAD_LEN];
    pus_serialize_hk_payload(payload, s->bus_voltage, s->battery_pct,
                              s->temp_obc, s->temp_batt, (uint8_t) s->mode);

    uint16_t data_len = (uint16_t) ((PUS_TM_SEC_HDR_LEN + PUS_HK_PAYLOAD_LEN) - 1);
    /* data_length AFTER we know the data field size (PUS hdr + payload),
     * per Session 7 Step 2 - octets minus one, not the raw byte count. */
    ccsds_pri_hdr pri_hdr = {
        .version      = 0,
        .type         = 0, /* TM */
        .sec_hdr_flag = 1, /* PUS secondary header present */
        .apid         = CCSDS_APID_HK,
        .seq_flags    = 3, /* standalone, unsegmented packet */
        .seq_count    = seq_count,
        .data_length  = data_len,
    };

    p += ccsds_pack_primary(p, &pri_hdr);
    p += pus_pack_secondary(p, &pus_hdr);
    memcpy(p, payload, PUS_HK_PAYLOAD_LEN);
    p += PUS_HK_PAYLOAD_LEN;

    return (size_t) (p - buf);
}

/* Step 3 self-check: parse the primary header back out of a packet and
 * confirm every field matches what was packed - proves the packer and
 * parser agree, not just that the packer runs. */
static void self_check_primary_header(const uint8_t *packet, uint16_t expected_seq,
                                       uint16_t expected_data_len) {
    ccsds_pri_hdr parsed;
    ccsds_parse_primary(packet, &parsed);

    printf("  parsed: APID=0x%03X type=%s seq=%u data_length=%u\n",
           parsed.apid, parsed.type == 0 ? "TM" : "TC",
           parsed.seq_count, parsed.data_length);

    assert(parsed.version == 0);
    assert(parsed.type == 0);
    assert(parsed.sec_hdr_flag == 1);
    assert(parsed.apid == CCSDS_APID_HK);
    assert(parsed.seq_flags == 3);
    assert(parsed.seq_count == expected_seq);
    assert(parsed.data_length == expected_data_len);
}

/* Deliverable #4 self-check: sequence count increments per packet and
 * wraps at 16383 (i.e. the 14-bit counter rolls 16383 -> 0). Run
 * independently of the 10-cycle demo below, which never gets near the
 * wraparound on its own. */
static void self_check_sequence_wrap(void) {
    uint16_t seq = 16382;
    uint16_t expect[] = { 16382, 16383, 0, 1 };

    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i) {
        assert(seq == expect[i]);
        seq = (uint16_t) ((seq + 1) % 16384);
    }
    printf("PASS sequence count wraps 16383 -> 0\n");
}

int main(void) {
    self_check_sequence_wrap();
    printf("\n");

    spacecraft_state s = { .mode = MODE_BOOT };
    uint16_t seq_count = 0;

    for (uint32_t frame = 0; frame < HK_NUM_CYCLES; ++frame) {
        sample_state(&s);
        apply_limits(&s);

        uint8_t packet[TM_PACKET_LEN];
        size_t len = build_hk_tm_packet(packet, seq_count, &s);
        assert(len == TM_PACKET_LEN);

        printf("frame=%u mode=%s\n", frame, s.mode == MODE_SAFE ? "SAFE" : "NOMINAL");
        print_hex("  packet", packet, len);
        self_check_primary_header(packet, seq_count,
                                   (uint16_t) ((PUS_TM_SEC_HDR_LEN + PUS_HK_PAYLOAD_LEN) - 1));

        /* Round-trip the payload too, to prove pack/unpack agree end to end,
         * not just the primary header. */
        float rt_bus, rt_obc, rt_batt;
        uint8_t rt_pct, rt_mode;
        pus_deserialize_hk_payload(packet + CCSDS_PRI_HDR_LEN + PUS_TM_SEC_HDR_LEN,
                                    &rt_bus, &rt_pct, &rt_obc, &rt_batt, &rt_mode);
        assert(rt_bus == s.bus_voltage);
        assert(rt_pct == s.battery_pct);
        assert(rt_obc == s.temp_obc);
        assert(rt_batt == s.temp_batt);
        assert(rt_mode == (uint8_t) s.mode);

        seq_count = (uint16_t) ((seq_count + 1) % 16384); /* HK-REQ-004-style, for packets */
    }

    printf("\nall %d HK TM packets built, packed, and round-trip verified\n", HK_NUM_CYCLES);
    return 0;
}
