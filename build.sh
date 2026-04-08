#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
BUILD_TYPE="${1:-Debug}"   # 인자 없으면 Debug, ./build.sh Release 로 변경 가능

echo "=== Build type: ${BUILD_TYPE} ==="

# 1) cmake configure (build 디렉터리 없거나 CMakeCache 없으면 실행)
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "--- Configuring ---"
    cmake -B "${BUILD_DIR}" -S "${PROJECT_DIR}" \
          -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
fi

# 2) build (코어 수 자동 감지)
echo "--- Building ---"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "=== Build complete ==="

# 3) 실행
echo "--- Running unicam_capture ---"
"${BUILD_DIR}/unicam_capture"
