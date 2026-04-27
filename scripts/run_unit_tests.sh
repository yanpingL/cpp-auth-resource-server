#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"

if [ "${FORCE_LOCAL_UNIT_TESTS:-0}" != "1" ] && [ ! -f "/.dockerenv" ]; then
    if command -v docker >/dev/null 2>&1 && docker ps --format '{{.Names}}' | grep -qx 'sys-web-1'; then
        docker exec -it sys-web-1 bash -lc "cd /workspace && ./scripts/run_unit_tests.sh '$BUILD_DIR'"
        exit 0
    fi
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -S . -B "$BUILD_DIR"
fi

cmake --build "$BUILD_DIR" --target unit_tests
ctest --test-dir "$BUILD_DIR" --output-on-failure -R unit_tests
