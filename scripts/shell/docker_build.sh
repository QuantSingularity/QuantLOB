#!/usr/bin/env bash
# docker_build.sh - Build and optionally run the Docker image
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "==> Building Docker image quantlob:latest..."
docker build \
    -f "${ROOT}/infrastructure/docker/Dockerfile" \
    -t quantlob:latest \
    "${ROOT}"

echo "==> Image built. Run with:"
echo "    docker run --rm quantlob:latest --events 100000"
echo "    docker run --rm -v \$(pwd)/out:/app/out quantlob:latest --export-snapshot --out-dir /app/out"
