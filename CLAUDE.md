# Komari Agent（C 语言版本）

## 项目概述

本项目是一个用 C 语言开发的系统监控代理，支持 Linux、OpenWrt 等多个平台。

## 技术栈

- **语言**：C (C99)
- **构建系统**：CMake（模块化配置，见 `cmake/` 目录）
- **CI/CD**：GitHub Actions（`ci.yml` + `release.yml`）
- **目标平台**：Linux、OpenWrt、8 种 CPU 架构
- **前端界面**：LuCI（Lua + CBI 框架，支持中英文 i18n）
- **测试框架**：Unity v2.5.2
- **第三方库**：cJSON v1.7.19、OpenSSL、zlib
- **交叉编译**：Docker 化构建（Dockerfile.build + Dockerfile.legacy）

## 开发规范

### Git 提交规范

采用 Conventional Commits 格式：

```
<type>(<scope>): <subject>  # type/scope 英文，subject 中文或英文

<body>  # 中文或英文

<footer>
```

> **提交信息语言**：`type` 与 `scope` 必须使用英文；`subject` 与 `body` 可使用中文或英文，建议与同一提交内已使用的语言保持一致，且同一仓库的历史提交风格保持连贯。

**提交类型**：`feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`build`、`ci`、`chore`、`revert`

**作用范围**：`i18n`、`luci`、`openwrt`、`workflows`、`core`、`utils`、`deps`、`tests`、`ci`、`config`

### 代码规范

- 所有代码注释必须使用英文
- 变量命名应清晰明了
- 跨平台代码需处理平台兼容性
- 所有内存分配必须检查返回值
- 文件操作必须检查返回值

### 测试规范

- 使用 Unity 框架编写单元测试
- 测试文件放置在 `tests/` 目录下
- `KOMARI_BUILD_TESTS` 与 `BUILD_TESTING` 两个选项同步（见 `cmake/BuildOptions.cmake`）
- 运行测试：`cmake -B build -DKOMARI_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure`
- Docker 环境运行测试：`./scripts/docker-build.sh test`

### 本地构建

```bash
# 配置并构建（默认 Release）
cmake -B build && cmake --build build

# 调试模式构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 构建产物位于 build/bin/komari-agent-c
```

#### CMake 预设

项目提供 9 个标准化预设（见 `CMakePresets.json`）：

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

#### Docker 交叉编译

支持 8 种 CPU 架构的 Docker 化交叉编译，无需本地安装工具链：

```bash
./scripts/docker-build.sh amd64    # 构建单架构
./scripts/docker-build.sh all      # 构建所有架构
./scripts/docker-build.sh test     # 运行单元测试
```

详见 [docker/README.md](docker/README.md)。

### macOS 开发环境说明

#### 系统要求

- macOS 12.0 或更高版本
- Xcode Command Line Tools
- Homebrew 包管理器

#### 依赖安装

```bash
brew install cmake openssl zlib
```

#### 构建流程说明

**重要**：由于 macOS 系统对相关编译器支持不够完善（OpenSSL/zlib 系统库路径配置复杂、部分 Linux 特有 API 不可用），为确保构建流程顺利进行，在 macOS 环境下默认跳过编译验证步骤。

具体跳过的步骤：
- `cmake --build build` - 实际编译步骤（可能因 OpenSSL 链接问题失败）

保留执行的步骤：
- `cmake -B build` - CMake 配置检查（验证 CMakeLists.txt 语法和依赖检测）

如需在 macOS 上尝试完整构建，需手动设置 OpenSSL 路径：

```bash
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
cmake -B build && cmake --build build
```

但此构建可能因 Linux 特有 API（如 forkpty、/proc 文件系统）而失败，仅用于语法检查。

#### macOS 上的推荐工作流

1. 使用 `cmake -B build` 验证 CMake 配置
2. 使用 WSL（Windows Subsystem for Linux）或 GitHub Actions 进行完整构建验证

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
sudo apt update && sudo apt install -y cmake build-essential libssl-dev zlib1g-dev

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

## 项目目录结构

```
luci-app-komari-agent-c/
├── cmake/                  # CMake 模块化配置（5 个模块）
├── docker/                 # Docker 交叉编译环境
├── include/                # 公共头文件（version.h 等）
├── luci/                   # LuCI 前端（Lua + CBI）
├── openwrt/                # OpenWrt 包定义（Makefile + init/config 文件）
├── scripts/                # 构建/打包/验证脚本
├── src/                    # C 源代码（按模块组织）
├── tests/                  # Unity 单元测试
├── .github/workflows/      # CI/CD 配置（ci.yml + release.yml）
├── CMakeLists.txt          # 顶层 CMake 配置
├── CMakePresets.json       # 9 个标准化构建预设
└── CLAUDE.md               # 本文件
```

## CMake 模块化配置

`cmake/` 目录包含 5 个模块，按特定顺序加载：

1. **BuildOptions.cmake** — 构建选项定义（在 `project()` 之前加载）
2. **Version.cmake** — 从 `version.h` 解析版本号（在 `project()` 之前加载）
3. **Platform.cmake** — 编译器/平台检测（在 `project()` 之后加载）
4. **CompilerFlags.cmake** — 警告/安全/LTO 编译标志（在 Platform 之后加载）
5. **Dependencies.cmake** — OpenSSL/ZLIB/Threads 依赖检测（在 CompilerFlags 之后加载）

另外 `cmake/toolchain-openwrt.cmake` 是 OpenWrt 交叉编译工具链文件。

### 关键构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `KOMARI_BUILD_PROFILE` | `binary` | 构建 profile：`binary`（独立二进制）或 `openwrt`（OpenWrt 包） |
| `KOMARI_BUILD_TESTS` | `ON`/`OFF` | 构建单元测试（OpenWrt profile 默认 OFF） |
| `KOMARI_BUILD_SHARED` | `OFF` | 将 `komari_core` 构建为共享库而非静态库 |
| `KOMARI_ENABLE_CPACK` | `ON`/`OFF` | 启用 CPack 打包（OpenWrt profile 默认 OFF） |
| `KOMARI_ENABLE_LTO` | `OFF` | 链接时优化 |
| `KOMARI_ENABLE_SANITIZERS` | `OFF` | ASan + UBSan |
| `KOMARI_ENABLE_COVERAGE` | `OFF` | 代码覆盖率插桩（仅 gcc/clang） |
| `KOMARI_ENABLE_PIE` | `ON` | 位置无关可执行文件 |
| `KOMARI_WERROR` | `OFF` | 警告视为错误 |
| `KOMARI_VERBOSE_CMAKE` | `OFF` | 打印详细 CMake 配置诊断信息 |
| `KOMARI_ENABLE_CCACHE` | `ON` | 启用 ccache 加速重复构建 |
| `KOMARI_HARDEN_STACK` | `ON` | 启用 `-fstack-protector-strong` |
| `KOMARI_HARDEN_FORTIFY` | `ON` | 启用 `-D_FORTIFY_SOURCE=2` |
| `KOMARI_HARDEN_FORMAT` | `ON` | 启用 `-Wformat -Werror=format-security` |
| `KOMARI_HARDEN_VISIBILITY` | `ON` | 启用 `-fvisibility=hidden` 隐藏非导出符号 |

### 安全编译标志

Linux/OpenWrt 默认启用 5 项硬化选项：
- `-fstack-protector-strong`（栈溢出保护）
- `-D_FORTIFY_SOURCE=2`（运行时检测缓冲区溢出）
- `-Wformat -Werror=format-security`（格式化字符串安全）
- `-fvisibility=hidden`（符号可见性）
- PIE（位置无关可执行文件）

## CI/CD 配置

### CI（`.github/workflows/ci.yml`）

推送/PR 到 `main`/`develop` 分支时触发，包含 4 个 job：

1. **test-binary-build** — 8 架构 Docker 二进制构建矩阵（amd64/arm64/arm/armv7/mipsel/mips64/riscv64/386）
2. **test-openwrt-build** — 10 个 OpenWrt 架构 × 2 个版本（24.10.6、25.12.2）矩阵
3. **test-luci-build** — LuCI 包构建测试（24.10.6、25.12.2）
4. **lint** — 代码质量检查（codespell、shell 语法、YAML 校验、verify_ci_config.py、Docker 单元测试）

### Release（`.github/workflows/release.yml`）

推送 `v*` tag 时触发，包含 4 个 job：

1. **build-openwrt** — 构建 OpenWrt IPK/APK 包（10 架构 × 2 版本）
2. **build-binaries** — 构建独立二进制（8 架构 Docker）
3. **build-luci** — 构建 LuCI 包（2 版本）
4. **release** — 汇总产物并创建 GitHub Release

## LuCI 前端

LuCI 前端位于 `luci/` 目录，提供 Web 配置界面：

- **3 个标签页**：配置（CBI 表单）、状态（实时仪表板）、日志（日志查看器）
- **7 个后端 JSON API**：status、start、stop、restart、test_connection、log、clear_log
- **i18n 支持**：中文（zh_Hans）翻译，`.po` → `.lmo` 编译
- **ACL 权限**：通过 `luci-app-komari-agent-c` ACL 定义控制访问
- **包元数据**：使用 `luci.mk` 标准流程，`LUCI_PKGARCH:=all`

### LuCI 版本同步

`luci/Makefile` 的 `PKG_VERSION` 必须与主项目版本保持同步。

## 多架构支持

支持 8 种 CPU 架构的交叉编译：

| GOARCH | Debian 架构 | 编译器 | 基础镜像 |
|--------|------------|--------|---------|
| amd64 | amd64 | gcc | Ubuntu 24.04 |
| arm64 | arm64 | aarch64-linux-gnu-gcc | Ubuntu 24.04 |
| arm | armel | arm-linux-gnueabi-gcc | Debian 12 (bookworm-slim) |
| armv7 | armhf | arm-linux-gnueabihf-gcc | Ubuntu 24.04 |
| mipsel | mipsel | mipsel-linux-gnu-gcc | Debian 12 (bookworm-slim) |
| mips64 | mips64el | mips64el-linux-gnuabi64-gcc | Debian 12 (bookworm-slim) |
| riscv64 | riscv64 | riscv64-linux-gnu-gcc | Ubuntu 24.04 |
| 386 | i386 | i686-linux-gnu-gcc | Ubuntu 24.04 |

armel/mipsel/mips64el 使用 Debian 12 (bookworm-slim)，因为这些架构在 Ubuntu 24.04 ports 仓库中缺少 `libc6-dev-*-cross` 包。
