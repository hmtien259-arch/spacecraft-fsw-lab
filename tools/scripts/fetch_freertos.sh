#!/usr/bin/env bash
# tools/scripts/fetch_freertos.sh
#
# Fetches the FreeRTOS kernel (POSIX simulator port included) into
# labs/02-rtos/FreeRTOS/FreeRTOS-Kernel. Not vendored into this repo (see
# labs/02-rtos/.gitignore) so the repository stays small; re-run this any
# time after a fresh clone, or let `make` in labs/02-rtos/ call it for you.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEST="$REPO_ROOT/labs/02-rtos/FreeRTOS/FreeRTOS-Kernel"

if [ -d "$DEST/.git" ]; then
    echo "FreeRTOS-Kernel already present at $DEST"
    exit 0
fi

mkdir -p "$(dirname "$DEST")"
echo "Cloning FreeRTOS-Kernel (shallow) into $DEST ..."
git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git "$DEST"
echo "Done."
