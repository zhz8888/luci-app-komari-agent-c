# Contributing Guide

[中文](CONTRIBUTING.md) | **English**

Thank you for considering a contribution to Komari Agent (C language version)! This document describes the process and conventions for contributing.

## General Requirements

- Ensure local build and unit tests pass before submitting (see "Build and Test" below)
- All code comments must be in English
- Use clear variable names; handle platform compatibility for cross-platform code
- All memory allocations and file operations must check return values
- Do not reference issue numbers, PR numbers, or abstract problem codes in code comments; comments should explain "why", not "what"

## Git Commit Conventions

We follow the [Conventional Commits](https://www.conventionalcommits.org/) format:

```
<type>(<scope>): <subject>  # type/scope in English, subject in Chinese or English

<body>  # Chinese or English, bullet points describing specific changes

<footer>
```

> **Commit message language**: `type` and `scope` must be in English; `subject` and `body` may be written in either Chinese or English. Within a single commit, keep the language consistent, and align with the repository's existing commit history style.

### Commit types (type)

`feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`

### Scopes (scope)

`i18n`, `luci`, `openwrt`, `workflows`, `core`, `utils`, `deps`, `tests`, `ci`, `config`

### Examples

Chinese commit message example:

```
fix(core): 修复 WebSocket 重连时未清理 fragment buffer 的问题

- 在 ws_client_disconnect 中重置 fragment_len 与 fragment_capacity
- 修复后 v2 协议长连接断开重连不再出现解析失败
```

English commit message example:

```
docs(openwrt): add 4 missing options to init validate_section

- Add protocol_version, disable_compression, disable_auto_update, auto_discovery_key to validate_section
- Pass options without CLI flags via procd_set_param env in start_instance
```

## Code Style

- **Language standard**: C99
- **Comment language**: English, using Doxygen style (`@param`, `@return`)
- **Memory safety**: All `malloc`/`calloc`/`strdup` allocations must check return values; set pointers to NULL after free to avoid double-free
- **File operations**: All `fopen`/`fread`/`fwrite` calls must check return values
- **Cross-platform**: Linux-specific APIs (e.g., `/proc`, `forkpty`) must be guarded by platform detection macros
- **Hardening flags**: `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-Wformat -Werror=format-security`, `-fvisibility=hidden`, PIE enabled by default

## Build and Test

### Local Build

```bash
# Configure and build (default Release + tests)
cmake --preset default
cmake --build build

# Debug mode
cmake --preset debug
cmake --build build
```

### Run Unit Tests

```bash
cmake --preset default
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run Tests in Docker

Run tests without installing a local toolchain:

```bash
./scripts/docker-build.sh test
```

### Cross-Architecture Compilation

Docker-based cross-compilation for 8 CPU architectures:

```bash
./scripts/docker-build.sh amd64    # Single architecture
./scripts/docker-build.sh all      # All architectures
```

See [docker/README.md](docker/README.md) for details.

### CMake Presets

The project provides 9 standardized presets (see `CMakePresets.json`):

| Preset | Purpose |
|--------|---------|
| `default` | Default Release + tests |
| `debug` | Debug + verbose diagnostics |
| `release` | Release + LTO, no tests |
| `relwithdebinfo` | Optimized + debug symbols |
| `minsizerel` | Minimum size (embedded targets) |
| `sanitize` | ASan + UBSan |
| `coverage` | Code coverage |
| `openwrt` | OpenWrt cross-compile (requires SDK env) |
| `analyze` | clang-tidy static analysis |

## Testing Conventions

- Use the [Unity](https://github.com/ThrowTheSwitch/Unity) v2.5.2 framework for unit tests
- Test files go in the `tests/` directory, named `test_<module>.c`
- `KOMARI_BUILD_TESTS` and `BUILD_TESTING` CMake options are kept in sync (see `cmake/BuildOptions.cmake`)
- Attach corresponding unit tests when adding features or fixing bugs
- Ensure `ctest --test-dir build --output-on-failure` passes before submitting

## PR Process

1. Fork the repository and create a feature branch: `git checkout -b feat/your-feature`
2. Write code and commit messages following the conventions above
3. Ensure local build and unit tests pass
4. Submit a PR with a title following Conventional Commits format (e.g., `fix(core): 修复 xxx` or `fix(core): fix xxx`)
5. Describe the motivation, scope of impact, and test results in the PR description
6. Wait for CI checks (8-arch Docker binary build, 10-arch OpenWrt package build, LuCI package build, code quality checks)

## Project Structure

```
luci-app-komari-agent-c/
├── cmake/                  # Modular CMake configuration (5 modules)
├── docker/                 # Docker cross-compile environment
├── include/                # Public headers (version.h, etc.)
├── luci/                   # LuCI frontend (Lua + CBI)
├── openwrt/                # OpenWrt package definitions (Makefile + init/config)
├── scripts/                # Build/package/verify scripts
├── src/                    # C source code (organized by module)
├── tests/                  # Unity unit tests
├── .github/workflows/      # CI/CD configuration (ci.yml + release.yml)
├── CMakeLists.txt          # Top-level CMake configuration
├── CMakePresets.json       # 9 standardized build presets
└── CLAUDE.md               # Project maintenance guide (internal)
```

## Contact

- Submit Issues: [GitHub Issues](https://github.com/zhz8888/luci-app-komari-agent-c/issues)
- Submit PRs: [GitHub Pull Requests](https://github.com/zhz8888/luci-app-komari-agent-c/pulls)
