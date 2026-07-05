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

### OpenWrt 系统（推荐）

```bash
# OpenWrt >= 24.10（使用 APK）
apk add komari-agent-c-1.0.0-1-<arch>.apk

# OpenWrt < 24.10（使用 IPK）
opkg install komari-agent-c_1.0.0-1_<arch>.ipk
```

安装 LuCI 前端界面（可选）：

```bash
# OpenWrt >= 24.10
apk add luci-app-komari-agent-c-1.0.0-1-<arch>.apk

# OpenWrt < 24.10
opkg install luci-app-komari-agent-c_1.0.0-1_<arch>.ipk
```

### 其他 Linux 系统

```bash
# 下载并解压二进制文件
wget https://github.com/zhz8888/luci-app-komari-agent-c/releases/download/v1.0.0/komari-agent-c-1.0.0-linux-<arch>.tar.gz
tar -xzf komari-agent-c-1.0.0-linux-<arch>.tar.gz
sudo cp komari-agent-c /usr/local/bin/
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

- OpenWrt 21.02 或更高版本（CI 测试覆盖 24.10.x 和 25.12.x）
- 最低内存：64MB
- 必需依赖：`libpthread`、`libopenssl`、`librt`、`zlib`

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
