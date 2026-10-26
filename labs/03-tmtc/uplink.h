/* labs/03-tmtc/uplink.h
 *
 * The onboard command path for Session 8: validate -> acceptance report ->
 * dispatch by (service, subtype) -> execute -> completion report.
 *
 *   Ground TC -> tc_parse() -> TM[1,1]/[1,2] -> dispatch() -> handler
 *             -> TM[17,2] (Service 17) -> TM[1,7]/[1,8]
 *
 * Rules the implementation follows (Session 8 Section 5):
 *   - nothing is executed until the packet has passed every check;
 *   - an unknown or malformed command gets a TM[1,2] acceptance failure and
 *     is never partially executed;
 *   - success reports are emitted only for the stages the command's ack
 *     flags asked for; failure reports are ALWAYS emitted, because a
 *     rejected command must never fail silently (and on a corrupt packet
 *     the ack flags themselves cannot be trusted).
 */
#ifndef UPLINK_H
#define UPLINK_H

#include "tc.h"
#include "verify.h"

#include <stddef.h>
#include <stdint.h>

/* dispatch() results. */
enum {
    CMD_OK = 0,
    CMD_UNKNOWN,   /* no handler for this (service, subtype) */
    CMD_BAD_ARGS   /* handler exists but the application data is wrong */
};

/* uplink_handle_tc() results beyond TC_OK and the TC_ERR_* parse errors. */
enum {
    UPLINK_ERR_UNKNOWN_CMD = 100, /* valid packet, no such (service, subtype) */
    UPLINK_ERR_BAD_APP_DATA       /* known command, wrong application data    */
};

#define UPLINK_MAX_TM_PER_TC   4u  /* accept, start, one service report, complete */
#define UPLINK_MAX_TM_LEN      VERIFY_REPORT_MAX_LEN

/* Reports produced while handling one TC, in emission order. */
typedef struct {
    uint8_t pkt[UPLINK_MAX_TM_PER_TC][UPLINK_MAX_TM_LEN];
    size_t  len[UPLINK_MAX_TM_PER_TC];
    size_t  count;
} tm_out;

/* Mission-specific (user-defined range) service for FDIR control, Session 10.
 * Not an ECSS-E-ST-70-41 service: numbers 128-255 are left to the mission. */
#define CMD_SERVICE_FDIR_CTRL   130u
#define CMD_EXIT_SAFE_MODE      1u   /* TC[130,1], no application data */

/* Onboard command-handler state. Counters exist so a self check can prove
 * "rejected, and NOT executed" rather than assume it. */
typedef struct {
    uint16_t tm_seq;        /* running 14-bit sequence count for CCSDS_APID_CMD TM */
    uint32_t accepted;      /* commands that passed validation and dispatch  */
    uint32_t rejected;      /* commands rejected, with a TM[1,2] emitted     */
    uint32_t dropped;       /* too short to even identify: no report possible */
    uint32_t executed;      /* handler invocations - stays 0 for bad input    */
    uint32_t conn_tests;    /* TC[17,1] executions                            */
    /* Hook for TC[130,1] "exit safe mode". Called when the command executes;
     * returns non-zero if the exit happened, 0 if it was refused (for example
     * the vehicle is not in safe mode). NULL = not wired, command fails with
     * VFAIL_EXEC_ERROR. Set by labs/05-fdir (fdir_attach_uplink). */
    int            (*exit_safe_hook)(void *user);
    void            *hook_user;
} uplink_ctx;

/* Pure lookup + argument check on the (service, subtype) pair. No side
 * effects: this never executes anything. Returns CMD_OK / CMD_UNKNOWN /
 * CMD_BAD_ARGS. */
int dispatch(const pus_tc_hdr *tc, const uint8_t *app, size_t app_len);

void uplink_init(uplink_ctx *ctx);

/* Handles one received TC packet end to end and fills `out` with the TM
 * reports to downlink. Returns TC_OK if the command was accepted (whether or
 * not its execution then succeeded), otherwise the tc_parse() TC_ERR_* code
 * or UPLINK_ERR_*. Any non-TC_OK result means nothing was executed. */
int uplink_handle_tc(uplink_ctx *ctx, const uint8_t *buf, size_t len, tm_out *out);

/* Session 8 lab self checks (uplink_test.c): valid / unknown / malformed
 * commands plus ack-flag, CRC and robustness cases. Asserts on failure. */
void uplink_selfcheck(void);

#endif /* UPLINK_H */
