#!/usr/bin/env bash
# Build script for komari-agent-c (runs inside Docker container)
# Environment variables:
#   GOARCH - target architecture name (amd64, arm64, arm, armv7, mipsel, mips64, riscv64, 386)
#   DEBIAN_ARCH - Debian architecture name (amd64, arm64, armel, armhf, mipsel, mips64el, riscv64, i386)
#   MULTIARCH_TRIPLET - multiarch triplet (x86_64-linux-gnu, aarch64-linux-gnu, etc.)
#   CC - C compiler binary name
#   OUTPUT_DIR - output directory for compiled binary
#   SRC_DIR - source directory (mounted read-only)
set -euo pipefail

GOARCH="${GOARCH:-amd64}"
DEBIAN_ARCH="${DEBIAN_ARCH:-amd64}"
MULTIARCH_TRIPLET="${MULTIARCH_TRIPLET:-x86_64-linux-gnu}"
CC="${CC:-gcc}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
SRC_DIR="${SRC_DIR:-/src}"
BUILD_DIR="${BUILD_DIR:-/tmp/komari-build}"

echo "=========================================="
echo "Building komari-agent-c for linux/$GOARCH"
echo "  DEBIAN_ARCH: $DEBIAN_ARCH"
echo "  MULTIARCH_TRIPLET: $MULTIARCH_TRIPLET"
echo "  CC: $CC"
echo "  SRC_DIR: $SRC_DIR (read-only)"
echo "  BUILD_DIR: $BUILD_DIR"
echo "=========================================="

# Verify cross-compilation environment
echo ""
echo "--- Verify cross-compilation environment ---"

if ! command -v "$CC" &> /dev/null; then
    echo "FAIL: Compiler '$CC' not found"
    exit 1
fi
echo "CC found: $(command -v "$CC")"

if [ "$DEBIAN_ARCH" != "amd64" ]; then
    LIB_DIR="/usr/lib/$MULTIARCH_TRIPLET"

    # Verify OpenSSL
    SSL_FOUND=0
    for lib in "$LIB_DIR/libssl.so" "$LIB_DIR/libssl.so.3" "$LIB_DIR/libcrypto.so" "$LIB_DIR/libcrypto.so.3"; do
        if [ -f "$lib" ]; then
            echo "OpenSSL: $lib"
            SSL_FOUND=1
            break
        fi
    done
    if [ "$SSL_FOUND" -eq 0 ]; then
        echo "FAIL: OpenSSL not found in $LIB_DIR"
        exit 1
    fi

    # Verify zlib
    if [ ! -f "$LIB_DIR/libz.so" ] && [ ! -f "$LIB_DIR/libz.so.1" ]; then
        echo "FAIL: zlib not found in $LIB_DIR"
        exit 1
    fi
    echo "zlib: found in $LIB_DIR"

    # Verify pkg-config
    export PKG_CONFIG_LIBDIR="$LIB_DIR/pkgconfig:/usr/share/pkgconfig"
    if ! pkg-config --exists openssl zlib; then
        echo "FAIL: pkg-config cannot find openssl/zlib"
        pkg-config --print-errors openssl zlib
        exit 1
    fi
    echo "pkg-config: openssl=$(pkg-config --modversion openssl), zlib=$(pkg-config --modversion zlib)"
else
    echo "Native build (amd64), skipping cross-compilation library verification"
fi

echo "Cross-compilation environment verification passed."
echo ""

# Build (out-of-source build in writable temp directory)
echo "--- Build binary ---"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

EXTRA_CMAKE_ARGS="-DKOMARI_BUILD_PROFILE=binary -DKOMARI_BUILD_TESTS=OFF"
if [ "$DEBIAN_ARCH" != "amd64" ]; then
    export PKG_CONFIG_LIBDIR="/usr/lib/$MULTIARCH_TRIPLET/pkgconfig:/usr/share/pkgconfig"
    EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$DEBIAN_ARCH -DCMAKE_LIBRARY_ARCHITECTURE=$MULTIARCH_TRIPLET"
fi

START_TIME=$(date +%s)

if ! cmake -B "$BUILD_DIR" -S "$SRC_DIR" -DCMAKE_C_COMPILER="$CC" $EXTRA_CMAKE_ARGS; then
    echo "FAIL: CMake configuration failed"
    exit 1
fi

if ! cmake --build "$BUILD_DIR"; then
    echo "FAIL: Build failed"
    exit 1
fi

END_TIME=$(date +%s)
BUILD_TIME=$((END_TIME - START_TIME))
echo "Build completed in ${BUILD_TIME}s"

if [ ! -f "$BUILD_DIR/bin/komari-agent-c" ]; then
    echo "FAIL: Binary not found at $BUILD_DIR/bin/komari-agent-c"
    exit 1
fi

BINARY_SIZE=$(stat -c%s "$BUILD_DIR/bin/komari-agent-c" 2>/dev/null || stat -f%z "$BUILD_DIR/bin/komari-agent-c" 2>/dev/null)
echo "Binary size: $((BINARY_SIZE / 1024)) KB"

# Verify binary architecture
echo ""
echo "--- Verify binary architecture ---"
FILE_OUTPUT=$(file "$BUILD_DIR/bin/komari-agent-c")
echo "file: $FILE_OUTPUT"

case "$GOARCH" in
    amd64)        EXPECTED_ARCH="x86-64" ;;
    arm64)        EXPECTED_ARCH="aarch64" ;;
    arm|armv7)    EXPECTED_ARCH="ARM" ;;
    386)          EXPECTED_ARCH="Intel 80386" ;;
    mipsel|mips64) EXPECTED_ARCH="MIPS" ;;
    riscv64)      EXPECTED_ARCH="RISC-V" ;;
    *)            EXPECTED_ARCH="unknown" ;;
esac

if echo "$FILE_OUTPUT" | grep -q "$EXPECTED_ARCH"; then
    echo "Architecture matches: $EXPECTED_ARCH"
else
    echo "FAIL: Architecture mismatch (expected $EXPECTED_ARCH)"
    exit 1
fi

# Copy to output
mkdir -p "$OUTPUT_DIR"
cp "$BUILD_DIR/bin/komari-agent-c" "$OUTPUT_DIR/komari-agent-c-linux-$GOARCH"
echo ""
echo "Binary copied to $OUTPUT_DIR/komari-agent-c-linux-$GOARCH"
echo "Build successful for linux/$GOARCH"
