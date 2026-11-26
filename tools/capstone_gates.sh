#!/usr/bin/env bash
# tools/capstone_gates.sh - the capstone CI gates (Session 13 section 9), runnable locally.
#
#   bash tools/capstone_gates.sh test        # build with -Werror, fault-injection isolation, all tests
#   bash tools/capstone_gates.sh coverage    # gcov: overall line >= 80 %, FDIR branch >= 90 %
#   bash tools/capstone_gates.sh cppcheck    # static analysis, any finding fails
#   bash tools/capstone_gates.sh report      # the report (outline at the baseline) is present and complete
#   bash tools/capstone_gates.sh all         # all four
#   CC=clang / SAN=1 select the compiler and sanitizers, as for tools/run_tests.sh, which runs `test`.
#
# Build conventions frozen with the gates (Session 14 builds to them):
#   capstone/src/*.c           product code. src/main.c holds the application's main() and is the
#                              only file left out of test links. Every product file must compile
#                              with -Werror both WITHOUT and WITH -DFSW_TEST (nominal and test build).
#   capstone/tests/test_*.c    one test program each, built with -DFSW_TEST and linked with every
#                              product object plus the support files (capstone/tests/*.c that are not
#                              test_*.c: the injector, the scenario harness). It must exit 0 and print
#                              a line containing "passed", so a program that silently ran nothing fails.
#   capstone/tests/test_fault_inject.c   the SW-FI-01 isolation check, built its own way (below).
# The FreeRTOS application link (src/main.c + the kernel) is added to `test` in Session 14 with
# the application itself.
#
# Gates armed before the code they measure exists: while capstone/src has no .c file, `coverage`
# reports NOT ARMED and passes, so main stays green during the build sessions. That is printed on
# every run, and the acceptance run (Session 13 section 7) uses REQUIRE_ARMED=1, which turns
# NOT ARMED into a failure.
set -euo pipefail
shopt -s nullglob
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC="${CC:-gcc}"
OUT="${OUT_DIR:-$ROOT/build/capstone}"
STRICT="-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -std=c11"
INC="-Icapstone/src -Icapstone/tests"
EXTRA=""
[ "${SAN:-0}" = "1" ] && EXTRA="$EXTRA -g -fsanitize=address,undefined -fno-sanitize-recover=all"
MIN_LINES="${MIN_LINES_CAPSTONE:-80}"
MIN_BRANCHES_FDIR="${MIN_BRANCHES_FDIR_CAPSTONE:-90}"

fail() { echo "FAIL: capstone: $*" >&2; exit 1; }

product_srcs() { local f; for f in capstone/src/*.c; do [ "$(basename "$f")" = main.c ] || echo "$f"; done; }
support_srcs() { local f; for f in capstone/tests/*.c; do case "$(basename "$f")" in test_*) ;; *) echo "$f";; esac; done; }
test_progs()   { local f; for f in capstone/tests/test_*.c; do [ "$(basename "$f")" = test_fault_inject.c ] || echo "$f"; done; }

# compile_objs <dir> <extra flags>: every product and support file to <dir>/*.o
compile_objs() {
    local dir="$1" flags="$2" f; mkdir -p "$dir"
    for f in $(product_srcs) $(support_srcs); do
        # shellcheck disable=SC2086
        $CC $STRICT $EXTRA $flags $INC -c "$f" -o "$dir/$(basename "${f%.c}").o" || fail "build $f ($flags)"
    done
}

gate_test() {
    rm -rf "$OUT/test"; mkdir -p "$OUT/test"
    echo "== capstone: fault injection isolation (SW-FI-01)"
    local t=capstone/tests/test_fault_inject.c
    # shellcheck disable=SC2086
    $CC $STRICT $EXTRA $INC "$t" -o "$OUT/test/fi_nominal" || fail "isolation check, nominal build"
    "$OUT/test/fi_nominal" | grep -q "passed (nominal build)" || fail "isolation check, nominal run"
    echo "   ok: nominal build: API absent, hook is the constant 0"
    # shellcheck disable=SC2086
    $CC $STRICT $EXTRA $INC -DFSW_TEST -c "$t" -o "$OUT/test/fi_test.o" || fail "isolation check, FSW_TEST build"
    echo "   ok: FSW_TEST build: API declared with the agreed signatures"
    printf '#include "fault_inject.h"\nvoid misuse(void);\nvoid misuse(void) { fault_inject(FI_LOW_VOLTAGE, 5500); }\n' > "$OUT/test/misuse.c"
    # shellcheck disable=SC2086
    if $CC $STRICT $INC -c "$OUT/test/misuse.c" -o "$OUT/test/misuse.o" 2>"$OUT/test/misuse.log"; then
        fail "a nominal unit calling fault_inject() compiled: the injector leaks into nominal builds"
    fi
    # shellcheck disable=SC2086
    $CC $STRICT $INC -DFSW_TEST -c "$OUT/test/misuse.c" -o "$OUT/test/misuse.o" \
        || fail "the same unit with -DFSW_TEST did not compile (the negative check proves nothing)"
    echo "   ok: calling fault_inject() fails to compile without -DFSW_TEST, compiles with it"

    echo "== capstone: product code, nominal and test builds"
    local n; n="$(product_srcs | wc -l)"
    compile_objs "$OUT/test/nominal" ""
    compile_objs "$OUT/test/fswtest" "-DFSW_TEST"
    [ -f capstone/src/main.c ] && {
        # shellcheck disable=SC2086
        $CC $STRICT $EXTRA $INC -c capstone/src/main.c -o "$OUT/test/main.o" || fail "build capstone/src/main.c"; }
    echo "   ok: $n product file(s) build with -Werror, with and without FSW_TEST"

    echo "== capstone: test programs"
    local p bin ran=0
    for p in $(test_progs); do
        bin="$OUT/test/$(basename "${p%.c}")"
        # shellcheck disable=SC2086
        $CC $STRICT $EXTRA $INC -DFSW_TEST "$p" "$OUT"/test/fswtest/*.o -o "$bin" ${LDLIBS:-} || fail "build $p"
        "$bin" >"$bin.log" 2>&1 || { tail -n 15 "$bin.log" >&2; fail "$p returned non-zero"; }
        grep -q "passed" "$bin.log" || { tail -n 15 "$bin.log" >&2; fail "$p printed no 'passed' line (did its checks run?)"; }
        echo "   ok: $p"; ran=$((ran + 1))
    done
    echo "capstone tests passed ($ran test program(s) + the isolation check, CC=$CC${SAN:+, sanitizers})"
}

gate_coverage() {
    local cov="$OUT/coverage"; rm -rf "$cov"; mkdir -p "$cov"
    if [ -z "$(product_srcs)" ]; then
        echo "capstone coverage: NOT ARMED - capstone/src has no product code yet (floors: lines >= ${MIN_LINES} %, FDIR branches >= ${MIN_BRANCHES_FDIR} %)"
        [ "${REQUIRE_ARMED:-0}" = "1" ] && fail "coverage gate not armed, but REQUIRE_ARMED=1 (acceptance run)"
        return 0
    fi
    [ "$CC" = gcc ] || fail "coverage needs gcc (gcov); CC=$CC"
    EXTRA="$EXTRA -O0 --coverage" compile_objs "$cov/obj" "-DFSW_TEST"
    local p bin
    for p in $(test_progs); do
        bin="$cov/$(basename "${p%.c}")"
        # shellcheck disable=SC2086
        $CC $STRICT -O0 --coverage $INC -DFSW_TEST "$p" "$cov"/obj/*.o -o "$bin" ${LDLIBS:-} || fail "coverage build $p"
        "$bin" >"$bin.log" 2>&1 || fail "$p failed under coverage"
    done
    local f
    for f in $(product_srcs); do
        # run from the root so gcov can open the source (relative path in the .gcno); a file no
        # test linked has no .gcda and counts as fully missed below
        if gcov -b -c -o "$cov/obj" "$f" >/dev/null 2>&1 && [ -f "$(basename "$f").gcov" ]; then
            mv "$(basename "$f").gcov" "$cov/"
        fi
    done
    python3 -I - "$cov" "$MIN_LINES" "$MIN_BRANCHES_FDIR" $(product_srcs) <<'PY'
import os, re, sys
cov, min_l, min_b, srcs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4:]
tot = hit = 0; fb = ft = 0; rows = []
for s in srcs:
    g = os.path.join(cov, os.path.basename(s) + ".gcov")
    l_t = l_h = b_t = b_h = 0
    if os.path.exists(g):
        for line in open(g, encoding="utf-8", errors="replace"):
            m = re.match(r"\s*([^:]+):\s*(\d+):", line)
            if m and m.group(1).strip() != "-" and m.group(2) != "0":
                l_t += 1; l_h += 0 if m.group(1).strip().startswith(("#", "=")) else 1
            b = re.match(r"branch\s+\d+ (taken (\d+)|never executed)", line)
            if b:
                b_t += 1; b_h += 1 if b.group(2) and int(b.group(2)) > 0 else 0
    else:  # never linked into any test: every line counts as missed
        l_t = sum(1 for _ in open(s, encoding="utf-8")); l_h = 0
    tot += l_t; hit += l_h
    if "fdir" in os.path.basename(s): fb += b_t; ft += b_h
    rows.append("| `%s` | %d/%d | %d/%d |" % (s, l_h, l_t, b_h, b_t))
lp = 100.0 * hit / tot if tot else 0.0
bp = 100.0 * ft / fb if fb else 0.0
with open(os.path.join(cov, "summary.md"), "w", encoding="utf-8") as f:
    f.write("# Capstone coverage\n\n| File | Lines | Branches taken |\n|---|---|---|\n" + "\n".join(rows) + "\n")
    f.write("\nOverall lines %.1f%% (floor %.0f%%); FDIR branches %.1f%% (floor %.0f%%)\n" % (lp, min_l, bp, min_b))
print("\n".join(rows)); print("overall lines %.1f%%, FDIR branches %.1f%%" % (lp, bp))
bad = []
if lp < min_l: bad.append("overall line coverage %.1f%% < %.0f%%" % (lp, min_l))
if fb == 0: bad.append("no FDIR branches measured (no capstone/src/*fdir*.c, or not linked into a test)")
elif bp < min_b: bad.append("FDIR branch coverage %.1f%% < %.0f%%" % (bp, min_b))
if bad:
    print("CAPSTONE COVERAGE GATE FAILED: " + "; ".join(bad), file=sys.stderr); sys.exit(1)
PY
}

gate_cppcheck() {
    command -v cppcheck >/dev/null || fail "cppcheck not found"
    echo "== capstone: cppcheck $(cppcheck --version | grep -o '[0-9.]*$'), nominal and FSW_TEST configurations"
    # The tree is clean, so any finding is new: --error-exitcode makes every finding fail. A
    # justified exception is an inline `// cppcheck-suppress <id>` with the reason on the same line.
    local cfg
    for cfg in "" "-DFSW_TEST"; do
        # shellcheck disable=SC2086
        cppcheck --enable=warning,style,performance,portability --std=c11 --inline-suppr \
            --error-exitcode=1 --quiet $cfg -Icapstone/src -Icapstone/tests \
            --suppress=missingIncludeSystem capstone/src capstone/tests \
            || fail "cppcheck findings (${cfg:-nominal})"
    done
    echo "cppcheck clean"
}

gate_report() {
    echo "== capstone: report"
    local o=capstone/report/outline.md s
    [ -f "$o" ] || fail "$o missing"
    for s in "Introduction" "System overview" "Requirements" "Design" "Verification and validation" \
             "Scenario results" "Limitations" "Conclusion" "Appendices"; do
        grep -qiE "^#+ .*$s" "$o" || fail "$o: no section heading containing '$s'"
    done
    # Once the report itself exists it must at least not be empty; building it (if a toolchain is
    # chosen) is added to this gate when the report is.
    s=capstone/report/report.md
    [ ! -e "$s" ] || [ -s "$s" ] || fail "$s is empty"
    echo "   ok: report outline present with all sections"
}

case "${1:-all}" in
    test)     gate_test ;;
    coverage) gate_coverage ;;
    cppcheck) gate_cppcheck ;;
    report)   gate_report ;;
    all)      gate_test; gate_coverage; gate_cppcheck; gate_report ;;
    *) echo "usage: $0 test|coverage|cppcheck|report|all" >&2; exit 2 ;;
esac
