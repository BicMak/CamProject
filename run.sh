#!/usr/bin/env bash
set -euo pipefail

# 스크립트 위치 기준 절대 경로 추출
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
EXEC_PATH="${BUILD_DIR}/unicam_capture"

# 실행 파일 존재 여부 확인
if [[ ! -x "$EXEC_PATH" ]]; then
    echo "Error: '${EXEC_PATH}' executable not found."
    echo "Please run build script first."
    exit 1
fi

echo "--- Running unicam_capture ---"
"$EXEC_PATH"