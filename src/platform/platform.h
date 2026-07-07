/*
 * Platform abstraction boundary for komari-agent-c.
 *
 * The agent currently targets Linux and OpenWrt only. Several modules read
 * from procfs (/proc/*) and sysfs (/sys/*); a few call BSD-derived libc
 * extensions such as forkpty. Rather than letting those Linux specifics
 * spread across the business modules, this header documents the boundary
 * and centralizes the compile-time platform detection that downstream
 * modules can rely on.
 *
 * Porting strategy:
 *   1. Filesystem paths are collected in paths.h. A non-Linux port replaces
 *      that header (or the functions that read it) with platform-equivalent
 *      sources (e.g. sysctl on BSD, IOKit on macOS).
 *   2. Compile-time platform macros (PLATFORM_LINUX / PLATFORM_OPENWRT) are
 *      re-exported from include/komari-agent-c/common.h; modules that need
 *      platform-specific code should branch on those macros rather than
 *      __linux__ directly so the OpenWrt variant is covered consistently.
 *   3. libc-extension calls such as forkpty are linked via CMake feature
 *      detection (see cmake/Dependencies.cmake) and must be isolated to the
 *      terminal module; do not introduce new direct dependencies on
 *      BSD-only headers from other modules.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_PLATFORM_H
#define KOMARI_AGENT_C_PLATFORM_H

#include "paths.h"

#endif /* KOMARI_AGENT_C_PLATFORM_H */
