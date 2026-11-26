#!/usr/bin/env python3
"""tools/check_capstone_traceability.py - capstone requirements, scenarios and tests must agree.

Inputs (all under capstone/):
  requirements.md   requirement IDs (SYS-nn, SW-XX-nn): first cell of a table row
  scenarios.md      scenario IDs (SCN-nn) with the requirements each covers, and a "### SCN-nn"
                    section holding the detailed pass conditions
  traceability.md   one row per requirement: Implemented by | Verified by | Scenario | Status
  tests/*.c         test sources (test_*.c, *_test.c)

Fails (exit 1) when
  * a requirement ID is duplicated, or has no row / more than one row in traceability.md,
    or traceability.md has a row for an ID that is not a requirement;
  * a SCN ID is used anywhere but not defined in scenarios.md, a scenario covers no requirement,
    or has no detailed section;
  * scenarios.md and traceability.md disagree on which scenario covers which requirement, or the
    "Verified by" column of requirements.md names different scenarios from traceability.md;
  * a Status is not `pending` or `verified`, or a row is `verified` without an executed test;
  * a backticked test name is not defined in a capstone test source, or is defined but never called
    (the Session 12 rule: a test that nothing calls verifies nothing);
  * --strict (the acceptance gate, Session 13 section 7): any row is still `pending`.

Why not the session sketch (grep each ID in tests/ and traceability.md): traceability.md lists every
ID by design, so that grep always succeeds. Its regex `(SYS|SW-[A-Z]+|)-[0-9]+` also has an empty
alternative that extracts "-09" from a date, and `grep -rqs "-09"` parses "-09" as an option and
matches everything. See docs/12-capstone-baseline.md.

Stdlib only. Run with `python3 -I` so the working directory is not on the import path.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CAP = ROOT / "capstone"
REQ_RE = re.compile(r"^(SYS-\d{2}|SW-[A-Z]{2}-\d{2})$")
SCN_RE = re.compile(r"^SCN-\d{2}$")
SCN_REF = re.compile(r"\bSCN-\d{2}\b")
REQ_REF = re.compile(r"\b(?:SYS-\d{2}|SW-[A-Z]{2}-\d{2})\b")
NAME_RE = re.compile(r"`([A-Za-z_][A-Za-z0-9_]*)(?:\(\))?`")
TEST_PREFIXES = ("test_", "check_", "scenario_")
STATUSES = ("pending", "verified")


def rel(p):
    return p.relative_to(ROOT).as_posix()


def table_rows(md):
    """Yields (header cells, row cells) for every table body row in a markdown file."""
    header = None
    for line in md.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            header = None
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if header is None:
            header = cells
        elif all(set(c) <= set("-: ") for c in cells):
            continue                                   # the |---|---| separator
        else:
            yield header, cells


def main():
    strict = "--strict" in sys.argv[1:]
    problems = []
    req_md, scn_md, tr_md = CAP / "requirements.md", CAP / "scenarios.md", CAP / "traceability.md"
    for f in (req_md, scn_md, tr_md):
        if not f.exists():
            print("check_capstone_traceability.py: FAILED\n  missing %s" % rel(f), file=sys.stderr)
            return 1

    # --- requirements.md ---------------------------------------------------------------------
    reqs, req_scn = [], {}
    for header, cells in table_rows(req_md):
        if not REQ_RE.match(cells[0]):
            continue
        rid = cells[0]
        if rid in req_scn:
            problems.append("%s: requirement %s defined twice" % (rel(req_md), rid))
            continue
        reqs.append(rid)
        col = header.index("Verified by") if "Verified by" in header else None
        req_scn[rid] = set(SCN_REF.findall(cells[col])) if col is not None and col < len(cells) else set()
    if not reqs:
        problems.append("%s: no requirement IDs found" % rel(req_md))

    # --- scenarios.md ------------------------------------------------------------------------
    scn_text = scn_md.read_text(encoding="utf-8")
    scenarios = {}                                    # SCN -> set of requirement IDs it covers
    for header, cells in table_rows(scn_md):
        if SCN_RE.match(cells[0]) and "Requirements" in header:
            sid = cells[0]
            if sid in scenarios:
                problems.append("%s: scenario %s defined twice" % (rel(scn_md), sid))
            scenarios[sid] = set(REQ_REF.findall(cells[header.index("Requirements")]))
    for sid, covered in sorted(scenarios.items()):
        if not covered:
            problems.append("%s: %s covers no requirement" % (rel(scn_md), sid))
        for rid in sorted(covered - set(reqs)):
            problems.append("%s: %s covers %s, which is not a requirement" % (rel(scn_md), sid, rid))
        if not re.search(r"(?m)^###\s+%s\b" % re.escape(sid), scn_text):
            problems.append("%s: %s has no '### %s' section with detailed pass conditions"
                            % (rel(scn_md), sid, sid))

    # --- capstone tests ----------------------------------------------------------------------
    tests_dir = CAP / "tests"
    test_srcs = sorted(p for p in tests_dir.rglob("*.c")
                       if p.name.startswith("test_") or p.name.endswith("_test.c"))
    defined = {}
    for src in test_srcs:
        for m in re.finditer(r"(?m)^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{",
                             src.read_text(encoding="utf-8")):
            if m.group(1).startswith(TEST_PREFIXES):
                defined[m.group(1)] = rel(src)
    corpus = "\n".join(p.read_text(encoding="utf-8") for p in sorted(CAP.rglob("*.c")))

    def called(name):
        return len(re.findall(r"\b%s\s*\(" % re.escape(name), corpus)) >= 2

    # --- traceability.md ---------------------------------------------------------------------
    rows = {}
    need = ("Requirement", "Verified by", "Scenario", "Status")
    for header, cells in table_rows(tr_md):
        if not REQ_RE.match(cells[0]):
            continue
        missing = [h for h in need if h not in header]
        if missing:
            problems.append("%s: table header lacks %s" % (rel(tr_md), ", ".join(missing)))
            break
        cell = {h: (cells[header.index(h)] if header.index(h) < len(cells) else "") for h in need}
        rid = cell["Requirement"]
        if rid in rows:
            problems.append("%s: %s has more than one row" % (rel(tr_md), rid))
            continue
        tests = [n for n in NAME_RE.findall(cell["Verified by"]) if n.startswith(TEST_PREFIXES)]
        good = []
        for n in tests:
            if n not in defined:
                problems.append("%s: %s names `%s`, which is not defined in any capstone test source"
                                % (rel(tr_md), rid, n))
            elif not called(n):
                problems.append("%s: %s names `%s`, which is defined but never called"
                                % (rel(tr_md), rid, n))
            else:
                good.append(n)
        rows[rid] = {"tests": good, "scn": set(SCN_REF.findall(cell["Scenario"])),
                     "status": cell["Status"].strip("`* ").lower()}

    for rid in reqs:
        if rid not in rows:
            problems.append("%s: requirement %s has no row" % (rel(tr_md), rid))
    for rid in sorted(set(rows) - set(reqs)):
        problems.append("%s: row %s is not a requirement in %s" % (rel(tr_md), rid, rel(req_md)))

    pending = []
    for rid in reqs:
        r = rows.get(rid)
        if r is None:
            continue
        if r["status"] not in STATUSES:
            problems.append("%s: %s status '%s' is not one of %s" % (rel(tr_md), rid, r["status"], STATUSES))
        if r["status"] == "verified" and not r["tests"]:
            problems.append("%s: %s is marked verified but names no executed test" % (rel(tr_md), rid))
        if r["status"] == "pending":
            pending.append(rid)
        for sid in sorted(r["scn"] - set(scenarios)):
            problems.append("%s: %s refers to %s, which is not defined in %s"
                            % (rel(tr_md), rid, sid, rel(scn_md)))
        from_scn = {sid for sid, cov in scenarios.items() if rid in cov}
        if r["scn"] != from_scn:
            problems.append("%s: %s lists scenarios %s but scenarios.md says %s"
                            % (rel(tr_md), rid, sorted(r["scn"]) or "none", sorted(from_scn) or "none"))
        if req_scn.get(rid, set()) != r["scn"]:
            problems.append("%s: %s names scenarios %s but traceability.md says %s"
                            % (rel(req_md), rid, sorted(req_scn[rid]) or "none", sorted(r["scn"]) or "none"))

    if strict:
        for rid in pending:
            problems.append("%s: %s is still pending (no passing test): acceptance not met" % (rel(tr_md), rid))

    verified = len(reqs) - len(pending)
    if problems:
        print("check_capstone_traceability.py: FAILED%s" % (" (strict)" if strict else ""), file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("check_capstone_traceability.py: ok%s: %d requirements, %d scenarios, %d verified, %d pending%s"
          % (" (strict)" if strict else "", len(reqs), len(scenarios), verified, len(pending),
             (": " + " ".join(pending)) if pending and len(pending) <= 8 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
