/*
 * Komari Agent common type definitions
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_COMMON_H
#define KOMARI_AGENT_C_COMMON_H

#include <stdint.h>
#include <stdbool.h>

/* Platform detection macro (Linux/OpenWrt only) */
#if defined(__OpenWrt__)
    #define PLATFORM_OPENWRT 1
    #define PLATFORM_NAME "OpenWrt"
    #define PLATFORM_LINUX 1
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
    #define PLATFORM_NAME "Linux"
#else
    #error "Unsupported platform: only Linux/OpenWrt is supported"
#endif

/* Unified error codes. See komari_errno.h for the full enum and the
 * komari_strerror helper. KOMARI_ERROR is kept as a readable alias for
 * legacy 0/-1 code paths that have not yet migrated to komari_errno_t. */
#include "komari_errno.h"
#define KOMARI_ERROR KOMARI_ERR_GENERIC

#endif /* KOMARI_AGENT_C_COMMON_H */
