/* labs/04-obdh/store.h
 *
 * Bounded packet store (ring buffer) for store-and-forward, Session 9 Step 2.
 *
 * Overflow policy: OVERWRITE OLDEST. When the store is full the oldest
 * packet is discarded to make room for the new one, and `dropped` is
 * incremented so the loss is visible (it is reported in OBDH housekeeping,
 * see obdh.h). Rationale is in docs/07-obdh.md.
 *
 * Each stored packet also carries the onboard time at which it was stored
 * (stretch goal): drain order is FIFO, and the stamps are strictly
 * non-decreasing in drain order.
 */
#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>

#define STORE_CAP 16u /* packets per store */
#define MAX_PKT   64u /* largest packet a slot can hold, in bytes */

typedef struct {
    uint8_t  slot[STORE_CAP][MAX_PKT];
    size_t   len[STORE_CAP];
    uint32_t stamp[STORE_CAP]; /* onboard time at which the packet was stored */
    unsigned head;             /* next slot to write */
    unsigned tail;             /* oldest stored packet (next to drain) */
    unsigned count;            /* packets currently stored, 0..STORE_CAP */
    uint32_t dropped;          /* packets lost: overwritten when full, or too big to store */
} packet_store;

/* store_put() results. */
enum {
    STORE_REJECTED  = -1, /* nothing stored (NULL, empty, or longer than MAX_PKT) */
    STORE_OK        = 0,  /* stored, nothing lost                                  */
    STORE_OVERWROTE = 1   /* stored, but the oldest packet was discarded for it    */
};

void store_init(packet_store *s);

/* Stores a copy of pkt[0..n). When full, overwrites the oldest packet and
 * counts it in `dropped`. A packet longer than MAX_PKT cannot be stored at
 * all: it is rejected and also counted in `dropped` (a real packet was lost).
 * n == 0 or a NULL pointer is rejected without counting (there was no packet). */
int store_put(packet_store *s, const uint8_t *pkt, size_t n);

/* As store_put(), recording `stamp` (onboard time) with the packet. */
int store_put_at(packet_store *s, const uint8_t *pkt, size_t n, uint32_t stamp);

/* FIFO drain: copies the oldest packet into `out` (room for MAX_PKT bytes),
 * sets *n to its length, removes it, and returns 1. Returns 0 when empty
 * (outputs untouched). */
int store_get(packet_store *s, uint8_t *out, size_t *n);

/* As store_get(), also returning the packet's storage stamp (may be NULL). */
int store_get_at(packet_store *s, uint8_t *out, size_t *n, uint32_t *stamp);

#endif /* STORE_H */
