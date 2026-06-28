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

static void set_socket_timeout(int fd, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int ping_resolve_ip(const char *target, char *ip_out, size_t ip_size, const char *custom_dns) {
    if (inet_pton(AF_INET, target, ip_out) == 1 || inet_pton(AF_INET6, target, ip_out) == 1) {
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
    char ip[INET6_ADDRSTRLEN];
    if (ping_resolve_ip(target, ip, sizeof(ip), custom_dns) != 0) {
        return -1;
    }

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        KOMARI_LOG_ERROR("ICMP socket creation failed (need root): %s", strerror(errno));
        return -1;
    }

    set_socket_timeout(fd, timeout_ms);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    struct {
        struct icmphdr hdr;
        char data[56];
    } packet;

    memset(&packet, 0, sizeof(packet));
    packet.hdr.type = ICMP_ECHO;
    packet.hdr.code = 0;
    packet.hdr.un.echo.id = getpid() & 0xFFFF;
    packet.hdr.un.echo.sequence = 1;
    packet.hdr.checksum = 0;

    int len = sizeof(packet);
    packet.hdr.checksum = compute_checksum((uint16_t *)&packet, len);

    int64_t start_time = get_time_ms();

    if (sendto(fd, &packet, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        KOMARI_LOG_ERROR("ICMP send failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    char recv_buf[1024];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);

    while (1) {
        int64_t now = get_time_ms();
        if (now - start_time >= timeout_ms) {
            close(fd);
            return -1;
        }

        int n = recvfrom(fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&src_addr, &src_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            close(fd);
            return -1;
        }

        struct iphdr *iph = (struct iphdr *)recv_buf;
        if (iph->protocol != IPPROTO_ICMP) {
            continue;
        }

        struct icmphdr *icmph = (struct icmphdr *)(recv_buf + iph->ihl * 4);
        if (icmph->type == ICMP_ECHOREPLY && icmph->un.echo.id == packet.hdr.un.echo.id) {
            int64_t end_time = get_time_ms();
            close(fd);
            return (int)(end_time - start_time);
        }
    }

    close(fd);
    return -1;
}

int ping_task_tcp(const char *target, int timeout_ms, const char *custom_dns) {
    char ip[INET6_ADDRSTRLEN];
    if (ping_resolve_ip(target, ip, sizeof(ip), custom_dns) != 0) {
        return -1;
    }

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

    int port = atoi(port_str);
    if (port <= 0 || port > 65535) {
        port = 80;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        KOMARI_LOG_ERROR("TCP socket creation failed: %s", strerror(errno));
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    int64_t start_time = get_time_ms();

    int ret = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
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
    char url[512];
    if (strncmp(target, "http://", 7) != 0 && strncmp(target, "https://", 8) != 0) {
        snprintf(url, sizeof(url), "http://%s", target);
    } else {
        strncpy(url, target, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }

    char host[256];
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
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, scheme_end, host_len);
        host[host_len] = '\0';
    } else {
        strncpy(host, scheme_end, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    const char *colon = strrchr(host, ':');
    if (colon) {
        port = atoi(colon + 1);
        char host_copy[256];
        strncpy(host_copy, host, sizeof(host_copy) - 1);
        host_copy[colon - host] = '\0';
        strncpy(host, host_copy, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    char ip[INET6_ADDRSTRLEN];
    if (ping_resolve_ip(host, ip, sizeof(ip), custom_dns) != 0) {
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        KOMARI_LOG_ERROR("HTTP socket creation failed: %s", strerror(errno));
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    int64_t start_time = get_time_ms();

    int ret = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
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

        if (SSL_connect(ssl) <= 0) {
            KOMARI_LOG_ERROR("HTTP TLS handshake failed");
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
    }

    char request[1024];
    snprintf(request, sizeof(request),
             "GET / HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: KomariAgent/1.0\r\n"
             "Connection: close\r\n"
             "\r\n", host);

    if (use_tls) {
        SSL_write(ssl, request, strlen(request));
    } else {
        send(fd, request, strlen(request), 0);
    }

    char recv_buf[1024];
    int64_t end_time = start_time;

    if (use_tls) {
        int n = SSL_read(ssl, recv_buf, sizeof(recv_buf) - 1);
        end_time = get_time_ms();
        if (n <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
    } else {
        int n = recv(fd, recv_buf, sizeof(recv_buf) - 1, 0);
        end_time = get_time_ms();
        if (n <= 0) {
            close(fd);
            return -1;
        }
    }

    recv_buf[1023] = '\0';

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

int ping_upload_result(int fd, const char *token, ping_task_result_t *result, bool use_tls, bool ignore_cert) {
    if (!token || !result) {
        return -1;
    }

    char json_buf[512];
    snprintf(json_buf, sizeof(json_buf),
             "{\"type\":\"ping_result\",\"task_id\":%u,\"ping_type\":\"%s\",\"value\":%d,\"finished_at\":%ld}",
             result->task_id, result->ping_type, result->result, (long)result->finished_at);

    KOMARI_LOG_DEBUG("Uploading ping result: %s", json_buf);

    return 0;
}
