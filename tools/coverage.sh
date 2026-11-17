#!/usr/bin/env bash
# tools/coverage.sh - structural coverage of the FDIR components with gcc and gcov.
#
#   bash tools/coverage.sh
#   MIN_LINES_FDIR=99 MIN_BRANCHES_FDIR=95 MIN_LINES_GEN=99 MIN_BRANCHES_GEN=90 bash tools/coverage.sh
#
# Two components, each measured by the tests that belong to it:
#   labs/05-fdir/fdir.c            by the Session 10 build (its own tests only, not the later
#                                   back-to-back comparison that also drives it)
#   labs/06-mbd/fdir_step_gen.c    by the Session 11 build (requirement tests + back-to-back)
# Output: a summary table, the uncovered lines and untaken branches, and build/coverage/summary.md.
# Gates: line coverage and "branches taken at least once" must not fall below the minimums. The
# minimums sit just under today's numbers, so they catch a regression, but they are not a target:
# read the uncovered list and decide whether each gap is a missing test or an unreachable guard.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

COV="$ROOT/build/cov"
OUT="$ROOT/build/coverage"
rm -rf "$COV" "$OUT"; mkdir -p "$OUT"

COVERAGE=1 SKIP_RTOS=1 OUT_DIR="$COV" bash tools/run_tests.sh >"$OUT/run.log" 2>&1 \
    || { tail -n 20 "$OUT/run.log" >&2; echo "FAIL: tests failed under coverage" >&2; exit 1; }

report() {   # report <label> <source file> <gcda prefix> <min lines %> <min branches taken %>
    local label="$1" src="$2" prefix="$3" MIN_LINES="$4" MIN_BRANCHES="$5" name; name="$(basename "$src")"
    gcov -b -c -o "$COV" "$COV/$prefix.gcda" >"$OUT/$name.gcov.txt" 2>&1
    mv "$name.gcov" "$OUT/" 
    python3 -I - "$label" "$src" "$OUT/$name.gcov" "$MIN_LINES" "$MIN_BRANCHES" "$OUT/summary.md" <<'PY'
import re, sys
label, src, gcov, min_l, min_b, summary = sys.argv[1:7]
min_l, min_b = float(min_l), float(min_b)
lines = open(gcov, encoding="utf-8", errors="replace").read().splitlines()
tot = hit = 0
branches = taken = 0
cur = ("?", "")
unc, unt = [], []
for l in lines:
    m = re.match(r"\s*([\d#=\-\*]+):\s*(\d+):(.*)", l)
    if m:
        cnt, no, text = m.groups()
        cur = (no, text.strip())
        if cnt.strip() != "-" and no != "0":
            tot += 1
            if cnt.strip().startswith(("#", "=")):
                unc.append(cur)
            else:
                hit += 1
        continue
    b = re.match(r"branch\s+\d+ (taken (\d+)|never executed)", l)
    if b:
        branches += 1
        if b.group(2) is not None and int(b.group(2)) > 0:
            taken += 1
        else:
            unt.append(cur)
lp = 100.0 * hit / tot
bp = 100.0 * taken / branches
row = "| %s | `%s` | %.1f%% (%d/%d) | %.1f%% (%d/%d) |" % (label, src, lp, hit, tot, bp, taken, branches)
with open(summary, "a", encoding="utf-8") as f:
    if f.tell() == 0:
        f.write("# FDIR coverage\n\n| Component | File | Lines | Branches taken |\n|---|---|---|---|\n")
    f.write(row + "\n")
print(row)
seen = set()
for no, text in unc:
    print("   line not executed  %s: %s" % (no, text[:90]))
for no, text in unt:
    if no not in seen:
        seen.add(no)
        print("   branch not taken   %s: %s" % (no, text[:90]))
bad = []
if lp < min_l: bad.append("line coverage %.1f%% < %.0f%%" % (lp, min_l))
if bp < min_b: bad.append("branch coverage %.1f%% < %.0f%%" % (bp, min_b))
if bad:
    print("COVERAGE GATE FAILED for %s: %s" % (src, "; ".join(bad)), file=sys.stderr)
    sys.exit(1)
PY
}

status=0
report "Hand-written FDIR (Session 10 tests)" labs/05-fdir/fdir.c 05-fdir-fdir "${MIN_LINES_FDIR:-99}" "${MIN_BRANCHES_FDIR:-95}" || status=1
report "Generated FDIR step (Session 11 tests)" labs/06-mbd/fdir_step_gen.c 06-mbd-fdir_step_gen "${MIN_LINES_GEN:-99}" "${MIN_BRANCHES_GEN:-90}" || status=1
echo "wrote build/coverage/summary.md"
exit $status
