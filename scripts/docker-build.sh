#!/usr/bin/env bash
# Docker-based build wrapper for komari-agent-c
# Usage:
#   ./scripts/docker-build.sh           # Build all architectures
#   ./scripts/docker-build.sh arm64     # Build specific architecture
#   ./scripts/docker-build.sh test      # Run unit tests
#
# Environment variables:
#   APT_MIRROR=cn   Use Tsinghua apt mirror for faster builds in China
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

COMPOSE_FILE="docker/docker-compose.yml"
ARCH="${1:-all}"

# Export APT_MIRROR for docker-compose variable interpolation (default: official mirrors)
export APT_MIRROR="${APT_MIRROR:-default}"

if [ "$APT_MIRROR" = "cn" ]; then
    echo "Using Tsinghua apt mirror (APT_MIRROR=cn)"
fi

case "$ARCH" in
  all)
    echo "Building all architectures..."
    mkdir -p output
    for arch in amd64 arm64 arm armv7 mipsel mips64 riscv64 386; do
      echo ""
      echo ">>> Building linux/$arch"
      docker compose -f "$COMPOSE_FILE" run --rm "build-$arch"
    done
    echo ""
    echo "All builds completed. Binaries in output/"
    ls -lh output/
    ;;
  test)
    echo "Running unit tests in Docker..."
    mkdir -p output
    docker compose -f "$COMPOSE_FILE" run --rm test
    ;;
  amd64|arm64|arm|armv7|mipsel|mips64|riscv64|386)
    echo "Building linux/$ARCH..."
    mkdir -p output
    docker compose -f "$COMPOSE_FILE" run --rm "build-$ARCH"
    echo "Binary: output/komari-agent-c-linux-$ARCH"
    ;;
  *)
    echo "Usage: $0 [all|test|amd64|arm64|arm|armv7|mipsel|mips64|riscv64|386]"
    echo "  Set APT_MIRROR=cn for Chinese apt mirror"
    exit 1
    ;;
esac
