# Komari Agent (C/OpenWrt Version)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![OpenWrt](https://img.shields.io/badge/platform-OpenWrt-orange.svg)](https://openwrt.org/)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Build Status](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/build.yml/badge.svg)](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/build.yml)
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

## 📦 Quick Installation

### OpenWrt Systems (Recommended)

```bash
# OpenWrt > 24.10 (using APK)
apk add komari-agent-1.0.1-1-<arch>.apk

# OpenWrt <= 24.10 (using IPK)
opkg install komari-agent-c_1.0.1-1_<arch>.ipk
```

### Other Linux Systems

```bash
# Download and extract the binary
wget https://github.com/zhz8888/luci-app-komari-agent-c/releases/download/v1.0.1/komari-agent-1.0.1-linux-<arch>.tar.gz
tar -xzf komari-agent-1.0.1-linux-<arch>.tar.gz
sudo cp komari-agent-c /usr/local/bin/
```

## 📚 Detailed Documentation

The complete usage instructions, configuration guide, and build documentation have been moved to the [docs](docs/) directory:

| Document | Description |
|----------|-------------|
| [OpenWrt Package Usage Guide](docs/openwrt-package.md) | IPK/APK package installation, configuration, and management guide |
| [Binary File Usage Guide](docs/binary-usage.md) | Download, installation, and usage instructions for standalone binaries |
| [Build and Debug Guide](docs/build-debug.md) | Complete guide for local compilation, cross-compilation, and debugging |
| [Configuration Guide](docs/configuration.md) | Detailed explanation of UCI configuration, environment variables, and command-line arguments |
| [Features Guide](docs/features.md) | Detailed explanation of monitoring data, Ping tasks, virtualization detection, and other features |

## 📋 System Requirements

- OpenWrt 21.02 or later (23.05 or 24.10 recommended)
- Minimum memory: 64MB
- Required dependencies: `libpthread`, `libopenssl`, `libm`

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
