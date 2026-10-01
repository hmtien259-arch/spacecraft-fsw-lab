/* labs/03-tmtc/ccsds.c - CCSDS Space Packet primary header pack/parse. */
#include "ccsds.h"

size_t ccsds_pack_primary(uint8_t *buf, const ccsds_pri_hdr *h) {
    uint16_t w0 = (uint16_t) ( ( ( h->version      & 0x7u )  << 13 )
                              | ( ( h->type         & 0x1u )  << 12 )
                              | ( ( h->sec_hdr_flag & 0x1u )  << 11 )
                              |   ( h->apid         & 0x7FFu ) );
    uint16_t w1 = (uint16_t) ( ( ( h->seq_flags & 0x3u )    << 14 )
                              |   ( h->seq_count & 0x3FFFu ) );

    buf[0] = (uint8_t) ( w0 >> 8 );
    buf[1] = (uint8_t) ( w0 & 0xFFu );
    buf[2] = (uint8_t) ( w1 >> 8 );
    buf[3] = (uint8_t) ( w1 & 0xFFu );
    buf[4] = (uint8_t) ( h->data_length >> 8 );
    buf[5] = (uint8_t) ( h->data_length & 0xFFu );

    return CCSDS_PRI_HDR_LEN;
}

void ccsds_parse_primary(const uint8_t *buf, ccsds_pri_hdr *h) {
    uint16_t w0 = (uint16_t) ( ( (uint16_t) buf[0] << 8 ) | buf[1] );
    uint16_t w1 = (uint16_t) ( ( (uint16_t) buf[2] << 8 ) | buf[3] );

    h->version      = (uint8_t)  ( ( w0 >> 13 ) & 0x7u );
    h->type         = (uint8_t)  ( ( w0 >> 12 ) & 0x1u );
    h->sec_hdr_flag = (uint8_t)  ( ( w0 >> 11 ) & 0x1u );
    h->apid         = (uint16_t) (   w0         & 0x7FFu );
    h->seq_flags    = (uint8_t)  ( ( w1 >> 14 ) & 0x3u );
    h->seq_count    = (uint16_t) (   w1         & 0x3FFFu );
    h->data_length  = (uint16_t) ( ( (uint16_t) buf[4] << 8 ) | buf[5] );
}
