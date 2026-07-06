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

> Replace `<version>` below with the latest release version (see [Releases page](https://github.com/zhz8888/luci-app-komari-agent-c/releases)), and `<arch>` with your target architecture.

### OpenWrt Systems (Recommended)

```bash
# OpenWrt >= 24.10 (using APK)
apk add komari-agent-c-<version>-<arch>.apk

# OpenWrt < 24.10 (using IPK)
opkg install komari-agent-c_<version>_<arch>.ipk
```

Install the LuCI web interface (optional):

```bash
# OpenWrt >= 24.10
apk add luci-app-komari-agent-c-<version>-<arch>.apk

# OpenWrt < 24.10
opkg install luci-app-komari-agent-c_<version>_<arch>.ipk
```

### Other Linux Systems

```bash
# Download and extract the binary
wget https://github.com/zhz8888/luci-app-komari-agent-c/releases/download/v<version>/komari-agent-c-<version>-linux-<arch>.tar.gz
tar -xzf komari-agent-c-<version>-linux-<arch>.tar.gz
sudo cp komari-agent-c /usr/local/bin/
```

## 🚀 Running

### Minimal Startup Example

```bash
komari-agent-c --token <TOKEN> --endpoint <URL>
```

- `<TOKEN>`: Authentication token obtained when adding a node in the Komari panel
- `<URL>`: WebSocket URL of the panel, e.g., `wss://panel.example.com/ws/client`

### Common Startup Options

| Option | Description |
|--------|-------------|
| `-t, --token <token>` | Authentication token (required) |
| `-e, --endpoint <url>` | Panel server URL (required) |
| `-i, --interval <seconds>` | Report interval in seconds, default 1.0 |
| `-d, --dns <server>` | Custom DNS server |
| `-c, --config <file>` | JSON configuration file path |
| `-k, --insecure` | Ignore TLS certificate errors (not recommended in production) |
| `-s, --disable-ssh` | Disable Web SSH remote terminal |
| `-v, --verbose` | Verbose log output |
| `-h, --help` | Show help message |

### systemd Integration Example (non-OpenWrt)

```ini
# /etc/systemd/system/komari-agent-c.service
[Unit]
Description=Komari Monitoring Agent
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/local/bin/komari-agent-c --token <TOKEN> --endpoint <URL>
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable --now komari-agent-c
```

### OpenWrt Startup

After installing the ipk/apk package, manage the service via `/etc/init.d/komari-agent-c`:

```bash
/etc/init.d/komari-agent-c enable   # Enable autostart
/etc/init.d/komari-agent-c start    # Start now
/etc/init.d/komari-agent-c restart  # Restart
/etc/init.d/komari-agent-c stop     # Stop
```

## ⚙️ Configuration

The agent supports four configuration sources, in order of precedence from lowest to highest:

1. **Defaults** (`config_init`)
2. **JSON config file** (path specified via `--config <file>`, or `AGENT_CONFIG_FILE` env var)
3. **OpenWrt UCI** (`/etc/config/komari-agent-c`, OpenWrt only)
4. **Environment variables** (`AGENT_*` series)
5. **Command-line arguments** (highest precedence, overrides all of the above)

### Configuration Reference

| Field | Type | Default | Env Variable | CLI Flag | Description |
|-------|------|---------|--------------|----------|-------------|
| `token` | string | empty | `AGENT_TOKEN` | `--token` | Authentication token (required) |
| `endpoint` | string | empty | `AGENT_ENDPOINT` | `--endpoint` | Panel server URL (required) |
| `interval` | float | `1.0` | `AGENT_INTERVAL` | `--interval` | Report interval (seconds) |
| `custom_dns` | string | empty | `AGENT_CUSTOM_DNS` | `--dns` | Custom DNS server |
| `ignore_unsafe_cert` | bool | `false` | `AGENT_IGNORE_UNSAFE_CERT` | `--insecure` | Ignore TLS certificate errors |
| `disable_web_ssh` | bool | `false` | `AGENT_DISABLE_WEB_SSH` | `--disable-ssh` | Disable Web SSH |
| `max_retries` | int | `5` | `AGENT_MAX_RETRIES` | - | Maximum reconnection attempts |
| `reconnect_interval` | int | `5` | `AGENT_RECONNECT_INTERVAL` | - | Reconnection interval (seconds) |
| `info_report_interval` | int | `30` | `AGENT_INFO_REPORT_INTERVAL` | - | System info report interval (seconds) |
| `month_rotate` | int | `0` | `AGENT_MONTH_ROTATE` | - | Traffic stat month rollover day (0=auto) |
| `protocol_version` | int | `2` | `AGENT_PROTOCOL_VERSION` | - | Protocol version (1 or 2) |
| `disable_auto_update` | bool | `false` | `AGENT_DISABLE_AUTO_UPDATE` | - | Disable auto-update check |
| `disable_compression` | bool | `false` | `AGENT_DISABLE_COMPRESSION` | - | Disable v2 protocol gzip compression |
| `enable_gpu` | bool | `false` | `AGENT_ENABLE_GPU` | - | Enable GPU monitoring |
| `include_nics` | string | empty | `AGENT_INCLUDE_NICS` | - | NICs to include (comma-separated) |
| `exclude_nics` | string | empty | `AGENT_EXCLUDE_NICS` | - | NICs to exclude (comma-separated) |
| `include_mountpoints` | string | empty | `AGENT_INCLUDE_MOUNTPOINTS` | - | Mount points to include (comma-separated) |
| `custom_ipv4` | string | empty | `AGENT_CUSTOM_IPV4` | - | Custom IPv4 address |
| `custom_ipv6` | string | empty | `AGENT_CUSTOM_IPV6` | - | Custom IPv6 address |
| `auto_discovery_key` | string | empty | `AGENT_AUTO_DISCOVERY_KEY` | - | Auto-discovery registration key |

> **Note**: Boolean environment variables accept `true`/`false`, `1`/`0`, `on`/`off`, etc.

### JSON Config File Example

Field names match the table above. Save as `/etc/komari/agent.json`:

```json
{
  "token": "your-token-here",
  "endpoint": "wss://panel.example.com/ws/client",
  "interval": 2.0,
  "max_retries": 10,
  "reconnect_interval": 10,
  "protocol_version": 2,
  "enable_gpu": true,
  "include_nics": "eth0,wlan0",
  "exclude_nics": "docker0"
}
```

Load via `--config` at startup:

```bash
komari-agent-c --config /etc/komari/agent.json
```

The file must have `600` permissions (owner read/write only); otherwise the agent refuses to load it:

```bash
chmod 600 /etc/komari/agent.json
```

### OpenWrt UCI Configuration

The UCI config file is located at `/etc/config/komari-agent-c`, with field names matching the table above:

```sh
config komari-agent-c 'komari-agent-c'
    option token 'your-token-here'
    option endpoint 'wss://panel.example.com/ws/client'
    option interval '1.0'
    option protocol_version '2'
    option enable_gpu '0'
```

Modify via the `uci` command:

```sh
uci set komari-agent-c.komari-agent-c.token='your-token'
uci set komari-agent-c.komari-agent-c.endpoint='wss://panel.example.com/ws/client'
uci commit komari-agent-c
/etc/init.d/komari-agent-c restart
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
cmake --preset relwithdebinfo # Optimized + debug symbols
cmake --preset minsizerel   # Minimum size (embedded targets)
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

- OpenWrt 24.10 or later (CI tested on 24.10.6 and 25.12.2)
- Minimum memory: 64MB
- Required dependencies: `libpthread`, `libopenssl`, `librt`, `zlib`

## 🔐 Security Recommendations

1. **Use HTTPS**: It is recommended to use HTTPS/WSS protocol for the endpoint
2. **Protect Token**: Do not leak the Token in public
3. **Certificate Verification**: Do not ignore certificate errors in production environments
4. **Disable Web SSH**: If remote terminal is not needed, it is recommended to disable it
5. **Update Regularly**: Keep the OpenSSL dependency up to date

## 🤝 Contributing

Issues and Pull Requests are welcome. Please read the [Contributing Guide](CONTRIBUTING.en.md) ([中文](CONTRIBUTING.md)) before submitting, to learn the Git commit conventions, code style, and testing requirements.

## 📄 License

[MIT License](LICENSE)

## 🔗 Related Links

- [Komari Monitor Panel](https://github.com/komari-monitor/komari-monitor)
- [Go Version Agent](https://github.com/komari-monitor/komari-agent)
- [OpenWrt Official Website](https://openwrt.org/)
