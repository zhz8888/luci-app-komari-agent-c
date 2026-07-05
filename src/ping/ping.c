/*
 * Network latency ping task implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <poll.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "ping.h"
#include "utils.h"
#include "logger.h"
#include "dns_resolver.h"

/* Compute the IP checksum (one's complement of the one's complement sum). */
static uint16_t compute_checksum(uint16_t *addr, int len) {
    unsigned long sum = 0;
    while (len > 1) {
        sum += *addr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(unsigned char *)addr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* Get the current monotonic time in milliseconds. */
static int64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Apply a millisecond-granularity send/receive timeout to a socket.
 * Used by TCP and HTTP ping to bound how long a stuck peer can block the
 * ping worker. Both SO_RCVTIMEO and SO_SNDTIMEO are set so a half-open
 * connection fails promptly in either direction. */
static void set_socket_timeout(int fd, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* Detect the address family of a numeric IP literal.
 * Returns AF_INET, AF_INET6, or -1 when the string is not a valid IP
 * literal. Uses temporary in_addr/in6_addr buffers so the caller's
 * destination buffer (which may be smaller than sizeof(struct in6_addr))
 * is never overwritten with binary address data. */
static int parse_ip_family(const char *ip, struct in_addr *addr4,
                           struct in6_addr *addr6) {
    if (inet_pton(AF_INET, ip, addr4) == 1) {
        return AF_INET;
    }
    if (inet_pton(AF_INET6, ip, addr6) == 1) {
        return AF_INET6;
    }
    return -1;
}

/* Strip surrounding brackets from an IPv6 literal in-place, e.g. "[::1]" -> "::1".
 * Mirrors Go strings.Trim(host, "[]") used by tcpPing/httpPing in
 * .komari-agent-main/server/task.go. The buffer is NUL-terminated after
 * the trimmed address. Returns 0 on success, -1 if the brackets are
 * unbalanced. */
static int strip_ipv6_brackets(char *host, size_t host_size) {
    if (host[0] != '[') {
        return 0;
    }
    char *end = strchr(host, ']');
    if (!end) {
        return -1;
    }
    size_t inner = (size_t)(end - host) - 1; /* exclude leading '[' */
    if (inner >= host_size) {
        return -1;
    }
    memmove(host, host + 1, inner);
    host[inner] = '\0';
    return 0;
}

int ping_resolve_ip(const char *target, char *ip_out, size_t ip_size, const char *custom_dns) {
    /* Validate target with temporary buffers to avoid writing binary address
     * data (up to 16 bytes for IPv6) into the caller's string buffer, which
     * may be smaller than sizeof(struct in6_addr). */
    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, target, &addr4) == 1 ||
        inet_pton(AF_INET6, target, &addr6) == 1) {
        strncpy(ip_out, target, ip_size - 1);
        ip_out[ip_size - 1] = '\0';
        return 0;
    }

    /* When a custom DNS is specified, resolve the domain via dns_resolver */
    if (custom_dns && custom_dns[0] != '\0') {
        KOMARI_LOG_DEBUG("Using custom DNS: %s for %s", custom_dns, target);
        dns_resolver_init(custom_dns);
        if (dns_resolver_lookup(target, ip_out, ip_size, 1) == 0) {
            return 0;
        }
        KOMARI_LOG_WARN("Custom DNS resolution failed for %s, falling back to system resolver", target);
    }

    /* Fall back to the system default getaddrinfo */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(target, NULL, &hints, &res);
    if (ret != 0) {
        KOMARI_LOG_ERROR("DNS resolution failed for %s: %s", target, gai_strerror(ret));
        return -1;
    }

    char addr_buf[INET6_ADDRSTRLEN];
    if (res->ai_family == AF_INET) {
        struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, addr_buf, sizeof(addr_buf));
    } else {
        struct sockaddr_in6 *addr = (struct sockaddr_in6 *)res->ai_addr;
        inet_ntop(AF_INET6, &addr->sin6_addr, addr_buf, sizeof(addr_buf));
    }

    strncpy(ip_out, addr_buf, ip_size - 1);
    ip_out[ip_size - 1] = '\0';

    freeaddrinfo(res);
    return 0;
}

int ping_task_icmp(const char *target, int timeout_ms, const char *custom_dns) {
    /* Trim surrounding [] from a bracketed IPv6 literal, e.g. "[::1]" -> "::1".
     * Mirrors Go strings.Trim(host, "[]") used by icmpPing in
     * .komari-agent-main/server/task.go. */
    char host_buf[256];
    const char *host = target;
    if (target[0] == '[') {
        strncpy(host_buf, target, sizeof(host_buf) - 1);
        host_buf[sizeof(host_buf) - 1] = '\0';
        if (strip_ipv6_brackets(host_buf, sizeof(host_buf)) != 0) {
            KOMARI_LOG_ERROR("Unbalanced IPv6 brackets in target: %s", target);
            return -1;
        }
        host = host_buf;
    }

    char ip[INET6_ADDRSTRLEN];
    if (ping_resolve_ip(host, ip, sizeof(ip), custom_dns) != 0) {
        return -1;
    }

    /* Detect the address family of the resolved IP literal using temporary
     * buffers (MAJ-16) so we never write 16-byte IPv6 binary data into a
     * smaller caller-provided buffer. */
    struct in_addr addr4;
    struct in6_addr addr6;
    int family = parse_ip_family(ip, &addr4, &addr6);
    if (family != AF_INET && family != AF_INET6) {
        return -1;
    }

    /* Create a raw socket matching the address family. IPv6 uses
     * IPPROTO_ICMPV6 (T10.2). */
    int fd;
    if (family == AF_INET) {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    } else {
        fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    }
    if (fd < 0) {
        KOMARI_LOG_ERROR("ICMP socket creation failed (need root): %s", strerror(errno));
        return -1;
    }

    set_socket_timeout(fd, timeout_ms);

    /* Build the ICMP/ICMPv6 echo request payload. ICMPv6 checksums are
     * computed by the kernel for raw sockets, so we leave it zero. */
    uint8_t packet[64];
    memset(packet, 0, sizeof(packet));
    int packet_len;
    uint16_t echo_id = (uint16_t)(getpid() & 0xFFFF);

    if (family == AF_INET) {
        struct icmphdr *hdr = (struct icmphdr *)packet;
        hdr->type = ICMP_ECHO;
        hdr->code = 0;
        hdr->un.echo.id = echo_id;
        hdr->un.echo.sequence = 1;
        packet_len = (int)sizeof(struct icmphdr) + 56; /* header + 56 bytes padding */
        hdr->checksum = 0;
        hdr->checksum = compute_checksum((uint16_t *)packet, packet_len);
    } else {
        struct icmp6_hdr *hdr = (struct icmp6_hdr *)packet;
        hdr->icmp6_type = ICMP6_ECHO_REQUEST;
        hdr->icmp6_code = 0;
        hdr->icmp6_id = echo_id;
        hdr->icmp6_seq = 1;
        /* icmp6_cksum is filled in by the kernel for SOCK_RAW on Linux. */
        packet_len = (int)sizeof(struct icmp6_hdr) + 56;
    }

    int64_t start_time = get_time_ms();

    if (family == AF_INET) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_addr = addr4;
        if (sendto(fd, packet, (size_t)packet_len, 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
            KOMARI_LOG_ERROR("ICMP send failed: %s", strerror(errno));
            close(fd);
            return -1;
        }
    } else {
        struct sockaddr_in6 dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_addr = addr6;
        if (sendto(fd, packet, (size_t)packet_len, 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
            KOMARI_LOG_ERROR("ICMPv6 send failed: %s", strerror(errno));
            close(fd);
            return -1;
        }
    }

    char recv_buf[1024];
    struct sockaddr_storage src_addr;
    socklen_t src_len = sizeof(src_addr);

    while (1) {
        int64_t now = get_time_ms();
        if (now - start_time >= timeout_ms) {
            close(fd);
            return -1;
        }

        int n = recvfrom(fd, recv_buf, sizeof(recv_buf), 0,
                         (struct sockaddr *)&src_addr, &src_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                /* EINTR: a signal interrupted recvfrom before any packet
                 * arrived; retry instead of reporting a false ping failure. */
                continue;
            }
            close(fd);
            return -1;
        }

        if (family == AF_INET) {
            struct sockaddr_in *src_v4 = (struct sockaddr_in *)&src_addr;
            /* Reject ICMP responses that originate from a different host than the
               one we probed; otherwise stray packets from other hosts could be
               mistaken for our echo reply. */
            if (src_v4->sin_family != AF_INET ||
                src_v4->sin_addr.s_addr != addr4.s_addr) {
                continue;
            }

            /* Validate that the received datagram is large enough to hold an IP header. */
            if (n < (int)sizeof(struct iphdr)) {
                continue;
            }

            struct iphdr *iph = (struct iphdr *)recv_buf;

            /* Validate the IP header length field (ihl is in 32-bit words, minimum 5). */
            int ip_hdr_len = iph->ihl * 4;
            if (iph->ihl < 5 || ip_hdr_len > n) {
                continue;
            }

            if (iph->protocol != IPPROTO_ICMP) {
                continue;
            }

            /* Ensure the ICMP header fits within the remaining payload. */
            if (ip_hdr_len > n - (int)sizeof(struct icmphdr)) {
                continue;
            }

            struct icmphdr *icmph = (struct icmphdr *)(recv_buf + ip_hdr_len);
            if (icmph->type == ICMP_ECHOREPLY && icmph->un.echo.id == echo_id) {
                int64_t end_time = get_time_ms();
                close(fd);
                return (int)(end_time - start_time);
            }
        } else {
            struct sockaddr_in6 *src_v6 = (struct sockaddr_in6 *)&src_addr;
            /* For IPv6 raw sockets the kernel strips the IPv6 header, so the
             * payload starts with the ICMPv6 header. Verify the source
             * address matches the target before accepting the reply. */
            if (src_v6->sin6_family != AF_INET6 ||
                memcmp(&src_v6->sin6_addr, &addr6, sizeof(addr6)) != 0) {
                continue;
            }

            if (n < (int)sizeof(struct icmp6_hdr)) {
                continue;
            }

            struct icmp6_hdr *icmp6h = (struct icmp6_hdr *)recv_buf;
            if (icmp6h->icmp6_type == ICMP6_ECHO_REPLY &&
                icmp6h->icmp6_id == echo_id) {
                int64_t end_time = get_time_ms();
                close(fd);
                return (int)(end_time - start_time);
            }
        }
    }

    close(fd);
    return -1;
}

int ping_task_tcp(const char *target, int timeout_ms, const char *custom_dns) {
    /* Split host:port first so the host part can be resolved correctly.
       Use strrchr to locate the last colon, which avoids mistaking the
       colons inside a literal IPv6 address for the port separator. For
       bracketed IPv6 literals such as "[::1]:80", strrchr lands on the
       port separator after the closing bracket (T10.3). */
    char host[256];
    char port_str[8] = "80";
    const char *colon = strrchr(target, ':');
    if (colon) {
        size_t host_len = (size_t)(colon - target);
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, target, host_len);
        host[host_len] = '\0';
        strncpy(port_str, colon + 1, sizeof(port_str) - 1);
        port_str[sizeof(port_str) - 1] = '\0';
    } else {
        strncpy(host, target, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    /* Strip surrounding [] from an IPv6 literal, e.g. "[::1]" -> "::1".
     * Mirrors Go strings.Trim(host, "[]") used by tcpPing. */
    if (strip_ipv6_brackets(host, sizeof(host)) != 0) {
        KOMARI_LOG_ERROR("Unbalanced IPv6 brackets in target: %s", target);
        return -1;
    }

    /* Resolve only the host part; inet_pton/getaddrinfo cannot parse
       strings that contain a port suffix. */
    char ip[INET6_ADDRSTRLEN];
    if (ping_resolve_ip(host, ip, sizeof(ip), custom_dns) != 0) {
        return -1;
    }

    int port = atoi(port_str);
    if (port <= 0 || port > 65535) {
        port = 80;
    }

    /* Detect the address family of the resolved IP literal so we can
     * construct the matching sockaddr and socket (T10.3). */
    struct in_addr addr4;
    struct in6_addr addr6;
    int family = parse_ip_family(ip, &addr4, &addr6);
    if (family != AF_INET && family != AF_INET6) {
        return -1;
    }

    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        KOMARI_LOG_ERROR("TCP socket creation failed: %s", strerror(errno));
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        KOMARI_LOG_ERROR("TCP fcntl(O_NONBLOCK) failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    int64_t start_time = get_time_ms();

    int ret;
    if (family == AF_INET) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons((uint16_t)port);
        dest_addr.sin_addr = addr4;
        ret = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    } else {
        struct sockaddr_in6 dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons((uint16_t)port);
        dest_addr.sin6_addr = addr6;
        ret = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }

    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (ret == 0) {
        int64_t end_time = get_time_ms();
        close(fd);
        return (int)(end_time - start_time);
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;

    ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
        close(fd);
        return -1;
    }

    int so_error;
    socklen_t len = sizeof(so_error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);

    int64_t end_time = get_time_ms();
    close(fd);

    if (so_error != 0) {
        return -1;
    }

    return (int)(end_time - start_time);
}

int ping_task_http(const char *target, int timeout_ms, const char *custom_dns) {
    /* If the target is a bare IPv6 literal (no scheme, no brackets), wrap it
     * in [] so the resulting URL has a valid authority. Mirrors Go httpPing:
     *   if ip := net.ParseIP(target); ip != nil && ip.To4() == nil {
     *       target = "[" + target + "]"
     *   }
     * (T10.4)
     */
    char adjusted_target[384]; /* INET6_ADDRSTRLEN + brackets + path margin */
    const char *effective_target = target;
    if (strncmp(target, "http://", 7) != 0 && strncmp(target, "https://", 8) != 0) {
        struct in6_addr addr6_test;
        if (target[0] != '[' && strchr(target, ':') != NULL &&
            inet_pton(AF_INET6, target, &addr6_test) == 1) {
            int adj_n = snprintf(adjusted_target, sizeof(adjusted_target), "[%s]", target);
            if (adj_n < 0 || (size_t)adj_n >= sizeof(adjusted_target)) {
                return -1; /* MAJ-11: snprintf truncation */
            }
            effective_target = adjusted_target;
        }
    }

    char url[512];
    if (strncmp(effective_target, "http://", 7) != 0 &&
        strncmp(effective_target, "https://", 8) != 0) {
        int url_n = snprintf(url, sizeof(url), "http://%s", effective_target);
        if (url_n < 0 || (size_t)url_n >= sizeof(url)) {
            return -1; /* MAJ-11: snprintf truncation */
        }
    } else {
        strncpy(url, effective_target, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }

    /* host_header preserves surrounding [] for IPv6 literals so it can be
     * used verbatim in the HTTP Host header (RFC 2732). A separate stripped
     * copy is produced later for DNS resolution. */
    char host_header[256];
    int port = 80;
    bool use_tls = false;

    const char *scheme_end = strstr(url, "://");
    if (scheme_end) {
        if (strncmp(url, "https", 5) == 0) {
            use_tls = true;
            port = 443;
        }
        scheme_end += 3;
    } else {
        scheme_end = url;
    }

    const char *path_start = strchr(scheme_end, '/');
    if (path_start) {
        size_t host_len = (size_t)(path_start - scheme_end);
        if (host_len >= sizeof(host_header)) host_len = sizeof(host_header) - 1;
        strncpy(host_header, scheme_end, host_len);
        host_header[host_len] = '\0';
    } else {
        strncpy(host_header, scheme_end, sizeof(host_header) - 1);
        host_header[sizeof(host_header) - 1] = '\0';
    }

    /* Split optional port suffix. For bracketed IPv6 literals the closing
     * ']' is followed by ':port'; for IPv4/hostname the only colon in the
     * authority is the port separator. */
    char *colon = strrchr(host_header, ':');
    if (colon) {
        char *bracket = strchr(host_header, ']');
        if (!bracket || colon > bracket) {
            int parsed_port = atoi(colon + 1);
            if (parsed_port > 0 && parsed_port <= 65535) {
                port = parsed_port;
            }
            *colon = '\0'; /* truncate host_header at the port separator */
        }
    }

    /* Build a stripped copy of the host (without []) for DNS resolution.
     * Mirrors Go strings.Trim(host, "[]") used by httpPing. */
    char host_for_resolve[256];
    strncpy(host_for_resolve, host_header, sizeof(host_for_resolve) - 1);
    host_for_resolve[sizeof(host_for_resolve) - 1] = '\0';
    if (strip_ipv6_brackets(host_for_resolve, sizeof(host_for_resolve)) != 0) {
        KOMARI_LOG_ERROR("Unbalanced IPv6 brackets in URL host: %s", host_header);
        return -1;
    }

    char ip[INET6_ADDRSTRLEN];
    if (ping_resolve_ip(host_for_resolve, ip, sizeof(ip), custom_dns) != 0) {
        return -1;
    }

    /* Detect address family so we can open the matching socket (T10.4). */
    struct in_addr addr4;
    struct in6_addr addr6;
    int family = parse_ip_family(ip, &addr4, &addr6);
    if (family != AF_INET && family != AF_INET6) {
        return -1;
    }

    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        KOMARI_LOG_ERROR("HTTP socket creation failed: %s", strerror(errno));
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        KOMARI_LOG_ERROR("HTTP fcntl(O_NONBLOCK) failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    int64_t start_time = get_time_ms();

    int ret;
    if (family == AF_INET) {
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons((uint16_t)port);
        dest_addr.sin_addr = addr4;
        ret = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    } else {
        struct sockaddr_in6 dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons((uint16_t)port);
        dest_addr.sin6_addr = addr6;
        ret = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }

    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (ret != 0) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;

        ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0) {
            close(fd);
            return -1;
        }

        int so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            close(fd);
            return -1;
        }
    }

    SSL *ssl = NULL;
    SSL_CTX *ssl_ctx = NULL;

    if (use_tls) {
        /* OpenSSL compatibility: 1.0.2 uses SSLv23_client_method, 1.1.0+ uses TLS_client_method */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        ssl_ctx = SSL_CTX_new(SSLv23_client_method());
#else
        ssl_ctx = SSL_CTX_new(TLS_client_method());
#endif
        if (!ssl_ctx) {
            close(fd);
            return -1;
        }

        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }

        SSL_set_fd(ssl, fd);

        /* Restore blocking mode for the TLS handshake. SSL_connect on a
           non-blocking socket may return -1 with SSL_ERROR_WANT_READ or
           SSL_ERROR_WANT_WRITE, which the previous code treated as failure.
           Switching back to blocking mode (with the socket timeout already
           set via SO_RCVTIMEO/SO_SNDTIMEO) lets OpenSSL complete the
           handshake synchronously. */
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

        /* Apply socket-level timeouts so the TLS handshake and subsequent
         * SSL_write/SSL_read cannot block indefinitely (T16, MAJ-23).
         * Mirrors Go http.Client{Timeout: timeout}. */
        set_socket_timeout(fd, timeout_ms);

        if (SSL_connect(ssl) <= 0) {
            KOMARI_LOG_ERROR("HTTP TLS handshake failed");
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
    }

    char request[1024];
    int req_n = snprintf(request, sizeof(request),
             "GET / HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: KomariAgent/1.0\r\n"
             "Connection: close\r\n"
             "\r\n", host_header);
    if (req_n < 0 || (size_t)req_n >= sizeof(request)) {
        /* MAJ-11: request truncated; abort to avoid sending a malformed HTTP request. */
        if (use_tls) {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
        }
        close(fd);
        return -1;
    }

    /* Send the HTTP request, looping until the full request is written or
     * an error occurs. SSL_write/send may return fewer bytes than requested
     * on a small send window or partial network write; treating that as
     * success would send a truncated request and the server would respond
     * with an error or time out, producing a false "target unreachable"
     * ping result. */
    size_t req_len = strlen(request);
    if (use_tls) {
        size_t written = 0;
        while (written < req_len) {
            int w = SSL_write(ssl, request + written, (int)(req_len - written));
            if (w <= 0) {
                int ssl_err = SSL_get_error(ssl, w);
                if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
                    continue;
                }
                SSL_free(ssl);
                SSL_CTX_free(ssl_ctx);
                close(fd);
                return -1;
            }
            written += (size_t)w;
        }
    } else {
        size_t sent = 0;
        while (sent < req_len) {
            ssize_t s = send(fd, request + sent, req_len - sent, 0);
            if (s < 0) {
                if (errno == EINTR) continue;
                close(fd);
                return -1;
            }
            sent += (size_t)s;
        }
    }

    char recv_buf[1024];
    int64_t end_time;

    if (use_tls) {
        int n = SSL_read(ssl, recv_buf, sizeof(recv_buf) - 1);
        end_time = get_time_ms();
        if (n <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
        recv_buf[n] = '\0';
    } else {
        int n = recv(fd, recv_buf, sizeof(recv_buf) - 1, 0);
        end_time = get_time_ms();
        if (n <= 0) {
            close(fd);
            return -1;
        }
        recv_buf[n] = '\0';
    }

    if (use_tls) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
    }
    close(fd);

    if (strstr(recv_buf, "HTTP/1.") != NULL) {
        int status_code = 0;
        if (sscanf(recv_buf, "HTTP/%*d.%*d %d", &status_code) == 1) {
            if (status_code >= 200 && status_code < 400) {
                return (int)(end_time - start_time);
            }
        }
    }

    return -1;
}

int ping_task_execute(const char *target, const char *type, ping_task_config_t *config, ping_task_result_t *result) {
    if (!target || !type || !result) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    strncpy(result->ping_target, target, sizeof(result->ping_target) - 1);
    result->ping_target[sizeof(result->ping_target) - 1] = '\0';
    strncpy(result->ping_type, type, sizeof(result->ping_type) - 1);
    result->ping_type[sizeof(result->ping_type) - 1] = '\0';

    int timeout_ms = config ? config->timeout_ms : PING_DEFAULT_TIMEOUT_MS;
    const char *custom_dns = config ? config->custom_dns : NULL;
    int high_latency_threshold = config ? config->high_latency_threshold_ms : PING_HIGH_LATENCY_THRESHOLD_MS;
    int high_latency_retries = config ? config->high_latency_retries : PING_HIGH_LATENCY_RETRIES;

    int64_t latency = -1;

    if (strcmp(type, PING_TYPE_ICMP) == 0) {
        latency = ping_task_icmp(target, timeout_ms, custom_dns);
    } else if (strcmp(type, PING_TYPE_TCP) == 0) {
        latency = ping_task_tcp(target, timeout_ms, custom_dns);
    } else if (strcmp(type, PING_TYPE_HTTP) == 0) {
        latency = ping_task_http(target, timeout_ms, custom_dns);
    } else {
        KOMARI_LOG_ERROR("Unsupported ping type: %s", type);
        result->result = -1;
        return -1;
    }

    if (latency > high_latency_threshold && high_latency_retries > 0) {
        KOMARI_LOG_WARN("High latency detected (%lld ms), retrying %d times",
                        (long long)latency, high_latency_retries);

        for (int i = 0; i < high_latency_retries; i++) {
            int64_t retry_latency = -1;

            if (strcmp(type, PING_TYPE_ICMP) == 0) {
                retry_latency = ping_task_icmp(target, timeout_ms, custom_dns);
            } else if (strcmp(type, PING_TYPE_TCP) == 0) {
                retry_latency = ping_task_tcp(target, timeout_ms, custom_dns);
            } else if (strcmp(type, PING_TYPE_HTTP) == 0) {
                retry_latency = ping_task_http(target, timeout_ms, custom_dns);
            }

            if (retry_latency >= 0 && retry_latency <= high_latency_threshold) {
                latency = retry_latency;
                KOMARI_LOG_INFO("Retry %d succeeded with normal latency (%lld ms)", i + 1, (long long)latency);
                break;
            }

            if (i == high_latency_retries - 1) {
                KOMARI_LOG_WARN("Latency remains high after %d retries", high_latency_retries);
            }
        }
    }

    result->latency_ms = latency;
    result->result = (int)latency;
    result->finished_at = time(NULL);

    return (latency >= 0) ? 0 : -1;
}
