#!/usr/bin/env bash
# run_synthetic.sh - Run a synthetic simulation and visualize results
# Usage: ./scripts/shell/run_synthetic.sh [output_dir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-${ROOT}/out}"
mkdir -p "${OUT}"

BINARY="${ROOT}/build/quantlob"
if [[ ! -f "${BINARY}" ]]; then
    echo "==> Binary not found, building..."
    "${ROOT}/scripts/shell/build.sh" Release
fi

echo "==> Running synthetic simulation -> ${OUT}"
"${BINARY}" \
    --symbol AAPL \
    --events 500000 \
    --mid 150.0 \
    --tick 0.01 \
    --seed 42 \
    --levels 10 \
    --out-dir "${OUT}" \
    --export-snapshot \
    --export-trades

echo ""
echo "==> Visualizing order book..."
if command -v python3 &>/dev/null; then
    python3 "${ROOT}/scripts/python/visualize_lob.py" book \
        "${OUT}/AAPL_snapshot.csv" \
        --symbol AAPL \
        --out "${OUT}/orderbook.png" \
    && echo "    Chart: ${OUT}/orderbook.png"
else
    echo "    python3 not found - skipping visualization"
fi
