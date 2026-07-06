# Komari Agent（C/OpenWrt 版本）

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![OpenWrt](https://img.shields.io/badge/platform-OpenWrt-orange.svg)](https://openwrt.org/)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![CI](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/ci.yml/badge.svg)](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/zhz8888/luci-app-komari-agent-c)](https://github.com/zhz8888/luci-app-komari-agent-c/releases)

[English](README.en.md) | **中文**

一个为 OpenWrt 路由器设计的轻量级监控代理。这是 [komari-monitor](https://github.com/komari-monitor/komari-agent) 的 C 语言重写版本。

## ✨ 核心特性

- 🚀 **轻量高效**：二进制文件仅 ~100KB，内存占用 < 3MB
- 📊 **全面监控**：CPU、内存、磁盘、网络、进程、连接数
- 🔒 **TLS 加密**：支持 wss:// 协议，安全传输监控数据
- 🔌 **WebSocket 通信**：实时数据上报，符合 RFC 6455 标准
- 💻 **Web SSH**：支持远程终端访问
- 📈 **流量统计**：按月统计网卡流量
- 🏓 **Ping 任务**：支持 ICMP、TCP、HTTP 三种模式
- 🔄 **自动重连**：网络断开后自动恢复连接
- 🖥️ **LuCI 前端**：提供 Web 配置界面，支持配置管理、实时状态监控、日志查看和连接测试

## 📦 快速安装

> 将下方 `<version>` 替换为最新 release 版本号（见 [Releases 页面](https://github.com/zhz8888/luci-app-komari-agent-c/releases)），`<arch>` 替换为目标架构。

### OpenWrt 系统（推荐）

```bash
# OpenWrt >= 24.10（使用 APK）
apk add komari-agent-c-<version>-<arch>.apk

# OpenWrt < 24.10（使用 IPK）
opkg install komari-agent-c_<version>_<arch>.ipk
```

安装 LuCI 前端界面（可选）：

```bash
# OpenWrt >= 24.10
apk add luci-app-komari-agent-c-<version>-<arch>.apk

# OpenWrt < 24.10
opkg install luci-app-komari-agent-c_<version>_<arch>.ipk
```

### 其他 Linux 系统

```bash
# 下载并解压二进制文件
wget https://github.com/zhz8888/luci-app-komari-agent-c/releases/download/v<version>/komari-agent-c-<version>-linux-<arch>.tar.gz
tar -xzf komari-agent-c-<version>-linux-<arch>.tar.gz
sudo cp komari-agent-c /usr/local/bin/
```

## 🚀 运行

### 最小启动示例

```bash
komari-agent-c --token <TOKEN> --endpoint <URL>
```

- `<TOKEN>`：在 Komari 面板添加节点时获取的认证 Token
- `<URL>`：面板的 WebSocket 地址，例如 `wss://panel.example.com/ws/client`

### 常用启动参数

| 参数 | 说明 |
|------|------|
| `-t, --token <token>` | 认证 Token（必填） |
| `-e, --endpoint <url>` | 面板服务器 URL（必填） |
| `-i, --interval <seconds>` | 上报间隔秒数，默认 1.0 |
| `-d, --dns <server>` | 自定义 DNS 服务器 |
| `-c, --config <file>` | JSON 配置文件路径 |
| `-k, --insecure` | 忽略 TLS 证书错误（不推荐在生产环境使用） |
| `-s, --disable-ssh` | 禁用 Web SSH 远程终端 |
| `-v, --verbose` | 详细日志输出 |
| `-h, --help` | 显示帮助信息 |

### systemd 集成示例（非 OpenWrt 系统）

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

启用并启动：

```bash
sudo systemctl enable --now komari-agent-c
```

### OpenWrt 启动

OpenWrt 安装 ipk/apk 后通过 `/etc/init.d/komari-agent-c` 管理：

```bash
/etc/init.d/komari-agent-c enable   # 开机自启
/etc/init.d/komari-agent-c start    # 立即启动
/etc/init.d/komari-agent-c restart  # 重启
/etc/init.d/komari-agent-c stop     # 停止
```

## ⚙️ 配置说明

Agent 支持四种配置途径，优先级从低到高依次为：

1. **默认值**（`config_init`）
2. **JSON 配置文件**（`--config <file>` 指定路径，或 `AGENT_CONFIG_FILE` 环境变量）
3. **OpenWrt UCI**（`/etc/config/komari-agent-c`，仅 OpenWrt 环境）
4. **环境变量**（`AGENT_*` 系列）
5. **命令行参数**（优先级最高，覆盖以上所有来源）

### 配置项一览

| 字段 | 类型 | 默认值 | 环境变量 | CLI 参数 | 说明 |
|------|------|--------|----------|----------|------|
| `token` | string | 空 | `AGENT_TOKEN` | `--token` | 认证 Token（必填） |
| `endpoint` | string | 空 | `AGENT_ENDPOINT` | `--endpoint` | 面板服务器 URL（必填） |
| `interval` | float | `1.0` | `AGENT_INTERVAL` | `--interval` | 上报间隔（秒） |
| `custom_dns` | string | 空 | `AGENT_CUSTOM_DNS` | `--dns` | 自定义 DNS 服务器 |
| `ignore_unsafe_cert` | bool | `false` | `AGENT_IGNORE_UNSAFE_CERT` | `--insecure` | 忽略 TLS 证书错误 |
| `disable_web_ssh` | bool | `false` | `AGENT_DISABLE_WEB_SSH` | `--disable-ssh` | 禁用 Web SSH |
| `max_retries` | int | `5` | `AGENT_MAX_RETRIES` | - | 最大重连次数 |
| `reconnect_interval` | int | `5` | `AGENT_RECONNECT_INTERVAL` | - | 重连间隔（秒） |
| `info_report_interval` | int | `30` | `AGENT_INFO_REPORT_INTERVAL` | - | 系统信息上报间隔（秒） |
| `month_rotate` | int | `0` | `AGENT_MONTH_ROTATE` | - | 流量统计月份切换日（0=自动） |
| `protocol_version` | int | `2` | `AGENT_PROTOCOL_VERSION` | - | 协议版本（1 或 2） |
| `disable_auto_update` | bool | `false` | `AGENT_DISABLE_AUTO_UPDATE` | - | 禁用自动更新检查 |
| `disable_compression` | bool | `false` | `AGENT_DISABLE_COMPRESSION` | - | 禁用 v2 协议 gzip 压缩 |
| `enable_gpu` | bool | `false` | `AGENT_ENABLE_GPU` | - | 启用 GPU 监控 |
| `include_nics` | string | 空 | `AGENT_INCLUDE_NICS` | - | 包含的网卡（逗号分隔） |
| `exclude_nics` | string | 空 | `AGENT_EXCLUDE_NICS` | - | 排除的网卡（逗号分隔） |
| `include_mountpoints` | string | 空 | `AGENT_INCLUDE_MOUNTPOINTS` | - | 包含的挂载点（逗号分隔） |
| `custom_ipv4` | string | 空 | `AGENT_CUSTOM_IPV4` | - | 自定义 IPv4 地址 |
| `custom_ipv6` | string | 空 | `AGENT_CUSTOM_IPV6` | - | 自定义 IPv6 地址 |
| `auto_discovery_key` | string | 空 | `AGENT_AUTO_DISCOVERY_KEY` | - | 自动发现注册密钥 |

> **注**：布尔类型环境变量接受 `true`/`false`、`1`/`0`、`on`/`off` 等常见格式。

### JSON 配置文件示例

字段名与上表一致，保存为 `/etc/komari/agent.json`：

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

启动时通过 `--config` 指定：

```bash
komari-agent-c --config /etc/komari/agent.json
```

文件权限应为 `600`（仅所有者可读写），否则 agent 会拒绝加载：

```bash
chmod 600 /etc/komari/agent.json
```

### OpenWrt UCI 配置

UCI 配置文件位于 `/etc/config/komari-agent-c`，字段名与上表一致：

```sh
config komari-agent-c 'komari-agent-c'
    option token 'your-token-here'
    option endpoint 'wss://panel.example.com/ws/client'
    option interval '1.0'
    option protocol_version '2'
    option enable_gpu '0'
```

通过 `uci` 命令修改：

```sh
uci set komari-agent-c.komari-agent-c.token='your-token'
uci set komari-agent-c.komari-agent-c.endpoint='wss://panel.example.com/ws/client'
uci commit komari-agent-c
/etc/init.d/komari-agent-c restart
```

## 🛠️ 本地构建

### 标准构建（CMake）

```bash
# 配置并构建（默认 Release）
cmake -B build && cmake --build build

# 调试模式构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 构建产物位于 build/bin/komari-agent-c
```

### CMake 预设

项目提供 9 个标准化预设（见 `CMakePresets.json`），简化常见构建场景：

```bash
cmake --preset default      # 默认 Release + 测试
cmake --preset debug        # Debug + 详细诊断
cmake --preset release      # Release + LTO，无测试
cmake --preset relwithdebinfo # 优化 + 调试符号
cmake --preset minsizerel   # 最小体积（嵌入式目标）
cmake --preset sanitize     # ASan + UBSan
cmake --preset coverage     # 代码覆盖率
cmake --preset openwrt      # OpenWrt 交叉编译（需 SDK 环境）
cmake --preset analyze      # clang-tidy 静态分析
```

### Docker 交叉编译

支持 8 种 CPU 架构的 Docker 化交叉编译，无需本地安装工具链：

```bash
# 构建单架构
./scripts/docker-build.sh amd64

# 构建所有架构
./scripts/docker-build.sh all

# 运行单元测试
./scripts/docker-build.sh test
```

详见 [docker/README.md](docker/README.md)。

### 运行测试

```bash
cmake -B build -DBUILD_TESTING=ON && cmake --build build
ctest --test-dir build --output-on-failure
```

## 📋 系统要求

- OpenWrt 24.10 或更高版本（CI 测试覆盖 24.10.6 与 25.12.2）
- 最低内存：64MB
- 必需依赖：`libpthread`、`libopenssl`、`librt`、`zlib`

## 🔐 安全建议

1. **使用 HTTPS**：建议 endpoint 使用 HTTPS/WSS 协议
2. **保护 Token**：不要在公开场合泄露 Token
3. **证书校验**：生产环境不要忽略证书错误
4. **禁用 Web SSH**：如不需要远程终端，建议禁用
5. **定期更新**：保持 OpenSSL 依赖为最新版本

## 🤝 贡献

欢迎提交 Issue 和 Pull Request。提交前请阅读 [贡献指南](CONTRIBUTING.md)（[English](CONTRIBUTING.en.md)），了解 Git 提交规范、代码规范与测试要求。

## 📄 许可证

[MIT 许可证](LICENSE)

## 🔗 相关链接

- [Komari Monitor 面板](https://github.com/komari-monitor/komari-monitor)
- [Go 版本 Agent](https://github.com/komari-monitor/komari-agent)
- [OpenWrt 官网](https://openwrt.org/)
