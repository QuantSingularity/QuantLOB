#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

# Prefer Ninja when available, otherwise fall back to the default generator.
if command -v ninja >/dev/null 2>&1; then
  GEN_ARGS=(-G Ninja)
else
  echo "    (ninja not found; using the default CMake generator)"
  GEN_ARGS=()
fi

echo "==> Configuring QuantLOB (${BUILD_TYPE})..."
cmake -B "${BUILD_DIR}" \
      "${GEN_ARGS[@]}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DQUANTLOB_BUILD_TESTS=ON \
      -DQUANTLOB_BUILD_BENCHMARKS=ON \
      -DQUANTLOB_BUILD_MAIN=ON

echo "==> Building..."
cmake --build "${BUILD_DIR}" --parallel

echo ""
echo "==> Build complete. Binaries in ${BUILD_DIR}/:"
ls -lh "${BUILD_DIR}/quantlob" "${BUILD_DIR}/quantlob_tests" "${BUILD_DIR}/quantlob_bench" 2>/dev/null || true
echo ""
echo "Run tests:      cd ${BUILD_DIR} && ctest --output-on-failure"
echo "Run benchmarks: ./${BUILD_DIR}/quantlob_bench"
echo "Run simulator:  ./${BUILD_DIR}/quantlob --help"
