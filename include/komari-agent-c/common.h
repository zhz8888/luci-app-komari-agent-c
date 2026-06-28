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

/* Common error codes */
#define KOMARI_OK          0
#define KOMARI_ERROR      -1
#define KOMARI_ERR_NO_MEM -2
#define KOMARI_ERR_IO     -3
#define KOMARI_ERR_NET    -4

#endif /* KOMARI_AGENT_C_COMMON_H */
