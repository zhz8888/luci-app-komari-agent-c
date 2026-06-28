# Komari Agent（C/OpenWrt 版本）

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![OpenWrt](https://img.shields.io/badge/platform-OpenWrt-orange.svg)](https://openwrt.org/)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Build Status](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/build.yml/badge.svg)](https://github.com/zhz8888/luci-app-komari-agent-c/actions/workflows/build.yml)
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

## 📦 快速安装

### OpenWrt 系统（推荐）

```bash
# OpenWrt > 24.10（使用 APK）
apk add komari-agent-1.0.1-1-<arch>.apk

# OpenWrt <= 24.10（使用 IPK）
opkg install komari-agent-c_1.0.1-1_<arch>.ipk
```

### 其他 Linux 系统

```bash
# 下载并解压二进制文件
wget https://github.com/zhz8888/luci-app-komari-agent-c/releases/download/v1.0.1/komari-agent-1.0.1-linux-<arch>.tar.gz
tar -xzf komari-agent-1.0.1-linux-<arch>.tar.gz
sudo cp komari-agent-c /usr/local/bin/
```

## 📚 详细文档

完整的使用说明、配置指南和构建文档已移至 [docs](docs/) 目录：

| 文档 | 说明 |
|------|------|
| [OpenWrt 软件包使用指南](docs/openwrt-package.md) | IPK/APK 软件包安装、配置和管理指南 |
| [二进制文件使用指南](docs/binary-usage.md) | 独立二进制文件的下载、安装和使用说明 |
| [构建与调试指南](docs/build-debug.md) | 本地编译、交叉编译和调试的完整指南 |
| [配置指南](docs/configuration.md) | UCI 配置、环境变量和命令行参数的详细说明 |
| [功能说明](docs/features.md) | 监控数据、Ping 任务、虚拟化检测等功能的详细说明 |

## 📋 系统要求

- OpenWrt 21.02 或更高版本（推荐 23.05 或 24.10）
- 最低内存：64MB
- 必需依赖：`libpthread`、`libopenssl`、`libm`

## 🔐 安全建议

1. **使用 HTTPS**：建议 endpoint 使用 HTTPS/WSS 协议
2. **保护 Token**：不要在公开场合泄露 Token
3. **证书校验**：生产环境不要忽略证书错误
4. **禁用 Web SSH**：如不需要远程终端，建议禁用
5. **定期更新**：保持 OpenSSL 依赖为最新版本

## 📄 许可证

[MIT 许可证](LICENSE)

## 🔗 相关链接

- [Komari Monitor 面板](https://github.com/komari-monitor/komari-monitor)
- [Go 版本 Agent](https://github.com/komari-monitor/komari-agent)
- [OpenWrt 官网](https://openwrt.org/)
