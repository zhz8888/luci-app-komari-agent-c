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
#include <pthread.h>

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
 * @param memory_include_cache Whether to include cache/buffers in "used" total
 * @param info Output memory info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_mem_info(bool memory_include_cache, mem_info_t *info);

/**
 * Get swap space information.
 *
 * @param memory_include_cache Whether to include cache/buffers in "used" total
 *                             (currently only affects memory, not swap, but
 *                             kept for signature symmetry with mem_info)
 * @param info Output memory info structure (reused for swap)
 * @return 0 on success, -1 on failure
 */
int monitoring_get_swap_info(bool memory_include_cache, mem_info_t *info);

/**
 * Get memory and swap information in a single /proc/meminfo read.
 *
 * report_generate calls monitoring_get_mem_info and monitoring_get_swap_info
 * back-to-back every cycle; each opened /proc/meminfo independently. This
 * combined interface parses the file once and populates both structures,
 * halving the per-cycle fopen/parse cost.
 *
 * @param memory_include_cache Whether to include cache/buffers in "used" total
 * @param mem  Output memory info structure (may be NULL to skip memory)
 * @param swap Output swap info structure (may be NULL to skip swap)
 * @return 0 on success, -1 on failure
 */
int monitoring_get_mem_swap_info(bool memory_include_cache, mem_info_t *mem, mem_info_t *swap);

/**
 * Get disk usage information aggregated across mounted filesystems.
 *
 * @param info Output disk info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_disk_info(disk_info_t *info);

/**
 * Per-caller network rate calculation state.
 *
 * monitoring_get_net_info computes rx_speed/tx_speed by comparing the current
 * byte counters against the previous call's snapshot. Previously this state
 * was stored in file-level globals, making the module non-reentrant and hard
 * to test in isolation. Callers now own and pass an instance of this struct
 * so each calling context (e.g. the report thread) has its own independent
 * rate tracker.
 *
 * Initialize with monitoring_net_state_init() before the first call. The
 * struct contains a mutex so it is safe to share between threads that access
 * the same network data source.
 */
typedef struct {
    uint64_t last_rx;       /* Total RX bytes at the previous sample */
    uint64_t last_tx;       /* Total TX bytes at the previous sample */
    uint64_t last_time;     /* Timestamp (ms) of the previous sample */
    bool has_prev_sample;   /* Whether a previous sample exists */
    bool mutex_inited;      /* Whether the mutex has been initialized */
    pthread_mutex_t mutex;  /* Protects the fields above */
} monitoring_net_state_t;

/**
 * Initialize a monitoring_net_state_t instance.
 *
 * Must be called once before passing the struct to monitoring_get_net_info.
 * Returns 0 on success, -1 on mutex init failure.
 *
 * @param state State to initialize (must not be NULL)
 * @return 0 on success, -1 on failure
 */
int monitoring_net_state_init(monitoring_net_state_t *state);

/**
 * Destroy resources owned by a monitoring_net_state_t instance.
 *
 * Call when the state is no longer needed to destroy the internal mutex.
 * Safe to call on a zero-initialized struct that was never passed to
 * monitoring_net_state_init.
 *
 * @param state State to clean up (may be NULL)
 */
void monitoring_net_state_cleanup(monitoring_net_state_t *state);

/**
 * Get network traffic statistics and current speed.
 *
 * @param state Caller-owned rate calculation state. Pass NULL to skip speed
 *              calculation (rx_speed and tx_speed will be 0). When non-NULL,
 *              the state is updated under its internal mutex so the next call
 *              can compute the rate delta.
 * @param info  Output network info structure
 * @return 0 on success, -1 on failure
 */
int monitoring_get_net_info(monitoring_net_state_t *state, net_info_t *info);

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
 *
 * @param state Caller-owned rate calculation state (may be NULL)
 */
void monitoring_net_speed_update(monitoring_net_state_t *state);

#endif
