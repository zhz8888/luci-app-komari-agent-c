/*
 * Network latency ping task interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_PING_H
#define KOMARI_AGENT_C_PING_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define PING_TYPE_ICMP "icmp"
#define PING_TYPE_TCP  "tcp"
#define PING_TYPE_HTTP "http"

#define PING_DEFAULT_TIMEOUT_MS 3000
#define PING_HIGH_LATENCY_THRESHOLD_MS 1000
#define PING_HIGH_LATENCY_RETRIES 3

typedef struct {
    uint32_t task_id;
    char ping_type[16];
    char ping_target[256];
    int64_t latency_ms;
    int result;
    time_t finished_at;
} ping_task_result_t;

typedef struct {
    int timeout_ms;
    int high_latency_threshold_ms;
    int high_latency_retries;
    char custom_dns[128];
    /* When non-zero, skip TLS certificate verification for HTTPS ping
     * targets. Mirrors the ignore_cert semantics used by the WebSocket
     * and report HTTP paths. Default 0 (verify). */
    int ignore_cert;
} ping_task_config_t;

/**
 * Execute a ping task of the specified type, applying high-latency retries.
 *
 * @param target Ping target (IP, host or URL depending on type)
 * @param type Ping type: PING_TYPE_ICMP, PING_TYPE_TCP or PING_TYPE_HTTP
 * @param config Ping task configuration (may be NULL for defaults)
 * @param result Output ping task result
 * @return 0 on success, -1 on failure
 */
int ping_task_execute(const char *target, const char *type, ping_task_config_t *config, ping_task_result_t *result);

/**
 * Perform an ICMP echo request and measure the round-trip latency.
 *
 * @param target Target host (IP or domain)
 * @param timeout_ms Timeout in milliseconds
 * @param custom_dns Optional custom DNS server (may be NULL)
 * @return Latency in milliseconds on success, -1 on failure
 */
int ping_task_icmp(const char *target, int timeout_ms, const char *custom_dns);

/**
 * Perform a TCP connect probe and measure the round-trip latency.
 *
 * @param target Target host:port or host (defaults to port 80)
 * @param timeout_ms Timeout in milliseconds
 * @param custom_dns Optional custom DNS server (may be NULL)
 * @return Latency in milliseconds on success, -1 on failure
 */
int ping_task_tcp(const char *target, int timeout_ms, const char *custom_dns);

/**
 * Perform an HTTP GET probe and measure the round-trip latency.
 *
 * @param target Target URL or host (http/https scheme optional)
 * @param timeout_ms Timeout in milliseconds
 * @param custom_dns Optional custom DNS server (may be NULL)
 * @param ignore_cert When non-zero, skip TLS certificate verification
 * @return Latency in milliseconds on success, -1 on failure
 */
int ping_task_http(const char *target, int timeout_ms, const char *custom_dns, int ignore_cert);

/**
 * Resolve a target hostname to an IP address string.
 *
 * @param target Target hostname or IP literal
 * @param ip_out Output buffer for the resolved IP string
 * @param ip_size Size of ip_out buffer
 * @param custom_dns Optional custom DNS server (may be NULL)
 * @return 0 on success, -1 on failure
 */
int ping_resolve_ip(const char *target, char *ip_out, size_t ip_size, const char *custom_dns);

#endif
