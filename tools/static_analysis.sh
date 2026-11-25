#!/usr/bin/env bash
# tools/static_analysis.sh - clang-tidy over every C file of labs 01 and 03-06; any finding fails.
#
# Product sources get every check; test sources skip three that do not apply (see TEST below).
# The repository is clean today, so "new findings fail" is simply "any finding fails". Checks:
# the clang static analyzer, bugprone-* and cert-*, minus
#   - bugprone-easily-swappable-parameters: style noise for the many (uint8_t, uint8_t) APIs
#   - clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling: wants the optional
#     C11 Annex K memcpy_s / memset_s, which gcc and clang on Linux do not provide
# Lab 02 is not analysed here: its translation units pull in the FreeRTOS kernel, which is
# third-party code (it is built with -Wall -Wextra and run by tools/run_tests.sh).
#
#   bash tools/static_analysis.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TIDY="${CLANG_TIDY:-clang-tidy}"
command -v "$TIDY" >/dev/null || { echo "FAIL: $TIDY not found" >&2; exit 1; }
BASE='-*,clang-analyzer-*,bugprone-*,cert-*,-bugprone-easily-swappable-parameters,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling'
# Test sources: the same checks minus a few that do not apply to host-side test code.
#   cert-err33-c            fprintf/snprintf/fclose return values in printing helpers
#   bugprone-unsafe-functions, cert-msc24-c, cert-msc33-c   rewind() on a tmpfile in a test
#   bugprone-suspicious-include  `#include "x.c"` is the deliberate HK_UNIT_TEST pattern
TEST="$BASE,-cert-err33-c,-bugprone-unsafe-functions,-cert-msc24-c,-cert-msc33-c,-bugprone-suspicious-include"

is_test() { case "$(basename "$1")" in *_test.c|test_*.c|main.c) return 0;; esac; return 1; }

files=(labs/01-foundation/*.c labs/03-tmtc/*.c labs/04-obdh/*.c labs/05-fdir/*.c labs/06-mbd/*.c)
echo "$("$TIDY" --version | grep -o 'version [0-9.]*' | head -n 1) ($TIDY): ${#files[@]} files"
status=0
for f in "${files[@]}"; do
    checks="$BASE"; is_test "$f" && checks="$TEST"
    out="$("$TIDY" -quiet -warnings-as-errors='*' -checks="$checks" "$f" -- -std=c11 -Ilabs/03-tmtc -Ilabs/04-obdh -Ilabs/05-fdir -Ilabs/06-mbd 2>&1 || true)"
    if grep -qE "(warning|error):" <<<"$out"; then
        grep -E "(warning|error):" <<<"$out" | sed "s|^$ROOT/||" >&2
        status=1
    fi
done
if [ $status -ne 0 ]; then echo "FAIL: static analysis findings (see above)" >&2; exit 1; fi
echo "static analysis clean"
