/*
 * DNS resolver implementation with optional custom DNS server support.
 * Sends raw DNS A/AAAA queries over UDP to the configured server, falling back
 * to the system resolver (getaddrinfo) on failure.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <resolv.h>
#include <arpa/nameser.h>

#include "dns_resolver.h"
#include "logger.h"

static char g_custom_dns[DNS_RESOLVER_MAX_ADDR_LEN] = "";
static int g_use_custom_dns = 0;

int dns_resolver_init(const char *custom_dns) {
    if (!custom_dns || custom_dns[0] == '\0') {
        g_use_custom_dns = 0;
        g_custom_dns[0] = '\0';
        KOMARI_LOG_INFO("Using system default DNS resolver");
        return 0;
    }
    
    strncpy(g_custom_dns, custom_dns, sizeof(g_custom_dns) - 1);
    g_custom_dns[sizeof(g_custom_dns) - 1] = '\0';
    g_use_custom_dns = 1;
    
    KOMARI_LOG_INFO("Using custom DNS server: %s", g_custom_dns);
    return 0;
}

int dns_resolver_is_custom(void) {
    return g_use_custom_dns;
}

/* Internal helper: query a specific DNS server for `hostname` over UDP and
 * return the first A or AAAA record as a string in ip_out. The record type
 * is selected based on prefer_ipv4: T_A when non-zero, T_AAAA otherwise. */
static int dns_query_server(const char *hostname, const char *dns_server, char *ip_out, size_t ip_size, int prefer_ipv4) {
    struct sockaddr_storage dns_addr;
    socklen_t addr_len = 0;
    
    memset(&dns_addr, 0, sizeof(dns_addr));
    
    if (strchr(dns_server, ':') != NULL) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&dns_addr;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(53);
        if (inet_pton(AF_INET6, dns_server, &addr6->sin6_addr) != 1) {
            return -1;
        }
        addr_len = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&dns_addr;
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(53);
        if (inet_pton(AF_INET, dns_server, &addr4->sin_addr) != 1) {
            return -1;
        }
        addr_len = sizeof(struct sockaddr_in);
    }
    
    /* Select DNS query type based on IP preference: AAAA for IPv6, A for IPv4. */
    int query_type = prefer_ipv4 ? T_A : T_AAAA;
    unsigned char query_buf[512];
    int query_len = res_mkquery(QUERY, hostname, C_IN, query_type, NULL, 0, NULL, query_buf, sizeof(query_buf));
    if (query_len <= 0) {
        return -1;
    }
    
    int sock = socket(dns_addr.ss_family, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (sendto(sock, query_buf, query_len, 0, (struct sockaddr*)&dns_addr, addr_len) < 0) {
        close(sock);
        return -1;
    }
    
    struct sockaddr_storage src_addr;
    socklen_t src_len = sizeof(src_addr);
    memset(&src_addr, 0, sizeof(src_addr));

    unsigned char response_buf[1024];
    ssize_t n = recvfrom(sock, response_buf, sizeof(response_buf), 0,
                         (struct sockaddr *)&src_addr, &src_len);
    close(sock);

    if (n <= 0) {
        return -1;
    }
    int response_len = (int)n;

    /* validate response came from the queried DNS server */
    if (src_len != addr_len ||
        memcmp(&src_addr, &dns_addr, src_len) != 0) {
        return -1;
    }

    /* minimum DNS header size is 12 bytes */
    if (response_len < 12) {
        return -1;
    }

    /* validate transaction ID matches the query */
    uint16_t query_txn_id = ((uint16_t)query_buf[0] << 8) | query_buf[1];
    uint16_t recv_txn_id = ((uint16_t)response_buf[0] << 8) | response_buf[1];
    if (recv_txn_id != query_txn_id) {
        return -1;
    }

    /* validate QR bit is set (response, not query) */
    if (!(response_buf[2] & 0x80)) {
        return -1;
    }
    
    ns_msg handle;
    if (ns_initparse(response_buf, response_len, &handle) < 0) {
        return -1;
    }
    
    int rr_count = ns_msg_count(handle, ns_s_an);
    for (int i = 0; i < rr_count; i++) {
        ns_rr rr;
        if (ns_parserr(&handle, ns_s_an, i, &rr) == 0) {
            if (ns_rr_type(rr) == T_A) {
                struct in_addr addr;
                if (ns_rr_rdlen(rr) < sizeof(addr)) {
                    continue;  /* skip malformed record */
                }
                memcpy(&addr, ns_rr_rdata(rr), sizeof(addr));
                inet_ntop(AF_INET, &addr, ip_out, ip_size);
                return 0;
            } else if (ns_rr_type(rr) == T_AAAA) {
                struct in6_addr addr;
                if (ns_rr_rdlen(rr) < sizeof(addr)) {
                    continue;  /* skip malformed record */
                }
                memcpy(&addr, ns_rr_rdata(rr), sizeof(addr));
                inet_ntop(AF_INET6, &addr, ip_out, ip_size);
                return 0;
            }
        }
    }
    
    return -1;
}

int dns_resolver_lookup(const char *hostname, char *ip_out, size_t ip_size, int prefer_ipv4) {
    if (!hostname || !ip_out || ip_size == 0) {
        return -1;
    }

    /* Validate hostname with temporary buffers to avoid writing binary address
     * data (up to 16 bytes for IPv6) into the caller's string buffer, which
     * may be smaller than sizeof(struct in6_addr). */
    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, hostname, &addr4) == 1 ||
        inet_pton(AF_INET6, hostname, &addr6) == 1) {
        strncpy(ip_out, hostname, ip_size - 1);
        ip_out[ip_size - 1] = '\0';
        return 0;
    }
    
    if (g_use_custom_dns) {
        if (dns_query_server(hostname, g_custom_dns, ip_out, ip_size, prefer_ipv4) == 0) {
            return 0;
        }
        KOMARI_LOG_WARN("Custom DNS %s unreachable, falling back to system resolver", g_custom_dns);
    }
    
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0) {
        KOMARI_LOG_ERROR("DNS resolution failed for %s", hostname);
        return -1;
    }
    
    int ret = -1;
    if (prefer_ipv4) {
        for (p = res; p != NULL; p = p->ai_next) {
            if (p->ai_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
                inet_ntop(AF_INET, &addr->sin_addr, ip_out, ip_size);
                ret = 0;
                break;
            }
        }
        if (ret != 0) {
            for (p = res; p != NULL; p = p->ai_next) {
                if (p->ai_family == AF_INET6) {
                    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)p->ai_addr;
                    inet_ntop(AF_INET6, &addr->sin6_addr, ip_out, ip_size);
                    ret = 0;
                    break;
                }
            }
        }
    } else {
        for (p = res; p != NULL; p = p->ai_next) {
            if (p->ai_family == AF_INET6) {
                struct sockaddr_in6 *addr = (struct sockaddr_in6 *)p->ai_addr;
                inet_ntop(AF_INET6, &addr->sin6_addr, ip_out, ip_size);
                ret = 0;
                break;
            }
        }
        if (ret != 0) {
            for (p = res; p != NULL; p = p->ai_next) {
                if (p->ai_family == AF_INET) {
                    struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
                    inet_ntop(AF_INET, &addr->sin_addr, ip_out, ip_size);
                    ret = 0;
                    break;
                }
            }
        }
    }
    
    freeaddrinfo(res);
    return ret;
}

void dns_resolver_cleanup(void) {
    g_use_custom_dns = 0;
    g_custom_dns[0] = '\0';
}
