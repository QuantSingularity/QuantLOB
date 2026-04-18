#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"

if [[ ! -f "${BUILD_DIR}/quantlob_tests" ]]; then
    echo "Error: ${BUILD_DIR}/quantlob_tests not found. Build the project first."
    echo "  cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Debug"
    echo "  cmake --build ${BUILD_DIR} --parallel"
    exit 1
fi

cd "${BUILD_DIR}"
ctest --output-on-failure --parallel "$(nproc 2>/dev/null || echo 4)"
