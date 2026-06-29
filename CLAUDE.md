# Komari Agent（C 语言版本）

## 项目概述

本项目是一个用 C 语言开发的系统监控代理，支持 Linux、OpenWrt 等多个平台。

## 技术栈

- **语言**：C (C99)
- **构建系统**：CMake
- **CI/CD**：GitHub Actions
- **目标平台**：Linux、OpenWrt、多架构支持
- **测试框架**：Unity v2.5.2
- **静态分析**：cppcheck
- **第三方库**：cJSON v1.7.19、OpenSSL、zlib

## 开发规范

### Git 提交规范

采用 Conventional Commits 格式：

```
<type>(<scope>): <subject>  # type/scope 英文，subject 中文

<body>  # 中文

<footer>
```

**提交类型**：`feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`build`、`ci`、`chore`、`revert`

**作用范围**：`i18n`、`luci`、`openwrt`、`workflows`、`core`、`utils`、`deps`、`tests`、`ci`、`config`

### 代码规范

- 所有代码注释必须使用英文
- 变量命名应清晰明了
- 跨平台代码需处理平台兼容性
- 使用 cppcheck 进行静态分析
- 所有内存分配必须检查返回值
- 文件操作必须检查返回值

### 测试规范

- 使用 Unity 框架编写单元测试
- 测试文件放置在 `tests/` 目录下
- 运行测试：`cmake -B build -DBUILD_TESTING=ON && cmake --build build && ctest --test-dir build --output-on-failure`

### 本地构建

```bash
# 配置并构建（默认 Release）
cmake -B build && cmake --build build

# 调试模式构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 构建产物位于 build/bin/komari-agent-c
```

### macOS 开发环境说明

#### 系统要求

- macOS 12.0 或更高版本
- Xcode Command Line Tools
- Homebrew 包管理器

#### 依赖安装

```bash
brew install cmake openssl zlib cppcheck
```

#### 构建流程说明

**重要**：由于 macOS 系统对相关编译器支持不够完善（OpenSSL/zlib 系统库路径配置复杂、部分 Linux 特有 API 不可用），为确保构建流程顺利进行，在 macOS 环境下默认跳过编译验证步骤。

具体跳过的步骤：
- `cmake --build build` - 实际编译步骤（可能因 OpenSSL 链接问题失败）

保留执行的步骤：
- `cmake -B build` - CMake 配置检查（验证 CMakeLists.txt 语法和依赖检测）
- `cppcheck --enable=warning,style,performance,portability -i src/vendor src/` - 静态分析

如需在 macOS 上尝试完整构建，需手动设置 OpenSSL 路径：

```bash
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
cmake -B build && cmake --build build
```

但此构建可能因 Linux 特有 API（如 forkpty、/proc 文件系统）而失败，仅用于语法检查。

#### macOS 上的推荐工作流

1. 使用 `cmake -B build` 验证 CMake 配置
2. 使用 `cppcheck` 进行静态代码分析
3. 使用 WSL（Windows Subsystem for Linux）或 GitHub Actions 进行完整构建验证

### Windows 开发环境说明

#### 系统要求

- Windows 10 版本 2004 或更高版本（内部版本 19041 或更高）
- WSL 2（Windows Subsystem for Linux）
- Linux 发行版（推荐 Ubuntu 20.04 LTS 或更高版本）

#### WSL 环境检查

**强制要求**：在 Windows 环境下进行任何编译或测试操作前，必须首先检查 WSL 是否已正确安装并配置。

检查步骤：
1. 运行 `wsl --list --verbose` 验证 WSL 已安装且为版本 2
2. 确认至少有一个 Linux 发行版已安装并可用
3. 验证 Linux 发行版状态为 "Running" 或 "Stopped"（非 "Installing" 或 "Uninstalling"）

#### 构建流程说明

**重要**：在 Windows 操作系统环境中进行开发时，必须使用 WSL（Windows Subsystem for Linux）对项目进行编译和测试。若检测到 WSL 未安装或未正确配置，必须立即终止所有正在进行的编译和测试流程，且不执行任何回退操作。

具体要求：
- 所有编译和测试命令必须在 WSL 环境中执行
- 不允许在 Windows 原生环境下执行任何构建或测试操作
- 若 WSL 环境检查失败，立即停止所有操作，不提供替代方案

WSL 环境下的标准构建流程：

```bash
# 在 WSL 中执行（假设项目路径已挂载到 /mnt/ 下）
cd /mnt/对应路径/luci-app-komari-agent-c

# 安装依赖（Ubuntu/Debian）
sudo apt update && sudo apt install -y cmake build-essential libssl-dev zlib1g-dev cppcheck

# 配置并构建
cmake -B build && cmake --build build

# 运行测试
cmake -B build -DBUILD_TESTING=ON && cmake --build build && ctest --test-dir build --output-on-failure
```

#### Windows 上的推荐工作流

1. 安装并配置 WSL 2（参考 [官方文档](https://docs.microsoft.com/zh-cn/windows/wsl/install)）
2. 在 WSL 中安装 Linux 发行版（推荐 Ubuntu 20.04 LTS 或更高版本）
3. 在 WSL 中安装必要的开发工具和依赖
4. 在 WSL 环境中执行所有编译和测试操作
5. 使用 GitHub Actions 进行跨平台构建验证

## 版本历史

### 版本记录规范

本章节集中记录所有版本信息，包括版本号、发布日期和主要变更点。后续版本更新时，必须在此章节追加新版本记录，确保版本信息的统一管理。

版本号由 git tags 决定，格式为 `vMAJOR.MINOR.PATCH`（遵循语义化版本规范 SemVer）。
