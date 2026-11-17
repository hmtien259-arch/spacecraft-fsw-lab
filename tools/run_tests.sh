#!/usr/bin/env bash
# tools/run_tests.sh - build every lab with warnings as errors, run its self checks, and fail
# (non-zero exit) if anything fails, warns, or silently did not run.
#
#   bash tools/run_tests.sh                 # gcc, all labs
#   CC=clang bash tools/run_tests.sh        # another compiler
#   SAN=1 bash tools/run_tests.sh           # + AddressSanitizer and UBSan
#   SKIP_RTOS=1 bash tools/run_tests.sh     # skip lab 02 (needs the FreeRTOS kernel download)
#   COVERAGE=1 bash tools/run_tests.sh      # builds with gcov instrumentation (see tools/coverage.sh)
#
# Why the "expect" markers: the labs chain their self checks through weak symbols, so a link
# change could make a whole group of checks silently not run and the binary would still exit 0.
# Every lab therefore names the lines its checks print, and the runner fails if it is absent.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC="${CC:-gcc}"
OUT="${OUT_DIR:-$ROOT/build/tests}"
STRICT="-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -std=c11"
EXTRA=""
[ "${SAN:-0}" = "1" ]      && EXTRA="$EXTRA -g -fsanitize=address,undefined -fno-sanitize-recover=all"
[ "${COVERAGE:-0}" = "1" ] && EXTRA="$EXTRA -O0 --coverage"
mkdir -p "$OUT"

passed=0
fail() { echo "FAIL: $*" >&2; exit 1; }

# run_lab <name> <expected output lines, separated by |> <sources...>
run_lab() {
    local name="$1" marker="$2"; shift 2
    local bin="$OUT/$name" log="$OUT/$name.log"
    echo "== $name"
    # shellcheck disable=SC2086
    $CC $STRICT $EXTRA "$@" -o "$bin" || fail "$name: build"
    "$bin" >"$log" 2>&1 || { tail -n 15 "$log" >&2; fail "$name: self checks returned non-zero"; }
    local m
    while IFS= read -r m; do
        grep -qF -- "$m" "$log" || { tail -n 15 "$log" >&2; fail "$name: expected '$m' in output (checks did not run?)"; }
        echo "   ok: $m"
    done <<< "${marker//|/$'\n'}"
    passed=$((passed + 1))
}

HK=labs/01-foundation
TMTC=(labs/03-tmtc/*.c)
OBDH=(labs/04-obdh/*.c)
FDIR=(labs/05-fdir/*.c)
MBD=(labs/06-mbd/*.c)

# Checks that are not compiled code (generated files and traceability must be current).
echo "== model-to-code consistency"
python3 -I labs/06-mbd/generate.py --check || fail "generated FDIR code does not match its model"
echo "== requirement traceability"
python3 -I tools/check_traceability.py || fail "requirement to test traceability"

# Each lab's build includes the earlier ones it depends on and re-runs their checks, so every
# group below is a superset of the previous one; the markers prove which groups really ran.
run_lab 01-foundation-hk   "HK frame=0009"                     "$HK/hk_telemetry.c"
run_lab 01-foundation-test "all tests passed"                  "$HK/test_hk.c"
run_lab 03-tmtc            "round-trip verified|all Session 8 uplink self checks passed" "${TMTC[@]}"
run_lab 04-obdh            "all Session 9 OBDH self checks passed"   "${TMTC[@]}" "${OBDH[@]}"
run_lab 05-fdir            "all Session 10 FDIR self checks passed"  "${TMTC[@]}" "${OBDH[@]}" "${FDIR[@]}"
run_lab 06-mbd             "all Session 11 model-based FDIR self checks passed" \
                           "${TMTC[@]}" "${OBDH[@]}" "${FDIR[@]}" "${MBD[@]}"

echo "ALL TESTS PASSED ($passed groups, CC=$CC${SAN:+, sanitizers}${COVERAGE:+, coverage})"
