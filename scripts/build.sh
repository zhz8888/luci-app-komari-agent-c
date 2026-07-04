#!/bin/bash
#
# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Supported platforms: Linux
#
# Build script (based on CMake)
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No color

info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="release"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="$PROJECT_ROOT/output"
CLEAN_BUILD=false
SHOW_HELP=false
RUN_TESTS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="debug"
            shift
            ;;
        --release)
            BUILD_TYPE="release"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --test)
            RUN_TESTS=true
            shift
            ;;
        -h|--help)
            SHOW_HELP=true
            shift
            ;;
        *)
            error "Unknown option: $1"
            ;;
    esac
done

if [ "$SHOW_HELP" = true ]; then
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --debug      Build with debug flags (CMAKE_BUILD_TYPE=Debug)"
    echo "  --release    Build with release flags (CMAKE_BUILD_TYPE=Release, default)"
    echo "  --clean      Clean build directory before building"
    echo "  --test       Run tests after building (ctest)"
    echo "  -h, --help   Show this help message"
    echo ""
    echo "Build commands:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=<Debug|Release>"
    echo "  cmake --build build"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build in release mode"
    echo "  $0 --debug --clean    # Clean and build in debug mode"
    echo "  $0 --release --test   # Build and run tests"
    exit 0
fi

detect_os() {
    local os_name
    os_name=$(uname -s)

    case "$os_name" in
        Linux*)
            echo "linux"
            ;;
        *)
            error "Unsupported operating system: $os_name"
            ;;
    esac
}

detect_arch() {
    local arch
    arch=$(uname -m)

    case "$arch" in
        x86_64|amd64)
            echo "x86_64"
            ;;
        aarch64|arm64)
            echo "aarch64"
            ;;
        armv7l|armv7)
            echo "armv7"
            ;;
        i386|i686)
            echo "i386"
            ;;
        *)
            warn "Unknown architecture: $arch, using original value"
            echo "$arch"
            ;;
    esac
}

# Used to name the output binary, e.g. komari-agent-c-Linux-x86_64
detect_platform() {
    local platform=$(uname -s)
    local arch=$(uname -m)
    echo "$platform-$arch"
}

check_dependencies() {
    local os=$1

    info "Checking build dependencies..."

    # Check compiler
    if ! command -v gcc &> /dev/null && ! command -v cc &> /dev/null; then
        error "GCC or CC compiler not found. Please install build-essential or equivalent"
    fi

    # Check cmake
    if ! command -v cmake &> /dev/null; then
        error "cmake not found. Please install cmake (version >= 3.10)"
    fi

    # Check OpenSSL development library
    if [ "$os" = "linux" ]; then
        # Distributions install headers under /usr/include (packaged) or
        # /usr/local/include (manual build); check both to avoid false negatives.
        if [ ! -f "/usr/include/openssl/ssl.h" ] && [ ! -f "/usr/local/include/openssl/ssl.h" ]; then
            warn "OpenSSL development headers not found. Build may fail"
            warn "Please install libssl-dev (Debian/Ubuntu) or openssl-devel (RHEL/Fedora)"
        fi

        # Check zlib development library
        if [ ! -f "/usr/include/zlib.h" ] && [ ! -f "/usr/local/include/zlib.h" ]; then
            warn "zlib development headers not found. Build may fail"
            warn "Please install zlib1g-dev (Debian/Ubuntu) or zlib-devel (RHEL/Fedora)"
        fi
    fi

    info "All required dependencies found"
}

install_dependencies() {
    local os=$1

    info "Installing dependencies for $os..."

    case "$os" in
        linux)
            if command -v apt-get &> /dev/null; then
                info "Detected Debian/Ubuntu system"
                sudo apt-get update
                sudo apt-get install -y build-essential cmake libssl-dev zlib1g-dev pkg-config
            elif command -v yum &> /dev/null; then
                info "Detected RHEL/CentOS system"
                sudo yum groupinstall -y "Development Tools"
                sudo yum install -y cmake openssl-devel zlib-devel pkgconfig
            elif command -v dnf &> /dev/null; then
                info "Detected Fedora system"
                sudo dnf groupinstall -y "Development Tools"
                sudo dnf install -y cmake openssl-devel zlib-devel pkgconfig
            elif command -v apk &> /dev/null; then
                info "Detected Alpine Linux system"
                sudo apk add build-base cmake openssl-dev zlib-dev pkgconfig
            else
                warn "Unrecognized Linux distribution. Please install dependencies manually"
            fi
            ;;
    esac

    info "Dependencies installed successfully"
}

clean_build() {
    info "Cleaning build directory..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        info "Build directory cleaned"
    else
        info "Build directory does not exist"
    fi
    if [ -d "$OUTPUT_DIR" ]; then
        rm -rf "$OUTPUT_DIR"
        info "Output directory cleaned"
    fi
}

do_build() {
    local os=$1
    local arch=$2

    info "Starting build for $os ($arch)..."
    info "Build type: $BUILD_TYPE"

    cd "$PROJECT_ROOT"

    # Determine CMake build type
    local cmake_build_type="Release"
    if [ "$BUILD_TYPE" = "debug" ]; then
        cmake_build_type="Debug"
    fi

    # Execute CMake configuration
    info "Configuring with CMake (BUILD_TYPE=$cmake_build_type)..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$cmake_build_type"

    # Execute CMake build
    info "Building..."
    if cmake --build "$BUILD_DIR"; then
        local binary_path="$BUILD_DIR/bin/komari-agent-c"

        if [ ! -f "$binary_path" ]; then
            error "Build reported success but binary not found: $binary_path"
        fi

        info "Build successful!"
        info "Binary location: $binary_path"

        # Show file information
        ls -lh "$binary_path"

        # Show file type
        if command -v file &> /dev/null; then
            file "$binary_path"
        fi

        # Copy to output directory
        local platform=$(detect_platform)
        mkdir -p "$OUTPUT_DIR"
        cp "$binary_path" "$OUTPUT_DIR/komari-agent-c-$platform"
        info "Binary saved to: $OUTPUT_DIR/komari-agent-c-$platform"

        return 0
    else
        error "Build failed! Binary not found: $BUILD_DIR/bin/komari-agent-c"
    fi
}

do_test() {
    info "Running tests..."

    local binary_path="$BUILD_DIR/bin/komari-agent-c"

    if [ -f "$binary_path" ]; then
        # Check if binary is executable
        if [ -x "$binary_path" ]; then
            info "Binary is executable"

            # Run basic functionality tests
            info "Testing binary execution..."
            "$binary_path" --help 2>&1 | head -5 || warn "Help command execution failed"
        else
            error "Binary is not executable"
        fi
    else
        error "Binary not found. Please build first"
    fi

    # Run ctest unit tests
    if [ -d "$BUILD_DIR" ]; then
        info "Running ctest unit tests..."
        (cd "$BUILD_DIR" && ctest --output-on-failure) || warn "ctest execution failed or no tests registered"
    fi
}

main() {
    info "=== Linux Build Script (CMake) ==="
    info "Project root: $PROJECT_ROOT"

    # Detect platform
    local os
    os=$(detect_os)
    info "Detected operating system: $os"

    local arch
    arch=$(detect_arch)
    info "Detected architecture: $arch"

    # Check dependencies
    check_dependencies "$os"

    # Clean build (if needed)
    if [ "$CLEAN_BUILD" = true ]; then
        clean_build
    fi

    # Execute build
    do_build "$os" "$arch"

    # Run tests (if needed)
    if [ "$RUN_TESTS" = true ]; then
        do_test
    fi

    info "=== Build Complete ==="
    info "Platform: $os ($arch)"
    info "Build type: $BUILD_TYPE"
    info "Binary: $BUILD_DIR/bin/komari-agent-c"
}

main "$@"
