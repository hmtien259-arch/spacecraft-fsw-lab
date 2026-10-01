/* labs/03-tmtc/verify.h
 *
 * PUS Service 1 (request verification) and Service 17 (test) TM builders
 * for Session 8. Both reuse ccsds_pack_primary() / pus_pack_secondary()
 * from Session 7 for the TM envelope.
 *
 * Verification report layout (TM[1,x]):
 *   [ primary : 6 B ][ PUS TM sec hdr : 5 B ][ cmd packet id : 2 B ]
 *   [ cmd seq ctrl : 2 B ][ failure code : 1 B, failure reports only ]
 * cmd packet id / seq ctrl are the first two 16-bit words of the primary
 * header of the command being reported on, so the ground can match report
 * to command.
 *
 * Subtype note: the numbers below come from the session text
 * (ECSS-E-ST-70-41 Service 1: 1/2 acceptance, 3/4 start, 5/6 progress,
 * 7/8 completion; Service 17: TC[17,1] -> TM[17,2]). Treat as provisional
 * until confirmed against the standard; see docs/06-tc-verification.md.
 */
#ifndef VERIFY_H
#define VERIFY_H

#include <stddef.h>
#include <stdint.h>

#define PUS_SERVICE_VERIFICATION 1u
#define PUS_SERVICE_TEST         17u

#define PUS_VER_ACCEPT_OK     1u
#define PUS_VER_ACCEPT_FAIL   2u
#define PUS_VER_START_OK      3u
#define PUS_VER_START_FAIL    4u
#define PUS_VER_PROGRESS_OK   5u
#define PUS_VER_PROGRESS_FAIL 6u
#define PUS_VER_COMPLETE_OK   7u
#define PUS_VER_COMPLETE_FAIL 8u

#define PUS_TEST_CONNECTION_TEST         1u /* TC[17,1] perform connection test     */
#define PUS_TEST_CONNECTION_TEST_REPORT  2u /* TM[17,2] connection test report      */

/* Failure codes carried in TM[1,2]/[1,4]/[1,6]/[1,8]. These are project
 * defined (this lab's own table), not ECSS-assigned values. 0 = no failure. */
enum {
    VFAIL_NONE          = 0,
    VFAIL_ILLEGAL_LEN   = 1, /* too short / too long / data_length mismatch */
    VFAIL_BAD_CRC       = 2, /* packet error control check failed            */
    VFAIL_BAD_HEADER    = 3, /* not a standalone TC with a PUS-C header      */
    VFAIL_BAD_APID      = 4, /* wrong destination APID                       */
    VFAIL_UNKNOWN_CMD   = 5, /* (service, subtype) has no handler            */
    VFAIL_BAD_APP_DATA  = 6, /* known command, wrong application data        */
    VFAIL_EXEC_ERROR    = 7  /* handler ran and reported an error            */
};

#define VERIFY_REPORT_MAX_LEN 16u /* 6 + 5 + 4 + 1 */
#define TEST_REPORT_LEN       11u /* 6 + 5, no application data */

/* v_subtype: PUS_VER_* (1 accept ok, 2 accept fail, 3 start ok, 4 start fail,
 * 5 progress ok, 6 progress fail, 7 complete ok, 8 complete fail).
 * `out` needs room for VERIFY_REPORT_MAX_LEN bytes. *tm_seq is the running
 * 14-bit sequence count of `tm_apid`, used for this packet and then advanced
 * (wrapping at 16384). failure_code is written only for failure subtypes.
 * Returns the packet length, or 0 (nothing written, *tm_seq untouched) for
 * an invalid v_subtype or NULL argument. */
size_t build_verification(uint8_t *out, uint16_t tm_apid, uint16_t *tm_seq,
                          uint8_t v_subtype,
                          uint16_t cmd_pkt_id, uint16_t cmd_seq_ctrl,
                          uint8_t failure_code);

/* TM[17,2] connection test report (no application data). `out` needs room
 * for TEST_REPORT_LEN bytes. Same *tm_seq contract as above. */
size_t build_conn_test_report(uint8_t *out, uint16_t tm_apid, uint16_t *tm_seq);

#endif /* VERIFY_H */
