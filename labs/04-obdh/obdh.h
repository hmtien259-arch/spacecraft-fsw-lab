/* labs/04-obdh/obdh.h
 *
 * Minimal OBDH core, Session 9: one router through which every packet
 * passes, a store-and-forward path for telemetry while out of ground
 * contact, and the Session 8 command dispatcher behind it.
 *
 *   HK TM (S7)   Event TM   Telecommand (S8)
 *            \       |       /
 *              obdh_ingest()  -> obdh_route(apid, type, in_contact)
 *                |        |             |
 *           downlink   store(s)   uplink_handle_tc()  (its TM[1,x]/TM[17,2]
 *                                                       re-enter the router)
 *
 * Routing (type 1 = TC, type 0 = TM):
 *   any APID, TC          -> DEST_DISPATCH
 *   APID_HK / APID_OBDH_HK-> in contact ? DOWNLINK : STORE   (HK store)
 *   APID_EVENT            -> in contact ? DOWNLINK : STORE   (event store)
 *   CCSDS_APID_CMD TM     -> DOWNLINK  (command responses: the ground just
 *                            spoke to us, so it is in contact by definition)
 *   anything else         -> DROP (counted, never a crash)
 *
 * Deviation from the Session 9 router sketch: events are sent in real time
 * only while in contact; out of contact they are STORED in a second,
 * higher-priority store (stretch goal) instead of being sent into the void.
 */
#ifndef OBDH_H
#define OBDH_H

#include "store.h"
#include "../03-tmtc/ccsds.h"
#include "../03-tmtc/pus.h"
#include "../03-tmtc/uplink.h"

#include <stddef.h>
#include <stdint.h>

/* Lab-assigned APIDs, consistent with ccsds.h (HK = 0x064, command app = 0x065).
 * The Session 9 table lists the event APID as "101" as an example, but 101
 * (0x065) is already CCSDS_APID_CMD from Session 8 (TC address and the source
 * of every verification report), so events use 102 here. */
#define APID_HK       CCSDS_APID_HK /* 0x064 = 100 */
#define APID_EVENT    0x066u        /* 102 */
#define APID_OBDH_HK  0x067u        /* 103: the OBDH's own housekeeping (store levels, drops) */

typedef enum { DEST_DOWNLINK, DEST_STORE, DEST_DISPATCH, DEST_DROP } obdh_dest;

/* Pure routing decision; no side effects. */
obdh_dest obdh_route(uint16_t apid, uint8_t pkt_type, int in_contact);

/* Where "downlink" goes: a callback, so the lab can record what the ground
 * would receive. Called once per packet, in the order the ground sees them. */
typedef void (*obdh_downlink_fn)(void *user, const uint8_t *pkt, size_t len);

typedef struct {
    packet_store     hk_store;       /* housekeeping, store-and-forward         */
    packet_store     ev_store;       /* events: higher priority, drains first   */
    uplink_ctx       uplink;         /* Session 8 command path                  */
    obdh_downlink_fn downlink;
    void            *downlink_user;
    int              in_contact;
    uint32_t         onboard_time;   /* the one authoritative clock, in ticks   */
    uint16_t         hk_seq;         /* sequence count of OBDH HK packets       */
    uint32_t         n_downlinked;   /* packets handed to the downlink          */
    uint32_t         n_stored;       /* packets routed into a store             */
    uint32_t         n_dispatched;   /* TCs handed to the command dispatcher    */
    uint32_t         n_drop_unknown; /* well-formed TM with an unmapped APID    */
    uint32_t         n_drop_malformed; /* unreadable / length-inconsistent      */
} obdh_ctx;

void obdh_init(obdh_ctx *c, obdh_downlink_fn downlink, void *user, int in_contact);

/* Advances the onboard clock by one tick. Packets are stamped with it where
 * they are stored. */
void obdh_tick(obdh_ctx *c);

/* Sets ground-contact state. On a transition into contact the stores are
 * drained to the downlink - events first, then housekeeping, each in FIFO
 * order - BEFORE any further packet is routed, so stored telemetry always
 * precedes real-time telemetry. */
void obdh_set_contact(obdh_ctx *c, int in_contact);

/* The single entry point: routes one packet and acts on the decision.
 * Returns the destination chosen (DEST_DROP also for malformed input).
 * TCs are not length-checked here: the Session 8 path validates them and
 * answers with TM[1,2], which must not be pre-empted by a silent drop.
 * Non-TC packets must be internally consistent (data_length vs bytes). */
obdh_dest obdh_ingest(obdh_ctx *c, const uint8_t *pkt, size_t len);

/* Packs a TM packet: primary header (APID, seq) + PUS TM secondary header
 * (service, subtype, counter = seq) + payload. Returns its length, or 0 if
 * it does not fit in `cap`. Used for OBDH housekeeping and by the self checks. */
size_t obdh_pack_tm(uint8_t *out, size_t cap, uint16_t apid, uint16_t seq,
                    uint8_t service, uint8_t subtype,
                    const uint8_t *payload, size_t payload_len);

/* OBDH housekeeping (stretch goal): store fill levels and loss counters,
 * as a Service 3 HK report on APID_OBDH_HK. Payload, big endian:
 *   hk_fill u8 | ev_fill u8 | hk_dropped u32 | ev_dropped u32 | unknown_apid_dropped u32
 * Returns the packet length (0 if `cap` is too small); the caller routes it
 * with obdh_ingest() like any other packet. */
#define OBDH_HK_PAYLOAD_LEN 14u
size_t obdh_build_hk(obdh_ctx *c, uint8_t *out, size_t cap);


#endif /* OBDH_H */
