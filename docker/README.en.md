# Docker Build Environment

[中文](README.md) | **English**

## Overview

This directory contains the Docker-based build environment for **komari-agent-c**. All binary compilation runs inside Docker containers, ensuring reproducible builds across 8 CPU architectures without requiring cross-compilation toolchains on the host system.

## Files

| File | Description |
|------|-------------|
| `Dockerfile.build` | Build environment image based on Ubuntu 24.04, supports amd64/arm64/armhf/riscv64/i386 |
| `Dockerfile.legacy` | Build environment image based on Debian 12 (bookworm-slim), supports armel/mipsel/mips64el (these architectures are unavailable in Ubuntu 24.04 ports repo) |
| `docker-compose.yml` | Service definitions for 8 architecture builds + 1 test service |
| `build.sh` | Build script executed inside the container |
| `test.sh` | Unit test script executed inside the container |
| `README.md` | Chinese documentation |
| `README.en.md` | This file |

## Quick Start

### Prerequisites

- Docker Engine 20.10+
- Docker Compose v2+

### Build a Single Architecture

From the **project root directory**:

```bash
# Build amd64 (native)
./scripts/docker-build.sh amd64

# Build arm64 (cross-compile)
./scripts/docker-build.sh arm64

# Build riscv64 (cross-compile)
./scripts/docker-build.sh riscv64
```

### Build All Architectures

```bash
./scripts/docker-build.sh all
```

### Run Unit Tests

```bash
./scripts/docker-build.sh test
```

### Use Chinese Mirror (Faster in China)

```bash
APT_MIRROR=cn ./scripts/docker-build.sh amd64
```

## Supported Architectures

| Service | GOARCH | Debian Arch | Compiler | Base Image | Description |
|---------|--------|-------------|----------|------------|-------------|
| `build-amd64` | amd64 | amd64 | gcc | Ubuntu 24.04 | x86-64 64-bit |
| `build-arm64` | arm64 | arm64 | aarch64-linux-gnu-gcc | Ubuntu 24.04 | ARM 64-bit |
| `build-arm` | arm | armel | arm-linux-gnueabi-gcc | Debian 12 (bookworm-slim) | ARM soft-float |
| `build-armv7` | armv7 | armhf | arm-linux-gnueabihf-gcc | Ubuntu 24.04 | ARM hard-float |
| `build-mipsel` | mipsel | mipsel | mipsel-linux-gnu-gcc | Debian 12 (bookworm-slim) | MIPS little-endian |
| `build-mips64` | mips64 | mips64el | mips64el-linux-gnuabi64-gcc | Debian 12 (bookworm-slim) | MIPS64 little-endian |
| `build-riscv64` | riscv64 | riscv64 | riscv64-linux-gnu-gcc | Ubuntu 24.04 | RISC-V 64-bit |
| `build-386` | 386 | i386 | i686-linux-gnu-gcc | Ubuntu 24.04 | x86 32-bit |

> **Note**: armel, mipsel, and mips64el are not available in Ubuntu 24.04 ports repository (missing libc6-dev-*-cross packages), so Debian 12 (bookworm-slim) is used as the base image for these architectures.

## Build Output

Compiled binaries are written to the `output/` directory at the project root:

```
output/
├── komari-agent-c-linux-amd64
├── komari-agent-c-linux-arm64
├── komari-agent-c-linux-arm
├── komari-agent-c-linux-armv7
├── komari-agent-c-linux-mipsel
├── komari-agent-c-linux-mips64
├── komari-agent-c-linux-riscv64
└── komari-agent-c-linux-386
```

## How It Works

1. **Image Build**: Each service builds a Docker image with the target architecture's cross-compiler and libraries (OpenSSL, zlib)
2. **Source Mount**: Project source code is mounted read-only at `/src`
3. **Out-of-Source Build**: CMake builds in `/tmp/komari-build` (writable temp directory)
4. **Binary Verification**: The `file` command verifies the output binary matches the target architecture
5. **Output Copy**: The verified binary is copied to `/output/` which maps to `output/` on the host

## APT Mirror Configuration

The `APT_MIRROR` build argument controls which apt mirror is used inside the container:

| Value | Mirror | Use Case |
|-------|--------|----------|
| `default` | Official mirrors (Ubuntu/Debian) | CI (GitHub Actions) |
| `cn` | Tsinghua University mirror | Local builds in China |

Set via environment variable:

```bash
export APT_MIRROR=cn
./scripts/docker-build.sh amd64
```

Or inline:

```bash
APT_MIRROR=cn ./scripts/docker-build.sh amd64
```

## Troubleshooting

### "Read-only file system" errors

This should not occur with the current configuration. The build uses out-of-source compilation (`cmake -B /tmp/komari-build -S /src`). If you see this error, ensure you are using the latest `build.sh`.

### Docker image not updating after code changes

Docker Compose may use a cached image. Force a rebuild:

```bash
docker compose -f docker/docker-compose.yml build --no-cache build-amd64
```

### Binary architecture mismatch

The build script automatically verifies the binary architecture using the `file` command. If you see "Architecture mismatch", the cross-compiler may not be correctly installed. Try rebuilding the image:

```bash
docker compose -f docker/docker-compose.yml build --no-cache build-arm64
```
