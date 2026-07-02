#!/usr/bin/env bash
# Test script for komari-agent-c (runs inside Docker container)
# Runs cmake with BUILD_TESTING=ON and executes unit tests
set -euo pipefail

CC="${CC:-gcc}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
SRC_DIR="${SRC_DIR:-/src}"
BUILD_DIR="${BUILD_DIR:-/tmp/komari-build}"

echo "=========================================="
echo "Running unit tests for komari-agent-c"
echo "  CC: $CC"
echo "  SRC_DIR: $SRC_DIR (read-only)"
echo "  BUILD_DIR: $BUILD_DIR"
echo "=========================================="

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "--- CMake configure (with tests) ---"
if ! cmake -B "$BUILD_DIR" -S "$SRC_DIR" -DCMAKE_C_COMPILER="$CC" -DBUILD_TESTING=ON; then
    echo "FAIL: CMake configuration failed"
    exit 1
fi

echo "--- Build ---"
if ! cmake --build "$BUILD_DIR"; then
    echo "FAIL: Build failed"
    exit 1
fi

echo "--- Run unit tests ---"
if ! ctest --test-dir "$BUILD_DIR" --output-on-failure; then
    echo "FAIL: Unit tests failed"
    exit 1
fi

echo ""
echo "All unit tests passed."

# Also copy the binary to output for consistency
if [ -f "$BUILD_DIR/bin/komari-agent-c" ]; then
    mkdir -p "$OUTPUT_DIR"
    cp "$BUILD_DIR/bin/komari-agent-c" "$OUTPUT_DIR/komari-agent-c-linux-amd64"
    echo "Binary copied to $OUTPUT_DIR/komari-agent-c-linux-amd64"
fi
