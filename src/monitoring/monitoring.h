/*
 * System monitoring data collection interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_MONITORING_H
#define KOMARI_AGENT_C_MONITORING_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

typedef struct {
    char cpu_name[128];
    char cpu_arch[32];
    int cpu_cores;
    double cpu_usage;
} cpu_info_t;

typedef struct {
    uint64_t total;
    uint64_t used;
    uint64_t free;
    uint64_t available;
    uint64_t buffers;
    uint64_t cached;
} mem_info_t;

typedef struct {
    uint64_t total;
    uint64_t used;
    uint64_t free;
} disk_info_t;

typedef struct {
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_speed;
    uint64_t tx_speed;
} net_info_t;

typedef struct {
    double load1;
    double load5;
    double load15;
} load_info_t;

typedef struct {
    int tcp_count;
    int udp_count;
} conn_info_t;

typedef struct {
    char os_name[128];
    char kernel_version[64];
    char arch[32];
    char hostname[64];
    uint64_t uptime;
} system_info_t;

/**
 * Get CPU information including name, architecture, cores and usage.
 *
 * @param info Output CPU info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_cpu_info(cpu_info_t *info);

/**
 * Get memory information (total, used, free, available, buffers, cached).
 *
 * @param info Output memory info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_mem_info(mem_info_t *info);

/**
 * Get swap space information.
 *
 * @param info Output memory info structure (reused for swap)
 * @return 0 on success, -1 on failure
 */
int monitoring_get_swap_info(mem_info_t *info);

/**
 * Get memory and swap information in a single /proc/meminfo read.
 *
 * report_generate calls monitoring_get_mem_info and monitoring_get_swap_info
 * back-to-back every cycle; each opened /proc/meminfo independently. This
 * combined interface parses the file once and populates both structures,
 * halving the per-cycle fopen/parse cost.
 *
 * @param mem  Output memory info structure (may be NULL to skip memory)
 * @param swap Output swap info structure (may be NULL to skip swap)
 * @return 0 on success, -1 on failure
 */
int monitoring_get_mem_swap_info(mem_info_t *mem, mem_info_t *swap);

/**
 * Get disk usage information aggregated across mounted filesystems.
 *
 * @param info Output disk info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_disk_info(disk_info_t *info);

/**
 * Get network traffic statistics and current speed.
 *
 * @param info Output network info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_net_info(net_info_t *info);

/**
 * Get system load averages (1, 5 and 15 minutes).
 *
 * @param info Output load info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_load_info(load_info_t *info);

/**
 * Get TCP and UDP connection counts.
 *
 * @param info Output connection info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_conn_info(conn_info_t *info);

/**
 * Get system information (OS, kernel, architecture, hostname, uptime).
 *
 * @param info Output system info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_system_info(system_info_t *info);

/**
 * Get system uptime in seconds.
 *
 * @return Uptime in seconds, 0 on failure
 */
uint64_t monitoring_get_uptime(void);

/**
 * Count the number of running processes by scanning /proc.
 *
 * @return Process count, 0 on failure
 */
int monitoring_get_process_count(void);

/**
 * Get the primary IPv4 and IPv6 addresses of the system.
 *
 * @param ipv4 Output buffer for IPv4 address
 * @param ipv4_len Size of ipv4 buffer
 * @param ipv6 Output buffer for IPv6 address
 * @param ipv6_len Size of ipv6 buffer
 * @return 0 on success, -1 on failure
 */
int monitoring_get_ip_address(char *ipv4, size_t ipv4_len,
                               char *ipv6, size_t ipv6_len);

/**
 * Force a network speed sample update by querying current net info.
 */
void monitoring_net_speed_update(void);

/**
 * Set global config pointer, used by memory calculation and other modules to read config items.
 *
 * @param config Pointer to the agent configuration
 */
void monitoring_set_config(const agent_config_t *config);

#endif
