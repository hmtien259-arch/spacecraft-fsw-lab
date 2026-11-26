#!/usr/bin/env bash
# tools/check_traceability.sh - every requirement, in the labs and in the capstone, traces to a test.
#
#   bash tools/check_traceability.sh            # labs (Session 12 rule) + capstone baseline rules
#   bash tools/check_traceability.sh --strict   # + no capstone requirement may be pending
#                                               #   (the Session 13 acceptance criterion)
#
# Lab requirements (HK-REQ, TMTC-REQ, FDIR-REQ): tools/check_traceability.py, unchanged.
# Capstone requirements (SYS, SW-*): tools/check_capstone_traceability.py. Both are stdlib Python,
# run with -I. This wrapper is the name the Session 13 brief asks for. Why the brief's grep sketch
# is not used: see the docstring of check_capstone_traceability.py.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

python3 -I tools/check_traceability.py
python3 -I tools/check_capstone_traceability.py "$@"
