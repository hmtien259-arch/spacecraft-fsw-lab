#!/usr/bin/env bash
# tools/demo_gates.sh - proves each CI gate can actually go red.
#
# For every case it copies the repository to a scratch directory, injects one fault, runs the gate
# that is supposed to catch it, and reports RED (caught) or GREEN (missed). A gate that has never
# been seen failing is not evidence of anything. The last two cases show why the sanitizer job
# exists: the same memory error passes the plain build and is caught only under ASan.
#
#   bash tools/demo_gates.sh          # exit 0 only if every case except the marked "expected GREEN" is RED
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRATCH="$ROOT/build/demo"
rm -rf "$SCRATCH"; mkdir -p "$SCRATCH"
missed=0

# case <expect RED|GREEN> <label> <gate command> <file> <old text> <new text>
case_() {
    local expect="$1" label="$2" cmd="$3" file="$4" old="$5" new="$6"
    local w; w="$(mktemp -d -p "$SCRATCH")"
    (cd "$ROOT" && tar cf - --exclude=./build --exclude=./labs/02-rtos/FreeRTOS --exclude=./.git .) | tar xf - -C "$w"
    ( cd "$w" && python3 -I - "$file" "$old" "$new" <<'PY'
import sys
f, o, n = sys.argv[1:4]
s = open(f, encoding="utf-8").read()
assert o in s, "pattern missing: " + o
open(f, "w", encoding="utf-8").write(s.replace(o, n, 1))
PY
    ) || { echo "ERROR  $label: could not inject the fault"; missed=1; return; }
    local out rc
    out="$(cd "$w" && SKIP_RTOS=1 OUT_DIR="$w/build/t" bash -c "$cmd" 2>&1)"; rc=$?
    local why; why="$(grep -m1 -E 'FAIL|Assertion|error:|ERROR|MISMATCH|FAILED|Sanitizer|runtime error' <<<"$out" | cut -c1-110)"
    if [ $rc -ne 0 ]; then
        printf 'RED    %-62s %s\n' "$label" "$why"
        [ "$expect" = RED ] || missed=1
    else
        printf 'GREEN  %-62s (not caught)\n' "$label"
        [ "$expect" = GREEN ] || missed=1
    fi
    rm -rf "$w"
}

T="bash tools/run_tests.sh"
case_ RED   "invert the SAFE mode condition (HK-REQ-003)"            "$T" labs/01-foundation/hk_telemetry.c "s->bus_voltage < BUS" "s->bus_voltage > BUS"
case_ RED   "FDIR escalation off by one (FDIR-REQ-005)"              "$T" labs/05-fdir/fdir.c "c->attempts >= FDIR_MAX_ATTEMPTS" "c->attempts > FDIR_MAX_ATTEMPTS"
case_ RED   "compiler warning (unused variable) under -Werror"       "$T" labs/04-obdh/store.c "#include" $'static int unused_demo;\n#include'
case_ RED   "generated C edited by hand"                             "$T" labs/06-mbd/fdir_step_gen.c "FDIR_MAX_ATTEMPTS" "(FDIR_MAX_ATTEMPTS + 1)"
case_ RED   "model edited, generated files not regenerated"          "$T" labs/06-mbd/fdir_model.txt "T12, MONITORING, OK,              NOMINAL" "T12, MONITORING, OK,              SAFE   "
case_ RED   "requirement added with no test"                         "$T" labs/03-tmtc/requirements.md "| TMTC-REQ-008 | No truncated" $'| TMTC-REQ-009 | untested one | |\n| TMTC-REQ-008 | No truncated'
case_ RED   "test renamed, requirement still names the old one"      "$T" labs/03-tmtc/uplink_test.c "static void check_malformed(void)" "static void check_malformed_x(void)"
case_ RED   "test defined but no longer called"                      "$T" labs/03-tmtc/uplink_test.c $'    check_unknown_command();\n' ""
case_ RED   "FDIR checks silently not linked into the binary"        "$T" labs/04-obdh/obdh_test.c "        fdir_selfcheck();" "        (void) 0;"
case_ RED   "FDIR coverage drops (a test no longer called)"          "bash tools/coverage.sh" labs/05-fdir/fdir_test.c $'    check_defensive_paths();\n' $'    (void) check_defensive_paths;\n'
case_ RED   "static analysis finding (null dereference)"             "bash tools/static_analysis.sh" labs/03-tmtc/verify.c "#include" $'void null_demo(int *out) { int *p = 0; if (out) { *p = 1; } }\n#include'
MEM='void ccsds_parse_primary(const uint8_t *buf, ccsds_pri_hdr *h) {'
MEMX=$'void ccsds_parse_primary(const uint8_t *buf, ccsds_pri_hdr *h) {\n    volatile uint8_t sink_demo = ((volatile uint8_t *) h)[sizeof(*h)]; (void) sink_demo;'
case_ GREEN "memory error that changes no result: plain build"       "$T" labs/03-tmtc/ccsds.c "$MEM" "$MEMX"
case_ RED   "the same memory error: sanitizer build"                 "SAN=1 bash tools/run_tests.sh" labs/03-tmtc/ccsds.c "$MEM" "$MEMX"

if [ $missed -ne 0 ]; then echo "demo_gates: a gate did not behave as expected" >&2; exit 1; fi
echo "demo_gates: every gate went red on its fault (the plain build missing the memory error is the expected GREEN)"
