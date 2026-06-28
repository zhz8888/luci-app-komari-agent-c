/*
 * DNS resolver with optional custom DNS server support.
 * Falls back to the system resolver (getaddrinfo) when no custom server is set
 * or the custom server is unreachable.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_DNS_RESOLVER_H
#define KOMARI_AGENT_C_DNS_RESOLVER_H

#include <stddef.h>

#define DNS_RESOLVER_MAX_SERVERS 4
#define DNS_RESOLVER_MAX_ADDR_LEN 64

/**
 * Initialize the DNS resolver.
 *
 * @param custom_dns Custom DNS server address (IPv4 or IPv6). Pass NULL or an
 *                   empty string to use the system default resolver.
 * @return 0 on success, -1 on failure.
 */
int dns_resolver_init(const char *custom_dns);

/**
 * Resolve a hostname to an IP address string.
 * If `hostname` is already a valid IP address literal, it is copied to ip_out as-is.
 *
 * @param hostname    Hostname (or IP literal) to resolve.
 * @param ip_out      Output buffer for the resulting IP string.
 * @param ip_size     Size of the ip_out buffer (in bytes).
 * @param prefer_ipv4 When non-zero, prefer IPv4 results over IPv6.
 * @return 0 on success, -1 on failure.
 */
int dns_resolver_lookup(const char *hostname, char *ip_out, size_t ip_size, int prefer_ipv4);

/**
 * Check whether a custom DNS server has been configured.
 *
 * @return 1 if a custom DNS server is in use, 0 otherwise.
 */
int dns_resolver_is_custom(void);

/**
 * Clean up DNS resolver state and reset to default configuration.
 */
void dns_resolver_cleanup(void);

#endif
