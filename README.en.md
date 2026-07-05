# Komari Agent (C/OpenWrt Version)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![OpenWrt](https://img.shields.io/badge/platform-OpenWrt-orange.svg)](https://openwrt.org/)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![CI](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/ci.yml/badge.svg)](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/zhz8888/luci-app-komari-agent-c)](https://github.com/zhz8888/luci-app-komari-agent-c/releases)

**English** | [中文](README.md)

A lightweight monitoring agent designed for OpenWrt routers. It is a C language rewrite of [komari-monitor](https://github.com/komari-monitor/komari-agent).

## ✨ Core Features

- 🚀 **Lightweight & Efficient**: Binary size only ~100KB, memory usage < 3MB
- 📊 **Comprehensive Monitoring**: CPU, memory, disk, network, processes, connection count
- 🔒 **TLS Encryption**: Supports wss:// protocol for secure transmission of monitoring data
- 🔌 **WebSocket Communication**: Real-time data reporting, compliant with RFC 6455
- 💻 **Web SSH**: Supports remote terminal access
- 📈 **Traffic Statistics**: Monthly network card traffic statistics
- 🏓 **Ping Tasks**: Supports ICMP, TCP, and HTTP modes
- 🔄 **Auto Reconnection**: Automatically recovers connections after network disconnection
- 🖥️ **LuCI Frontend**: Provides a web interface for configuration management, real-time status monitoring, log viewing, and connection testing

## 📦 Quick Installation

### OpenWrt Systems (Recommended)

```bash
# OpenWrt >= 24.10 (using APK)
apk add komari-agent-c-1.0.0-1-<arch>.apk

# OpenWrt < 24.10 (using IPK)
opkg install komari-agent-c_1.0.0-1_<arch>.ipk
```

Install the LuCI web interface (optional):

```bash
# OpenWrt >= 24.10
apk add luci-app-komari-agent-c-1.0.0-1-<arch>.apk

# OpenWrt < 24.10
opkg install luci-app-komari-agent-c_1.0.0-1_<arch>.ipk
```

### Other Linux Systems

```bash
# Download and extract the binary
wget https://github.com/zhz8888/luci-app-komari-agent-c/releases/download/v1.0.0/komari-agent-c-1.0.0-linux-<arch>.tar.gz
tar -xzf komari-agent-c-1.0.0-linux-<arch>.tar.gz
sudo cp komari-agent-c /usr/local/bin/
```

## 🛠️ Local Build

### Standard Build (CMake)

```bash
# Configure and build (default Release)
cmake -B build && cmake --build build

# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Build output: build/bin/komari-agent-c
```

### CMake Presets

The project provides 9 standardized presets (see `CMakePresets.json`) for common build scenarios:

```bash
cmake --preset default      # Default Release + tests
cmake --preset debug        # Debug + verbose diagnostics
cmake --preset release      # Release + LTO, no tests
cmake --preset sanitize     # ASan + UBSan
cmake --preset coverage     # Code coverage
cmake --preset openwrt      # OpenWrt cross-compile (requires SDK env)
cmake --preset analyze      # clang-tidy static analysis
```

### Docker Cross-Compilation

Docker-based cross-compilation for 8 CPU architectures, no local toolchain required:

```bash
# Build a single architecture
./scripts/docker-build.sh amd64

# Build all architectures
./scripts/docker-build.sh all

# Run unit tests
./scripts/docker-build.sh test
```

See [docker/README.md](docker/README.md) for details.

### Running Tests

```bash
cmake -B build -DBUILD_TESTING=ON && cmake --build build
ctest --test-dir build --output-on-failure
```

## 📋 System Requirements

- OpenWrt 21.02 or later (CI tested on 24.10.x and 25.12.x)
- Minimum memory: 64MB
- Required dependencies: `libpthread`, `libopenssl`, `librt`, `zlib`

## 🔐 Security Recommendations

1. **Use HTTPS**: It is recommended to use HTTPS/WSS protocol for the endpoint
2. **Protect Token**: Do not leak the Token in public
3. **Certificate Verification**: Do not ignore certificate errors in production environments
4. **Disable Web SSH**: If remote terminal is not needed, it is recommended to disable it
5. **Update Regularly**: Keep the OpenSSL dependency up to date

## 📄 License

[MIT License](LICENSE)

## 🔗 Related Links

- [Komari Monitor Panel](https://github.com/komari-monitor/komari-monitor)
- [Go Version Agent](https://github.com/komari-monitor/komari-agent)
- [OpenWrt Official Website](https://openwrt.org/)
