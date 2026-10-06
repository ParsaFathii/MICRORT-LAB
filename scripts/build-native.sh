#!/usr/bin/env bash
# MicroRT-Lab native build: kernel (C11) + engine (C++20).
# Usage: ./scripts/build-native.sh [build-dir]   (default: engine/build)
set -euo pipefail
BUILD_DIR="${1:-engine/build}"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 2)"
echo "Binary: $BUILD_DIR/micrort-engine"
