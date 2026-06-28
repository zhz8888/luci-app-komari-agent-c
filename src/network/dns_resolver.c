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
 * return the first A/AAAA record as a string in ip_out. */
static int dns_query_server(const char *hostname, const char *dns_server, char *ip_out, size_t ip_size) {
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
    
    unsigned char query_buf[512];
    int query_len = res_mkquery(QUERY, hostname, C_IN, T_A, NULL, 0, NULL, query_buf, sizeof(query_buf));
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
    
    unsigned char response_buf[1024];
    int response_len = recv(sock, response_buf, sizeof(response_buf), 0);
    close(sock);
    
    if (response_len <= 0) {
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
                memcpy(&addr, ns_rr_rdata(rr), sizeof(addr));
                inet_ntop(AF_INET, &addr, ip_out, ip_size);
                return 0;
            } else if (ns_rr_type(rr) == T_AAAA) {
                struct in6_addr addr;
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
    
    if (inet_pton(AF_INET, hostname, ip_out) == 1 || inet_pton(AF_INET6, hostname, ip_out) == 1) {
        strncpy(ip_out, hostname, ip_size - 1);
        ip_out[ip_size - 1] = '\0';
        return 0;
    }
    
    if (g_use_custom_dns) {
        if (dns_query_server(hostname, g_custom_dns, ip_out, ip_size) == 0) {
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
