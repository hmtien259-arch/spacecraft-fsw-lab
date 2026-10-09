/* labs/04-obdh/store.c - ring-buffer packet store with overwrite-oldest. */
#include "store.h"

#include <string.h>

void store_init(packet_store *s) {
    memset(s, 0, sizeof(*s));
}

int store_put_at(packet_store *s, const uint8_t *pkt, size_t n, uint32_t stamp) {
    if (s == NULL || pkt == NULL || n == 0) { return STORE_REJECTED; }
    if (n > MAX_PKT) {
        s->dropped++; /* a real packet is being lost: make it visible */
        return STORE_REJECTED;
    }

    int result = STORE_OK;
    if (s->count == STORE_CAP) {
        /* Full: discard the oldest to make room. head == tail here, so the
         * slot about to be written is the oldest one. */
        s->tail = (s->tail + 1u) % STORE_CAP;
        s->count--;
        s->dropped++;
        result = STORE_OVERWROTE;
    }
    memcpy(s->slot[s->head], pkt, n);
    s->len[s->head]   = n;
    s->stamp[s->head] = stamp;
    s->head = (s->head + 1u) % STORE_CAP;
    s->count++;
    return result;
}

int store_put(packet_store *s, const uint8_t *pkt, size_t n) {
    return store_put_at(s, pkt, n, 0u);
}

int store_get_at(packet_store *s, uint8_t *out, size_t *n, uint32_t *stamp) {
    if (s == NULL || out == NULL || n == NULL || s->count == 0) { return 0; }
    memcpy(out, s->slot[s->tail], s->len[s->tail]);
    *n = s->len[s->tail];
    if (stamp != NULL) { *stamp = s->stamp[s->tail]; }
    s->tail = (s->tail + 1u) % STORE_CAP;
    s->count--;
    return 1;
}

int store_get(packet_store *s, uint8_t *out, size_t *n) {
    return store_get_at(s, out, n, NULL);
}
