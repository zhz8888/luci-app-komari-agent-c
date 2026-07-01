# Docker 构建环境

**中文** | [English](README.en.md)

## 概述

本目录包含 **komari-agent-c** 的 Docker 构建环境。所有二进制编译均在 Docker 容器内执行，确保 8 种 CPU 架构的可复现构建，无需在宿主系统安装交叉编译工具链。

## 文件说明

| 文件 | 说明 |
|------|------|
| `Dockerfile.build` | 基于 Ubuntu 22.04 的构建环境镜像 |
| `docker-compose.yml` | 8 个架构构建服务 + 1 个测试服务的定义 |
| `build.sh` | 容器内执行的构建脚本 |
| `test.sh` | 容器内执行的单元测试脚本 |
| `README.md` | 本文档（中文） |
| `README.en.md` | 英文文档 |

## 快速开始

### 前提条件

- Docker Engine 20.10+
- Docker Compose v2+

### 构建单个架构

在**项目根目录**下执行：

```bash
# 构建 amd64（原生）
./scripts/docker-build.sh amd64

# 构建 arm64（交叉编译）
./scripts/docker-build.sh arm64
```

### 构建所有架构

```bash
./scripts/docker-build.sh all
```

### 运行单元测试

```bash
./scripts/docker-build.sh test
```

### 使用国内镜像（加速构建）

```bash
APT_MIRROR=cn ./scripts/docker-build.sh amd64
```

## 支持的架构

| 服务 | GOARCH | Debian 架构 | 编译器 | 说明 |
|------|--------|------------|--------|------|
| `build-amd64` | amd64 | amd64 | x86_64-linux-gnu-gcc | x86-64 64 位 |
| `build-arm64` | arm64 | arm64 | aarch64-linux-gnu-gcc | ARM 64 位 |
| `build-arm` | arm | armel | arm-linux-gnueabi-gcc | ARM 软浮点 |
| `build-armv7` | armv7 | armhf | arm-linux-gnueabihf-gcc | ARM 硬浮点 |
| `build-mipsel` | mipsel | mipsel | mipsel-linux-gnu-gcc | MIPS 小端 |
| `build-mips64` | mips64 | mips64el | mips64el-linux-gnuabi64-gcc | MIPS64 小端 |
| `build-riscv64` | riscv64 | riscv64 | riscv64-linux-gnu-gcc | RISC-V 64 位 |
| `build-386` | 386 | i386 | i686-linux-gnu-gcc | x86 32 位 |

## 构建输出

编译产物写入项目根目录的 `output/` 目录：

```
output/
├── komari-agent-c-linux-amd64
├── komari-agent-c-linux-arm64
├── komari-agent-c-linux-arm
└── ...
```

## 工作原理

1. **镜像构建**：每个服务构建一个 Docker 镜像，包含目标架构的交叉编译器和库（OpenSSL、zlib）
2. **源码挂载**：项目源代码以只读方式挂载到 `/src`
3. **外部构建**：CMake 在 `/tmp/komari-build`（可写临时目录）中进行构建
4. **二进制验证**：使用 `file` 命令验证输出二进制文件与目标架构匹配
5. **产物复制**：验证后的二进制文件复制到 `/output/`，映射到宿主机的 `output/`

## APT 镜像配置

`APT_MIRROR` 构建参数控制容器内使用的 apt 镜像源：

| 值 | 镜像 | 使用场景 |
|----|------|---------|
| `default` | Ubuntu 官方镜像 | CI（GitHub Actions） |
| `cn` | 清华大学镜像 | 中国本地构建 |

通过环境变量设置：

```bash
export APT_MIRROR=cn
./scripts/docker-build.sh amd64
```

或内联设置：

```bash
APT_MIRROR=cn ./scripts/docker-build.sh amd64
```

## 常见问题

### 出现 "Read-only file system" 错误

当前配置使用外部源码编译（`cmake -B /tmp/komari-build -S /src`），不应出现此错误。如仍遇到，请确认使用的是最新版 `build.sh`。

### 代码修改后 Docker 镜像未更新

Docker Compose 可能使用了缓存镜像。强制重建：

```bash
docker compose -f docker/docker-compose.yml build --no-cache build-amd64
```

### 使用 APT_MIRROR=cn 时出现 "Certificate verification failed"

基础镜像 `ubuntu:22.04` 不包含 `ca-certificates`。Dockerfile 使用 HTTP（而非 HTTPS）访问 apt 镜像以避免此问题。如修改 Dockerfile，请确保在安装 `ca-certificates` 之前使用 HTTP 协议。

### 二进制架构不匹配

构建脚本会自动使用 `file` 命令验证二进制架构。如出现 "Architecture mismatch"，可能是交叉编译器未正确安装。尝试重建镜像：

```bash
docker compose -f docker/docker-compose.yml build --no-cache build-arm64
```
