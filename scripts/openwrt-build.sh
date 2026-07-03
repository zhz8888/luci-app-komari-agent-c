#!/usr/bin/env bash
# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# OpenWrt package build helper.
#
# This script isolates the OpenWrt package build from the standalone
# binary build by using a separate CMake build subdirectory (build-openwrt
# by default). It is invoked by the OpenWrt SDK's Build/Compile step
# and can also be run manually for local OpenWrt cross-compilation tests.
#
# Usage:
#   # Inside OpenWrt SDK (called by Build/Compile in openwrt/Makefile):
#   #   environment variables TARGET_CC, STAGING_DIR, PKG_BUILD_DIR are set
#   #   by the SDK; this script is NOT called directly in that case.
#
#   # Local cross-compile test (requires OpenWrt SDK env sourced):
#   ./scripts/openwrt-build.sh
#
#   # Local cross-compile test with custom build subdir:
#   KOMARI_BUILD_SUBDIR=build-openwrt-custom ./scripts/openwrt-build.sh
#
# Environment variables:
#   SRC_DIR             - source directory (default: parent of scripts/)
#   KOMARI_BUILD_SUBDIR - CMake build subdirectory name (default: build-openwrt)
#   KOMARI_BUILD_TYPE   - CMake build type (default: Release)
#   EXTRA_CMAKE_ARGS    - extra CMake arguments (optional)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SRC_DIR:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
KOMARI_BUILD_SUBDIR="${KOMARI_BUILD_SUBDIR:-build-openwrt}"
KOMARI_BUILD_TYPE="${KOMARI_BUILD_TYPE:-Release}"
BUILD_DIR="${SRC_DIR}/${KOMARI_BUILD_SUBDIR}"

echo "=========================================="
echo "OpenWrt cross-compile (local test)"
echo "  SRC_DIR:             ${SRC_DIR}"
echo "  KOMARI_BUILD_SUBDIR: ${KOMARI_BUILD_SUBDIR}"
echo "  BUILD_DIR:           ${BUILD_DIR}"
echo "  KOMARI_BUILD_TYPE:   ${KOMARI_BUILD_TYPE}"
echo "=========================================="

# ---------------------------------------------------------------------------
# Verify OpenWrt SDK environment
# ---------------------------------------------------------------------------
if [ -z "${TARGET_CC:-}" ]; then
    echo "WARNING: TARGET_CC is not set."
    echo "  For a real OpenWrt build, source the OpenWrt SDK env first:"
    echo "    . /path/to/openwrt-sdk/env.sh"
    echo ""
    echo "  Continuing with a best-effort native build for syntax check..."
    echo ""
    export TARGET_CC="${CC:-gcc}"
fi

if [ -z "${STAGING_DIR:-}" ]; then
    echo "WARNING: STAGING_DIR is not set; library discovery may fail."
    # Fall back to the system root so find_package() can still discover
    # host libraries for a syntax-check build.
    export STAGING_DIR="/usr"
fi

# ---------------------------------------------------------------------------
# Configure CMake with the OpenWrt toolchain and openwrt profile
# ---------------------------------------------------------------------------
TOOLCHAIN_FILE="${SRC_DIR}/cmake/toolchain-openwrt.cmake"
if [ ! -f "${TOOLCHAIN_FILE}" ]; then
    echo "FAIL: toolchain file not found: ${TOOLCHAIN_FILE}"
    exit 1
fi

echo ""
echo "--- CMake configure ---"
rm -rf "${BUILD_DIR}"

cmake -B "${BUILD_DIR}" -S "${SRC_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE="${KOMARI_BUILD_TYPE}" \
    -DKOMARI_BUILD_PROFILE=openwrt \
    -DKOMARI_BUILD_TESTS=OFF \
    -DKOMARI_ENABLE_CPACK=OFF \
    -DKOMARI_VERBOSE_CMAKE=ON \
    ${EXTRA_CMAKE_ARGS:-}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo ""
echo "--- CMake build ---"
START_TIME=$(date +%s)

cmake --build "${BUILD_DIR}" -- -j"$(nproc 2>/dev/null || echo 1)"

END_TIME=$(date +%s)
BUILD_TIME=$((END_TIME - START_TIME))
echo "Build completed in ${BUILD_TIME}s"

# ---------------------------------------------------------------------------
# Verify binary
# ---------------------------------------------------------------------------
BINARY="${BUILD_DIR}/bin/komari-agent-c"
if [ ! -f "${BINARY}" ]; then
    echo "FAIL: Binary not found at ${BINARY}"
    exit 1
fi

BINARY_SIZE=$(stat -c%s "${BINARY}" 2>/dev/null || stat -f%z "${BINARY}" 2>/dev/null)
echo ""
echo "Binary: ${BINARY}"
echo "Size:   $((BINARY_SIZE / 1024)) KB"
echo "Type:   $(file "${BINARY}")"
echo ""
echo "OpenWrt cross-compile test succeeded."
echo "Binary is in isolated directory: ${KOMARI_BUILD_SUBDIR}/"
