#!/usr/bin/env bash
# Build and run the full QuantLOB stack: the C++ REST server serving the built
# React frontend on a single port.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PORT="${QUANTLOB_PORT:-8080}"
FRONTEND="$ROOT/frontend"
DIST="$FRONTEND/dist"

echo "[1/3] Building frontend"
if [ ! -d "$DIST" ] || [ -n "${REBUILD_FRONTEND:-}" ]; then
  ( cd "$FRONTEND" && npm install --no-audit --no-fund && npm run build )
else
  echo "  dist already present (set REBUILD_FRONTEND=1 to force)"
fi

echo "[2/3] Building server (Release)"
cmake -S "$ROOT" -B "$ROOT/build" \
  -DQUANTLOB_BUILD_TESTS=OFF -DQUANTLOB_BUILD_BENCHMARKS=OFF \
  -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" -j --target quantlob_server

echo "[3/3] Starting server on http://localhost:$PORT"
# Pass an absolute path to the built frontend so static assets resolve
# regardless of the working directory.
exec "$ROOT/build/quantlob_server" "$PORT" "$DIST"
