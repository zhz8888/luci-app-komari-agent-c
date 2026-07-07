/*
 * Centralized filesystem paths used by the Linux monitoring, virtualization
 * detection and GPU enumeration modules.
 *
 * All paths in this header are Linux-specific (procfs, sysfs). They are
 * collected here so that a future port to another platform can replace this
 * single header plus the reading code, instead of hunting scattered string
 * literals across the source tree. When a BSD or macOS port is attempted,
 * provide an equivalent paths.h (or per-platform implementation directory)
 * that maps these names to sysctl/IOKit sources.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_PLATFORM_PATHS_H
#define KOMARI_AGENT_C_PLATFORM_PATHS_H

/* ---- procfs: system monitoring sources ---- */
#define KOMARI_PATH_PROC           "/proc"
#define KOMARI_PATH_PROC_CPUINFO   "/proc/cpuinfo"
#define KOMARI_PATH_PROC_STAT      "/proc/stat"
#define KOMARI_PATH_PROC_MEMINFO   "/proc/meminfo"
#define KOMARI_PATH_PROC_MOUNTS    "/proc/mounts"
#define KOMARI_PATH_PROC_LOADAVG   "/proc/loadavg"
#define KOMARI_PATH_PROC_UPTIME    "/proc/uptime"

/* ---- procfs: network state ---- */
#define KOMARI_PATH_PROC_NET_DEV   "/proc/net/dev"
#define KOMARI_PATH_PROC_NET_TCP   "/proc/net/tcp"
#define KOMARI_PATH_PROC_NET_TCP6  "/proc/net/tcp6"
#define KOMARI_PATH_PROC_NET_UDP   "/proc/net/udp"
#define KOMARI_PATH_PROC_NET_UDP6  "/proc/net/udp6"

/* ---- procfs: process/container detection ---- */
#define KOMARI_PATH_PROC_SELF_CGROUP "/proc/1/cgroup"
#define KOMARI_PATH_PROC_VZ_VEINFO   "/proc/vz/veinfo"

/* ---- sysfs: GPU / DRM enumeration ---- */
#define KOMARI_PATH_SYS_DRM "/sys/class/drm"

/* ---- sysfs: DMI product name (virtualization detection) ---- */
#define KOMARI_PATH_SYS_DMI_PRODUCT_NAME "/sys/class/dmi/id/product_name"

/* ---- Container/Virtualization markers ---- */
#define KOMARI_PATH_DOCKER_ENV       "/.dockerenv"
#define KOMARI_PATH_CONTAINER_ENV    "/run/.containerenv"

#endif /* KOMARI_AGENT_C_PLATFORM_PATHS_H */
